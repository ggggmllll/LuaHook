#include "ffi.h"
#include "lua.h"
#include "lauxlib.h"
#include <stddef.h>
#include "LuaHook.h"

/* ---------- container_of 宏（从 ffi_type* 获得 Structure*） ---------- */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))
#endif

/* ---------- 根据 ffi_type* 获取对应的 Structure（仅对结构体有效） ---------- */
static inline Structure* get_structure(ffi_type* type) {
    if (!type || type->type != FFI_TYPE_STRUCT) return NULL;
    return container_of(type, Structure, type);
}

/* 参数数量检查 */
#define LUA_ARGC_ASSERT(L, num) \
    do { \
        if (lua_gettop(L) != (num)) \
            luaL_error(L, "LuaHook: %s expected %d arguments", __func__, (num)); \
    } while (0)

/* 参数类型检查 */
#define LUA_TYPE_ASSERT(L, type, idx) \
    do { \
        int __idx = (idx); \
        if (!lua_is##type(L, __idx)) \
            luaL_error(L, "LuaHook: %s's argument %d needs type %s", \
                       __func__, __idx, #type); \
    } while (0)

#define LUA_ALLOC_ASSERT(L, mem) \
    do { \
        if (!mem) \
            luaL_error(L, "LuaHook: %s failed to alloc mem", __func__); \
    } while(0)

#define LUA_FUNC_PARSE_ASSERT(L, result) \
    do { \
        LUA_ALLOC_ASSERT(L, result); \
        if (!result[0] || !result[1]) \
            luaL_error(L, "LuaHook: %s's signature need a ret-value and one argument at least", __func__); \
    } while(0)

static inline ffi_type** parse_string_fsm(const char* str) {
    if (!str || !*str) {
        ffi_type** empty = malloc(sizeof(ffi_type*));
        if (empty) *empty = NULL;
        return empty;
    }

    size_t str_len = strlen(str);
    ffi_type** result = malloc((str_len + 1) * sizeof(ffi_type*));
    if (!result) return NULL;

    size_t count = 0;
    const char* p = str;
    const char* key_start = NULL;

    for (; *p; ) {
        if (!key_start) {   /* 正常状态：等待单字符、'|' 或 '...' */
            /* 单字符映射 */
            if (__native_type_map[(unsigned char)*p]) {
                result[count++] = __native_type_map[(unsigned char)*p];
                p++;
                continue;
            }
            /* 进入管道状态 */
            if (*p == '|') {
                key_start = p + 1;
                p++;
                continue;
            }
            /* 检测可变参数标记 '...'（三个连续点） */
            if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
                result[count++] = (ffi_type*)VARIABLE;   // VARIABLE 应定义为 (ffi_type*)-1
                p += 3;
                continue;
            }
            /* 忽略其他字符 */
            p++;
        } else {            /* 管道内状态：收集键，直到遇到下一个'|' */
            if (*p == '|') {
                size_t key_len = p - key_start;
                if (key_len > 0) {
                    char stack_buf[256];
                    char* key = key_len < sizeof(stack_buf) ? stack_buf : malloc(key_len + 1);
                    if (!key && key_len >= sizeof(stack_buf)) {
                        free(result);
                        return NULL;
                    }
                    memcpy(key, key_start, key_len);
                    key[key_len] = '\0';

                    size_t idx = hash_str(key) % __g_struct_map->size;
                    Node* node = __g_struct_map->buckets[idx];
                    while (node) {
                        if (node->key[0] == key[0] &&
                            strlen(node->key) == key_len &&
                            memcmp(node->key, key, key_len) == 0) {
                            result[count++] = &node->type.type;
                            break;
                        }
                        node = node->next;
                    }

                    if (key_len >= sizeof(stack_buf)) free(key);
                }
                key_start = NULL;
                p++;
            } else {
                p++;
            }
        }
    }

    result[count] = NULL;

    /* 压缩数组（可选） */
    if (count + 1 < str_len + 1) {
        ffi_type** shrunk = realloc(result, (count + 1) * sizeof(ffi_type*));
        if (shrunk) result = shrunk;
    }

    return result;
}

int setAbi(lua_State* L) {
    LUA_ARGC_ASSERT(L, 1);
    LUA_TYPE_ASSERT(L, integer, 1);
    ffi_abi abi = lua_tointeger(L, 1);
    if (abi < FFI_FIRST_ABI || abi > FFI_DEFAULT_ABI) luaL_error(L, "LuaHook: bad abi");
    __g_abi = abi;
    return 0;
}

int registerStructType(lua_State* L) {
    LUA_ARGC_ASSERT(L, 2);
    LUA_TYPE_ASSERT(L, string, 1);
    LUA_TYPE_ASSERT(L, string, 2);
    
    INIT_STRUCTMAP(32);
    const char* key = lua_tostring(L, 1);
    const char* sign = lua_tostring(L, 2);
    
    ffi_type** elements = parse_string_fsm(sign);
    LUA_ALLOC_ASSERT(L, elements);
    
    int count = 0;
    for (; *(elements + count); count++);  // 字段个数
    
    size_t* offsets = malloc(count * sizeof(size_t));
    LUA_ALLOC_ASSERT(L, offsets);
    
    char* name = strdup(key);
    LUA_ALLOC_ASSERT(L, name);
    
    Structure type = {
        .type = (ffi_type){
            .size = 0,
            .alignment = 0,
            .type = FFI_TYPE_STRUCT,
            .elements = elements
        },
        .name = name,
        .offsets = offsets   // 仍保留，供 C 内部使用（例如 ffi_get_struct_offsets）
    };
    STRUCTMAP_PUT(key, type);
    
    if (ffi_get_struct_offsets(__g_abi, &STRUCTMAP_GET(key)->type, offsets) == FFI_BAD_TYPEDEF) 
        luaL_error(L, "LuaHook: bad typedef");
    
    return 0;
}

int unregisterStructType(lua_State* L) {
    LUA_ARGC_ASSERT(L, 1);
    LUA_TYPE_ASSERT(L, string, 1);
    const char* key = lua_tostring(L, 1);
    STRUCTMAP_DEL(key);
    return 0;
}

/* ---------- 从 Lua 值转换为 C 值（分配临时内存，返回指针） ---------- */
static void lua_to_cvalue(lua_State* L, int idx, ffi_type* type, void* out) {
    if (type->type == FFI_TYPE_STRUCT) {
        Structure* st = get_structure(type);
        if (!st) luaL_error(L, "LuaHook: Unknown structure type");
        luaL_checktype(L, idx, LUA_TTABLE);

        ffi_type** elems = type->elements;
        size_t* offsets = st->offsets;
        for (int i = 0; elems[i] != NULL; i++) {
            lua_rawgeti(L, idx, i + 1);                // 取数组第 i+1 个元素
            void* field_ptr = (char*)out + offsets[i];
            lua_to_cvalue(L, -1, elems[i], field_ptr); // 递归填充字段
            lua_pop(L, 1);
        }
        return;
    }

    switch (type->type) {
        case FFI_TYPE_SINT8:
        case FFI_TYPE_UINT8:
        case FFI_TYPE_SINT16:
        case FFI_TYPE_UINT16:
        case FFI_TYPE_SINT32:
        case FFI_TYPE_UINT32:
        case FFI_TYPE_SINT64:
        case FFI_TYPE_UINT64: {
            lua_Integer val = luaL_checkinteger(L, idx);
            size_t sz = type->size;
            if (sz == 1)      *(uint8_t*) out = (uint8_t) val;
            else if (sz == 2) *(uint16_t*)out = (uint16_t)val;
            else if (sz == 4) *(uint32_t*)out = (uint32_t)val;
            else if (sz == 8) *(uint64_t*)out = (uint64_t)val;
            break;
        }
        case FFI_TYPE_FLOAT: {
            float val = (float)luaL_checknumber(L, idx);
            *(float*)out = val;
            break;
        }
        case FFI_TYPE_DOUBLE: {
            double val = luaL_checknumber(L, idx);
            *(double*)out = val;
            break;
        }
        case FFI_TYPE_LONGDOUBLE: {
            long double val = (long double)luaL_checknumber(L, idx);
            *(long double*)out = val;
            break;
        }
        case FFI_TYPE_POINTER: {
            void* ptr = lua_touserdata(L, idx);
            *(void**)out = ptr;
            break;
        }
        default:
            luaL_error(L, "LuaHook: Unsupported ffi_type: %d", type->type);
    }
}

/* ---------- 将 C 值转换为 Lua 值并压栈 ---------- */
static void lua_push_cvalue(lua_State* L, void* value, ffi_type* type) {
    if (type->type == FFI_TYPE_STRUCT) {
        Structure* st = get_structure(type);
        if (!st) luaL_error(L, "LuaHook: Unknown structure type");
        lua_newtable(L);
        ffi_type** elems = type->elements;
        size_t* offsets = st->offsets;
        for (int i = 0; elems[i] != NULL; i++) {
            void* field_ptr = (char*)value + offsets[i];
            lua_push_cvalue(L, field_ptr, elems[i]);
            lua_rawseti(L, -2, i + 1);
        }
        return;
    }

    switch (type->type) {
        case FFI_TYPE_VOID:
            lua_pushnil(L);
            break;
        case FFI_TYPE_SINT8:
        case FFI_TYPE_UINT8:
        case FFI_TYPE_SINT16:
        case FFI_TYPE_UINT16:
        case FFI_TYPE_SINT32:
        case FFI_TYPE_UINT32:
        case FFI_TYPE_SINT64:
        case FFI_TYPE_UINT64: {
            lua_Integer v = 0;
            size_t sz = type->size;
            if (sz == 1) v = *(uint8_t*) value;
            else if (sz == 2) v = *(uint16_t*)value;
            else if (sz == 4) v = *(uint32_t*)value;
            else if (sz == 8) v = *(uint64_t*)value;
            lua_pushinteger(L, v);
            break;
        }
        case FFI_TYPE_FLOAT:
            lua_pushnumber(L, *(float*)value);
            break;
        case FFI_TYPE_DOUBLE:
            lua_pushnumber(L, *(double*)value);
            break;
        case FFI_TYPE_LONGDOUBLE:
            lua_pushnumber(L, (double)*(long double*)value);
            break;
        case FFI_TYPE_POINTER:
            lua_pushlightuserdata(L, *(void**)value);
            break;
        default:
            lua_pushnil(L);
            break;
    }
}

/* ---------- enterNativeFunction __call 元方法 ---------- */
int enterNativeFunction(lua_State* L) {
    NativeFunction** ud = (NativeFunction**)lua_touserdata(L, 1);
    if (!ud) luaL_error(L, "LuaHook: Expected NativeFunction userdata");
    NativeFunction* nf = *ud;
    if (!nf) luaL_error(L, "LuaHook: NativeFunction is NULL");

    int nargs = lua_gettop(L) - 1;

    /* ---------- 可变参数分支 ---------- */
    if (nf->is_variadic) {
        if (nargs < nf->nfixed)
            luaL_error(L, "LuaHook: Not enough arguments (need at least %d)", nf->nfixed);
        int nvar = nargs - nf->nfixed;
        int total = nf->nfixed + nvar;

        /* 动态构建参数类型数组（栈上分配） */
        ffi_type** arg_types = alloca(total * sizeof(ffi_type*));
        for (int i = 0; i < nf->nfixed; i++)
            arg_types[i] = nf->fixed_types[i];
        for (int i = 0; i < nvar; i++)
            arg_types[nf->nfixed + i] = nf->var_promoted;

        /* 临时 cif（栈上分配） */
        ffi_cif cif;
        ffi_status s = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI,
                                        nf->nfixed, total,
                                        nf->ret_type, arg_types);
        if (s != FFI_OK) luaL_error(L, "LuaHook: ffi_prep_cif_var failed");

        /* 构造参数指针数组 */
        void* args[total];
        for (int i = 0; i < total; i++) {
            void* buf = alloca(arg_types[i]->size);          // 分配足够空间
            lua_to_cvalue(L, i + 2, arg_types[i], buf);      // 填充值
            args[i] = buf;
        }

        /* 返回值缓冲区 */
        void* ret_buf = NULL;
        if (nf->ret_type->type != FFI_TYPE_VOID) {
            size_t sz = nf->ret_type->size;
            if (nf->ret_type->type >= FFI_TYPE_UINT8 &&
                nf->ret_type->type <= FFI_TYPE_SINT32 &&
                sz < sizeof(ffi_arg))
                sz = sizeof(ffi_arg);
            ret_buf = alloca(sz);
            memset(ret_buf, 0, sz);
        }

        ffi_call(&cif, FFI_FN(nf->func_ptr), ret_buf, args);

        if (nf->ret_type->type != FFI_TYPE_VOID) {
            lua_push_cvalue(L, ret_buf, nf->ret_type);
            return 1;
        }
        return 0;
    }

    /* ---------- 非可变参数分支（使用预先生成的 cif） ---------- */
    if (nargs != nf->nfixed)
        luaL_error(L, "LuaHook: NativeFunction expected %d arguments, got %d",
                   nf->nfixed, nargs);

    void* args[nf->nfixed];
    for (int i = 0; i < nf->nfixed; i++) {
        void* buf = alloca(nf->fixed_types[i]->size);          // 分配足够空间
        lua_to_cvalue(L, i + 2, nf->fixed_types[i], buf);      // 填充值
        args[i] = buf;
    }

    void* ret_buf = NULL;
    if (nf->ret_type->type != FFI_TYPE_VOID) {
        size_t sz = nf->ret_type->size;
        if (nf->ret_type->type >= FFI_TYPE_UINT8 &&
            nf->ret_type->type <= FFI_TYPE_SINT32 &&
            sz < sizeof(ffi_arg))
            sz = sizeof(ffi_arg);
        ret_buf = alloca(sz);
        memset(ret_buf, 0, sz);
    }

    ffi_call(&nf->cif, FFI_FN(nf->func_ptr), ret_buf, args);

    if (nf->ret_type->type != FFI_TYPE_VOID) {
        lua_push_cvalue(L, ret_buf, nf->ret_type);
        return 1;
    }
    return 0;
}

