#include "ffi.h"
#include "stdlib.h"
#include "string.h"
#include "pthread.h"
#include <stddef.h>

typedef struct {
    ffi_type type;
    size_t* offsets;
    char* name;
} Structure;

typedef struct Node {
    char* key;
    Structure type;
    struct Node* next;
} Node;

typedef struct {
    Node** buckets;
    size_t size;
    size_t count;
    pthread_rwlock_t lock;  // 改用读写锁提高并发性能
} StructMap;

// 全局哈希表实例
static StructMap* __g_struct_map = NULL;

// 检查编译器是否支持 GNU C 扩展
#ifdef __GNUC__
    #define HAS_GNU_EXTENSIONS 1
#else
    #define HAS_GNU_EXTENSIONS 0
#endif

// ==================== 原子操作和内存屏障 ====================
#if HAS_GNU_EXTENSIONS
// 使用 GCC 内置原子操作进行无锁读
#define ATOMIC_LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ATOMIC_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#else
// 回退方案：使用 volatile 和内存屏障
#include <stdatomic.h>
#define ATOMIC_LOAD(p) atomic_load_explicit((atomic_size_t*)(p), memory_order_acquire)
#define ATOMIC_STORE(p, v) atomic_store_explicit((atomic_size_t*)(p), (v), memory_order_release)
#endif

// ==================== 哈希函数 ====================
static inline size_t hash_str(const char* key) {
    size_t h = 14695981039346656037ULL;
    unsigned char c;
    while ((c = (unsigned char)*key++)) {
        h = (h ^ c) * 1099511628211ULL;
    }
    return h;
}

// ==================== 节点操作 ====================
static inline Node* hash_node_create(const char* key, Structure type) {
    Node* node = (Node*) malloc(sizeof(Node));
    if (!node) return NULL;
    
    node->key = strdup(key); // 使用 strdup 简化代码
    if (!node->key) {
        free(node);
        return NULL;
    }
    node->type = type;
    node->next = NULL;
    return node;
}

static inline void hash_node_free(Node* node) {
    if (node) {
        free(node->key);
        free(node->type.offsets);
        free(node->type.name);
        free(node->type.type.elements);
        free(node);
    }
}

// ==================== 锁优化辅助函数 ====================
// 自动锁管理，确保异常安全
#if HAS_GNU_EXTENSIONS
#define WITH_READ_LOCK(map) \
    pthread_rwlock_rdlock(&(map)->lock); \
    __attribute__((cleanup(__auto_unlock_read))) \
    pthread_rwlock_t* __lock_ptr = &(map)->lock

#define WITH_WRITE_LOCK(map) \
    pthread_rwlock_wrlock(&(map)->lock); \
    __attribute__((cleanup(__auto_unlock_write))) \
    pthread_rwlock_t* __lock_ptr = &(map)->lock

// 自动解锁函数
static inline void __auto_unlock_read(pthread_rwlock_t** lock) {
    if (*lock) pthread_rwlock_unlock(*lock);
}

static inline void __auto_unlock_write(pthread_rwlock_t** lock) {
    if (*lock) pthread_rwlock_unlock(*lock);
}
#else
// 非 GNU C 回退方案
typedef struct {
    StructMap* map;
    int is_write;
} AutoLock;

static inline void auto_lock_cleanup(AutoLock* al) {
    if (al->map) pthread_rwlock_unlock(&al->map->lock);
}

#define WITH_READ_LOCK(map) \
    pthread_rwlock_rdlock(&(map)->lock); \
    AutoLock __al = {map, 0}; \
    (void)__al

#define WITH_WRITE_LOCK(map) \
    pthread_rwlock_wrlock(&(map)->lock); \
    AutoLock __al = {map, 1}; \
    (void)__al
#endif

// ==================== 核心哈希表操作 ====================
#if HAS_GNU_EXTENSIONS
// 使用 GNU C 语句表达式实现的宏版本
static pthread_once_t __g_struct_map_once = PTHREAD_ONCE_INIT;

static void __struct_map_init(void) {
    /* 实际初始化函数，只执行一次 */
    StructMap* __map = (StructMap*) malloc(sizeof(StructMap));
    if (__map) {
        __map->size = 0;        // 占位，后面会通过宏设置
        __map->count = 0;
        __map->buckets = NULL;
        pthread_rwlock_init(&__map->lock, NULL);
        __g_struct_map = __map;
    }
}

