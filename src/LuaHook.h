#include "ffi.h"
#include "StructMap.h"
#include "LuaMap.h"

#define MATCH_NATIVE_TYPE(type) (__native_type_map[type])
#define VARIABLE ((ffi_type*) -1)

typedef enum {
    VOID = 'v',
    SCHAR = 'c',
    UCHAR = 'C',
    SSHORT = 's',
    USHORT = 'S',
    SINT = 'i',
    UINT = 'I',
    FLOAT = 'f',
    SLONG = 'l',
    ULONG = 'L',
    DOUBLE = 'd',
    POINTER = 'p',
    LONGDOUBLE = 'o',
    VARIABLE_ARGS = 3026478,
    STRUCT
} BasicNativeType;

typedef enum {
    NONE = 'v',
    USERDATA = 'p',
    FUNCTION = 'f',
    TABLE = 't',
    STRING = 's',
    INTEGER = 'i',
    NUMBER = 'n' 
} LuaType;

static inline LuaType ctype_to_luatype(BasicNativeType ctype) {
    switch (ctype) {
        case VOID:
            return NONE;
        case SCHAR:
        case UCHAR:
        case SSHORT:
        case USHORT:
        case SINT:
        case UINT:
        case SLONG:
        case ULONG:
            return INTEGER;
        case FLOAT:
        case DOUBLE:
        case LONGDOUBLE:
            return NUMBER;
        case STRUCT:
            return TABLE;
        case POINTER:
            return USERDATA;
        case VARIABLE_ARGS:
            return NONE;
        default:
            return NONE;
    }
}

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