/* ---------- __gc 元方法 ---------- */
static int nativefunction_gc(lua_State* L) {
    NativeFunction** ud = (NativeFunction**)lua_touserdata(L, 1);
    if (ud && *ud) {
        NativeFunction* nf = *ud;
        free(nf->sign_base);      // 释放 parse_string_fsm 返回的数组
        free(nf->fixed_types);    // 释放固定参数类型数组
        free(nf);
        *ud = NULL;
    }
    return 0;
}

/* ---------- wrapNativeFunction 构造函数 ---------- */
int wrapNativeFunction(lua_State* L) {
    LUA_ARGC_ASSERT(L, 2);
    LUA_TYPE_ASSERT(L, lightuserdata, 1);
    LUA_TYPE_ASSERT(L, string, 2);

    void* func_ptr = lua_touserdata(L, 1);
    const char* sign = lua_tostring(L, 2);
    ffi_type** sign_types = parse_string_fsm(sign);
    LUA_FUNC_PARSE_ASSERT(L, sign_types);   // 确保至少有一个返回值和一个参数

    ffi_type* ret_type = sign_types[0];
    ffi_type** params_start = sign_types + 1;

    /* ---------- 解析参数，分离固定参数与可变参数标记 ---------- */
    int nfixed = 0;
    int has_var = 0;
    ffi_type* last_fixed = NULL;
    int i;
    for (i = 0; params_start[i] != NULL; i++) {
        if (params_start[i] == (ffi_type*)VARIABLE) {
            has_var = 1;
            i++;  // 跳过标记
            break;
        }
        nfixed++;
        last_fixed = params_start[i];
    }
    /* 检查标记后是否还有多余参数（违反 ... 语义） */
    if (has_var && params_start[i] != NULL) {
        free(sign_types);
        luaL_error(L, "LuaHook: Variadic marker '...' must be at the end of signature");
    }
    if (has_var && nfixed == 0)
        luaL_error(L, "LuaHook: Variadic function must have at least one fixed argument");

    /* ---------- 构建固定参数类型数组 ---------- */
    ffi_type** fixed_types = NULL;
    if (nfixed > 0) {
        fixed_types = malloc(nfixed * sizeof(ffi_type*));
        LUA_ALLOC_ASSERT(L, fixed_types);
        for (int j = 0; j < nfixed; j++)
            fixed_types[j] = params_start[j];
    }

    /* ---------- 计算可变参数提升类型 ---------- */
    ffi_type* var_promoted = NULL;
    if (has_var && last_fixed) {
        if (last_fixed == &ffi_type_float)
            var_promoted = &ffi_type_double;
        else if (last_fixed == &ffi_type_schar || last_fixed == &ffi_type_uchar ||
                 last_fixed == &ffi_type_sshort || last_fixed == &ffi_type_ushort) {
            /* 根据有符号性提升为 int 或 unsigned int */
            if (last_fixed == &ffi_type_schar || last_fixed == &ffi_type_sshort)
                var_promoted = &ffi_type_sint;
            else
                var_promoted = &ffi_type_uint;
        } else {
            var_promoted = last_fixed;   // 其他类型（double, pointer, struct...）保持不变
        }
    }

    /* ---------- 分配 NativeFunction ---------- */
    NativeFunction* nf = malloc(sizeof(NativeFunction));
    LUA_ALLOC_ASSERT(L, nf);
    nf->func_ptr    = func_ptr;
    nf->ret_type    = ret_type;
    nf->fixed_types = fixed_types;
    nf->nfixed      = nfixed;
    nf->var_promoted = var_promoted;
    nf->is_variadic = has_var;
    nf->sign_base   = sign_types;      // 所有权转移，__gc 中释放

    /* ---------- 非可变参数函数：预先生成 cif ---------- */
    if (!has_var) {
        ffi_status status = ffi_prep_cif(&nf->cif, FFI_DEFAULT_ABI, nfixed,
                                         ret_type, fixed_types);
        if (status != FFI_OK) {
            free(sign_types);
            free(fixed_types);
            free(nf);
            luaL_error(L, "LuaHook: ffi_prep_cif failed: %d", status);
        }
    }

    /* ---------- 创建 full userdata ---------- */
    NativeFunction** ud = (NativeFunction**)lua_newuserdata(L, sizeof(NativeFunction*));
    *ud = nf;

    /* ---------- 设置元表 ---------- */
    luaL_getmetatable(L, "NativeFunction");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        luaL_newmetatable(L, "NativeFunction");
        lua_pushcfunction(L, enterNativeFunction);
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, nativefunction_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);

    return 1;
}