#define INIT_STRUCTMAP(sz) ({ \
    pthread_once(&__g_struct_map_once, __struct_map_init); \
    StructMap* __map = __g_struct_map; \
    if (__map && (__map)->size == 0) { \
        __map->buckets = calloc((sz), sizeof(Node*)); \
        if (__map->buckets) { \
            (__map)->size = (sz); \
        } else { \
            pthread_rwlock_destroy(&__map->lock); \
            free(__map); \
            __g_struct_map = NULL; \
            __map = NULL; \
        } \
    } \
    __map; \
})

#define STRUCTMAP_PUT(_key, _type) ({ \
    int __result = 0; \
    if (!__g_struct_map || !(_key)) { \
        __result = -1; \
    } else { \
        WITH_WRITE_LOCK(__g_struct_map); \
        size_t __idx = hash_str(_key) % __g_struct_map->size; \
        Node* __curr = __g_struct_map->buckets[__idx]; \
        Node* __prev = NULL; \
        while (__curr) { \
            if (strcmp(__curr->key, _key) == 0) { \
                (__curr)->type = (_type); \
                __result = 1; \
                break; \
            } \
            __prev = __curr; \
            __curr = __curr->next; \
        } \
        if (!__curr) { \
            Node* __new = hash_node_create(_key, _type); \
            if (__new) { \
                if (__prev) __prev->next = __new; \
                else __g_struct_map->buckets[__idx] = __new; \
                ATOMIC_STORE(&__g_struct_map->count, __g_struct_map->count + 1); \
                __result = 2; \
            } else { \
                __result = -2; \
            } \
        } \
    } \
    __result; \
})

#define STRUCTMAP_GET(_key) ({ \
    Structure* __result = NULL; \
    if (__g_struct_map && (_key)) { \
        size_t __idx = hash_str(_key) % __g_struct_map->size; \
        Node* __curr = __g_struct_map->buckets[__idx]; \
        for (int __i = 0; __i < 3 && __curr; __i++) { \
            if (strcmp(__curr->key, _key) == 0) { \
                __result = &__curr->type; \
                break; \
            } \
            __curr = __curr->next; \
        } \
        if (!__result) { \
            WITH_READ_LOCK(__g_struct_map); \
            __curr = __g_struct_map->buckets[__idx]; \
            while (__curr) { \
                if (strcmp(__curr->key, _key) == 0) { \
                    __result = &__curr->type; \
                    break; \
                } \
                __curr = __curr->next; \
            } \
        } \
    } \
    __result; \
})

#define STRUCTMAP_DEL(_key) ({ \
    int __result = 0; \
    if (__g_struct_map && (_key)) { \
        WITH_WRITE_LOCK(__g_struct_map); \
        size_t __idx = hash_str(_key) % __g_struct_map->size; \
        Node* __curr = __g_struct_map->buckets[__idx]; \
        Node* __prev = NULL; \
        while (__curr) { \
            if (strcmp(__curr->key, _key) == 0) { \
                if (__prev) __prev->next = __curr->next; \
                else __g_struct_map->buckets[__idx] = __curr->next; \
                hash_node_free(__curr); \
                ATOMIC_STORE(&__g_struct_map->count, __g_struct_map->count - 1); \
                __result = 1; \
                break; \
            } \
            __prev = __curr; \
            __curr = __curr->next; \
        } \
    } \
    __result; \
})

#define STRUCTMAP_DESTROY() do { \
    if (__g_struct_map) { \
        pthread_rwlock_wrlock(&__g_struct_map->lock); \
        for (size_t __i = 0; __i < __g_struct_map->size; __i++) { \
            Node* __curr = __g_struct_map->buckets[__i]; \
            while (__curr) { \
                Node* __tmp = __curr->next; \
                hash_node_free(__curr); \
                __curr = __tmp; \
            } \
        } \
        free(__g_struct_map->buckets); \
        pthread_rwlock_unlock(&__g_struct_map->lock); \
        pthread_rwlock_destroy(&__g_struct_map->lock); \
        free(__g_struct_map); \
        __g_struct_map = NULL; \
    } \
} while(0)

