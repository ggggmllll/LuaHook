#include "ffi.h"
#include "StructMap.h"
#include "LuaMap.h"

#define MATCH_NATIVE_TYPE(type) (__native_type_map[type])
#define VARIABLE ((ffi_type*) -1)

static ffi_type* __native_type_map[128] = {
    ['v'] = &ffi_type_void,
    ['c'] = &ffi_type_schar,
    ['C'] = &ffi_type_uchar,
    ['s'] = &ffi_type_sshort,
    ['S'] = &ffi_type_ushort,
    ['i'] = &ffi_type_sint,
    ['I'] = &ffi_type_uint,
    ['l'] = &ffi_type_slong,
    ['L'] = &ffi_type_ulong,
    ['f'] = &ffi_type_float,
    ['d'] = &ffi_type_double,
    ['p'] = &ffi_type_pointer,
    ['o'] = &ffi_type_longdouble
};

static ffi_abi __g_abi = FFI_DEFAULT_ABI;

/* ---------- NativeFunction 结构体 ---------- */
typedef struct NativeFunction {
    void*       func_ptr;       // 目标 C 函数指针
    ffi_type*   ret_type;       // 返回值类型
    ffi_type**  fixed_types;    // 固定参数类型数组（原始，未经提升）
    int         nfixed;         // 固定参数个数
    ffi_type*   var_promoted;   // 可变参数提升后的类型（仅当 is_variadic）
    int         is_variadic;    // 是否为可变参数函数
    ffi_cif     cif;            // 非可变参数时预先生成
    ffi_type**  sign_base;      // parse_string_fsm 返回的原始数组，用于释放
} NativeFunction;

int luaopen_LuaFFI(lua_State* L);