#include <ffi.h>
#include <lua.h>
#include <lauxlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ---------- LuaClosureInfo 结构体 ---------- */
typedef struct LuaClosureInfo {
    lua_State* L;               // Lua 状态
    int func_ref;                // 函数在注册表中的引用
    ffi_type* ret_type;          // 返回值类型
    ffi_type** arg_types;        // 参数类型数组（指向 sign_base+1）
    int nargs;                   // 参数个数
    void* writable;              // 可写地址（用于 ffi_closure_free）
    ffi_cif cif;                 // 预先生成的 ffi_cif
    ffi_type** sign_base;        // parse_string_fsm 返回的原始数组，用于释放
} LuaClosureInfo;

/* ---------- 全局映射：可执行地址 -> LuaClosureInfo ---------- */
typedef struct MapEntry {
    void* code;
    struct LuaClosureInfo* info;
    struct MapEntry* next;
} MapEntry;

static MapEntry* map[256];
static pthread_mutex_t map_mutex = PTHREAD_MUTEX_INITIALIZER;

static void map_insert(void* code, struct LuaClosureInfo* info) {
    unsigned int h = (unsigned long)code % 256;
    pthread_mutex_lock(&map_mutex);
    MapEntry* e = malloc(sizeof(MapEntry));
    e->code = code;
    e->info = info;
    e->next = map[h];
    map[h] = e;
    pthread_mutex_unlock(&map_mutex);
}

static struct LuaClosureInfo* map_find(void* code) {
    unsigned int h = (unsigned long)code % 256;
    pthread_mutex_lock(&map_mutex);
    MapEntry* e = map[h];
    while (e) {
        if (e->code == code) {
            pthread_mutex_unlock(&map_mutex);
            return e->info;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&map_mutex);
    return NULL;
}

static void map_remove(void* code) {
    unsigned int h = (unsigned long)code % 256;
    pthread_mutex_lock(&map_mutex);
    MapEntry** p = &map[h];
    while (*p) {
        if ((*p)->code == code) {
            MapEntry* tmp = *p;
            *p = (*p)->next;
            free(tmp);
            break;
        }
        p = &(*p)->next;
    }
    pthread_mutex_unlock(&map_mutex);
}