/* ---------- 闭包回调函数 ---------- */
static void lua_closure_callback(ffi_cif* cif, void* ret, void** args, void* user_data) {
    LuaClosureInfo* info = (LuaClosureInfo*)user_data;
    lua_State* L = info->L;
    int top = lua_gettop(L);

    // 压入 Lua 函数
    lua_rawgeti(L, LUA_REGISTRYINDEX, info->func_ref);

    // 压入参数
    for (int i = 0; i < info->nargs; i++) {
        // 使用之前定义的 lua_push_cvalue（需已在项目中实现）
        lua_push_cvalue(L, args[i], info->arg_types[i]);
    }

    // 调用 Lua 函数
    if (lua_pcall(L, info->nargs, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        fprintf(stderr, "Lua closure error: %s\n", err);
        lua_error(L);   // 抛出错误（longjmp）
        return;         // 不会执行到这里
    }

    // 处理返回值
    if (info->ret_type->type != FFI_TYPE_VOID) {
        lua_to_cvalue(L, -1, info->ret_type, ret);      // 直接写入 ret 指向的内存
        lua_pop(L, 1);
    }

    // 恢复栈
    lua_settop(L, top);
}

/* ---------- wrapLuaFunction ---------- */
int wrapLuaFunction(lua_State* L) {
    LUA_ARGC_ASSERT(L, 2);
    LUA_TYPE_ASSERT(L, string, 1);
    LUA_TYPE_ASSERT(L, string, 2);

    const char* func_name = lua_tostring(L, 1);
    const char* sign = lua_tostring(L, 2);

    // 1. 获取 Lua 函数
    lua_getglobal(L, func_name);
    if (!lua_isfunction(L, -1)) {
        luaL_error(L, "LuaHook: %s is not a function", func_name);
    }

    // 2. 解析签名
    ffi_type** sign_types = parse_string_fsm(sign);
    if (!sign_types || !sign_types[0]) {
        free(sign_types);
        luaL_error(L, "LuaHook: invalid signature (missing return type)");
    }
    // 检查可变参数标记（closure 不支持）
    for (int i = 1; sign_types[i] != NULL; i++) {
        if (sign_types[i] == (ffi_type*)VARIABLE) {
            free(sign_types);
            luaL_error(L, "LuaHook: variadic arguments not supported in closure");
        }
    }

    // 3. 计算参数个数
    int nargs = 0;
    while (sign_types[nargs + 1] != NULL) nargs++;

    // 4. 分配 LuaClosureInfo
    LuaClosureInfo* info = malloc(sizeof(LuaClosureInfo));
    if (!info) {
        free(sign_types);
        luaL_error(L, "LuaHook: out of memory");
    }
    info->L = L;
    info->sign_base = sign_types;
    info->ret_type = sign_types[0];
    info->arg_types = sign_types + 1;
    info->nargs = nargs;

    // 5. 获取函数引用（存入注册表）
    lua_pushvalue(L, -1);               // 复制函数
    info->func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);                       // 弹出原始函数

    // 6. 分配 closure
    void* code;
    ffi_closure* closure = ffi_closure_alloc(sizeof(ffi_closure), &code);
    if (!closure) {
        luaL_unref(L, LUA_REGISTRYINDEX, info->func_ref);
        free(sign_types);
        free(info);
        luaL_error(L, "LuaHook: ffi_closure_alloc failed");
    }
    info->writable = closure;

    // 7. 准备 ffi_cif
    ffi_status status = ffi_prep_cif(&info->cif, FFI_DEFAULT_ABI, nargs,
                                  info->ret_type, info->arg_types);
    if (status != FFI_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, info->func_ref);
        ffi_closure_free(closure);
        free(sign_types);
        free(info);
        luaL_error(L, "LuaHook: ffi_prep_cif failed");
    }

    // 8. 准备 closure
    status = ffi_prep_closure_loc(closure, &info->cif, lua_closure_callback,
                                   info, code);
    if (status != FFI_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, info->func_ref);
        ffi_closure_free(closure);
        free(sign_types);
        free(info);
        luaL_error(L, "LuaHook: ffi_prep_closure_loc failed");
    }

    // 9. 插入映射
    map_insert(code, info);

    // 10. 返回 lightuserdata（可执行地址）
    lua_pushlightuserdata(L, code);
    return 1;
}