#define STRUCTMAP_COUNT() ({ \
    size_t __result = 0; \
    if (__g_struct_map) { \
        __result = ATOMIC_LOAD(&__g_struct_map->count); \
    } \
    __result; \
})

#else
// ==================== 非 GNU C 的内联函数版本 ====================
static inline int init_structmap(size_t size) {
    if (!__g_struct_map) return 1;
    StructMap* map = (StructMap*) malloc(sizeof(StructMap));
    if (!map) return 0;
    
    map->size = size;
    map->count = 0;
    map->buckets = calloc(size, sizeof(Node*));
    if (!map->buckets) {
        free(map);
        return 0;
    }
    
    pthread_rwlock_init(&map->lock, NULL);
    __g_struct_map = map;
    return map;
}

static inline int structmap_put(const char* key, Structure type) {
    if (!__g_struct_map || !key) return -1;
    
    pthread_rwlock_wrlock(&__g_struct_map->lock);
    int result = 0;
    size_t idx = hash_str(key) % __g_struct_map->size;
    Node* curr = __g_struct_map->buckets[idx];
    Node* prev = NULL;
    
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            curr->type = type;
            result = 1;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    if (!curr) {
        Node* new = hash_node_create(key, type);
        if (new) {
            if (prev) prev->next = new;
            else __g_struct_map->buckets[idx] = new;
            __g_struct_map->count++;
            result = 2;
        } else {
            result = -2;
        }
    }
    
    pthread_rwlock_unlock(&__g_struct_map->lock);
    return result;
}

static inline Structure* structmap_get(const char* key) {
    Structure* __result = NULL;
    if (__g_struct_map && (key)) {
        size_t __idx = hash_str(key) % __g_struct_map->size;
        Node* __curr = __g_struct_map->buckets[__idx];
        for (int __i = 0; __i < 3 && __curr; __i++) {
            if (strcmp(__curr->key, key) == 0) {
                __result = &__curr->type;
                break;
            }
            __curr = __curr->next;
        }
        if (!__result) {
            WITH_READ_LOCK(__g_struct_map);
            __curr = __g_struct_map->buckets[__idx];
            while (__curr) {
                if (strcmp(__curr->key, key) == 0) {
                    __result = &__curr->type;
                    break;
                }
                __curr = __curr->next;
            }
        }
    }
    return __result;
}

static inline int structmap_del(const char* key) {
    int __result = 0;
    if (__g_struct_map && (key)) {
        WITH_WRITE_LOCK(__g_struct_map);
        size_t __idx = hash_str(key) % __g_struct_map->size;
        Node* __curr = __g_struct_map->buckets[__idx];
        Node* __prev = NULL;
        while (__curr) {
            if (strcmp(__curr->key, key) == 0) {
                if (__prev) __prev->next = __curr->next;
                else __g_struct_map->buckets[__idx] = __curr->next;
                hash_node_free(__curr);
                ATOMIC_STORE(&__g_struct_map->count, __g_struct_map->count - 1);
                __result = 1;
                break;
            }
            __prev = __curr;
            __curr = __curr->next;
        }
    }
    return __result;
}

static inline void structmap_destroy() {
    if (!__g_struct_map) 
        return;
    pthread_rwlock_wrlock(&__g_struct_map->lock);
    for (size_t __i = 0; __i < __g_struct_map->size; __i++) {
        Node* __curr = __g_struct_map->buckets[__i];
        while (__curr) {
            Node* __tmp = __curr->next;
            hash_node_free(__curr);
            __curr = __tmp;
        }
    }
    free(__g_struct_map->buckets);
    pthread_rwlock_unlock(&__g_struct_map->lock);
    pthread_rwlock_destroy(&__g_struct_map->lock);
    free(__g_struct_map);
    __g_struct_map = NULL;
}

size_t structmap_count() {
    size_t __result = 0;
    if (__g_struct_map) {
        __result = ATOMIC_LOAD(&__g_struct_map->count);
    }
    return __result;
}

#define INIT_STRUCTMAP(size) init_structmap(size)
#define STRUCTMAP_PUT(key, type) structmap_put(key, type)
#define STRUCTMAP_GET(key) structmap_get(key)
#define STRUCTMAP_DEL(key) structmap_del(key)
#define STRUCTMAP_DESTROY() structmap_destroy()
#define STRUCTMAP_COUNT() structmap_count()

#endif // HAS_GNU_EXTENSIONS