/* ---------- unwrapLuaFunction ---------- */
int unwrapLuaFunction(lua_State* L) {
    LUA_ARGC_ASSERT(L, 1);
    LUA_TYPE_ASSERT(L, lightuserdata, 1);
    void* code = lua_touserdata(L, 1);

    LuaClosureInfo* info = map_find(code);
    if (!info) return 0;   // 未找到，可能已释放

    map_remove(code);

    // 释放 Lua 函数引用
    luaL_unref(info->L, LUA_REGISTRYINDEX, info->func_ref);

    // 释放 closure
    ffi_closure_free(info->writable);

    // 释放签名数组
    free(info->sign_base);

    // 释放 info 本身
    free(info);

    return 0;
}

int luaopen_LuaHook(lua_State* L) {
    /* 初始化全局结构体映射（确保 __g_struct_map 已创建） */
    INIT_STRUCTMAP(32);

    /* 创建 NativeFunction 元表（若尚未存在） */
    luaL_newmetatable(L, "NativeFunction");
    lua_pushcfunction(L, enterNativeFunction);
    lua_setfield(L, -2, "__call");
    lua_pushcfunction(L, nativefunction_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);  /* 弹出元表 */

    /* 注册所有 API 函数到一张新表中 */
    lua_newtable(L);

    lua_pushcfunction(L, setAbi);
    lua_setfield(L, -2, "setAbi");

    lua_pushcfunction(L, registerStructType);
    lua_setfield(L, -2, "registerStruct");

    lua_pushcfunction(L, unregisterStructType);
    lua_setfield(L, -2, "unregisterStruct");

    lua_pushcfunction(L, wrapNativeFunction);
    lua_setfield(L, -2, "wrapNative");

    lua_pushcfunction(L, wrapLuaFunction);
    lua_setfield(L, -2, "wrapLua");

    lua_pushcfunction(L, unwrapLuaFunction);
    lua_setfield(L, -2, "unwrapLua");

    return 1;  /* 返回包含所有函数的表 */
}