[**中文**](README.md) | [**English**](README_en.md)

# LuaHook: A Dynamic C Function Binding Library

A Lua extension library based on libffi that implements efficient bidirectional calls between Lua and C. It supports structure passing, variadic functions, ABI selection, and more, allowing direct invocation of any C function or exposing Lua functions to C without writing glue code.

## ✨ Key Features

- **Bidirectional Calls**:
  - Wrap any C function pointer into a Lua callable object (`wrapNative`)
  - Wrap a Lua function into a C function pointer (`wrapLua`) via libffi closure
- **Structure Support**:
  - Register C structure layouts; structures are represented as Lua arrays (field order preserved)
  - Automatically compute field offsets and handle alignment correctly
- **Variadic Functions**:
  - C function signatures support the `...` marker; variadic argument types are determined by the last fixed argument and automatically promoted (`float` → `double`, integer types smaller than `int` → `int`/`unsigned int`)
  - Lua closure wrapping does not support variadic arguments
- **ABI Selection**: Dynamically set the calling convention via `setAbi`
- **Type Safety**: Signature parsing + runtime type checking with clear error messages
- **Memory Management**:
  - C function wrappers return full userdata; Lua GC automatically reclaims them
  - Lua function wrappers return lightuserdata; manual release is required (`unwrapLua`)
- **Thread Safety**: Global mapping tables are protected with mutex locks

## 📦 API Reference

All functions are exported through the module table.

### `LuaHook.setAbi(abi)`
Sets the global FFI ABI number. The parameter is an integer within the range `[FFI_FIRST_ABI, FFI_DEFAULT_ABI]` (defined by libffi). Defaults to `FFI_DEFAULT_ABI`; usually no extra configuration is needed.

### `LuaHook.registerStruct(name, signature)`
Registers a C structure type.
- `name`: string, the structure name (can be used later in signatures)
- `signature`: string, a list of field types, e.g., `"ii"` for two `int` fields.
  Signature format: custom structure types are enclosed in `|`.

### `LuaHook.unregisterStruct(name)`
Unregisters a previously registered structure.

### `LuaHook.wrapNative(ptr, signature) -> userdata`
Wraps a C function pointer into a Lua callable object.
- `ptr`: lightuserdata, the C function address
- `signature`: string, function signature in the format `"<return type><param1>[param2][...]"`. For variadic functions, append a `...` marker at the end (e.g., `"ip..."`). Custom structure types are enclosed in `|`.
- Returns: full userdata with a `__call` metamethod; can be called directly in Lua.

### `LuaHook.wrapLua(func_name, signature) -> lightuserdata`
Wraps a Lua function into a C function pointer (via libffi closure).
- `func_name`: string, the name of a global Lua function
- `signature`: string, signature format same as `wrapNative`, but **does not support variadic arguments** (i.e., cannot contain `...`)
- Returns: lightuserdata, the executable address of the generated C function, which can be passed to APIs expecting a C callback.

### `LuaHook.unwrapLua(code)`
Frees resources allocated by `wrapLua`.
- `code`: lightuserdata, the executable address returned by `wrapLua`.

### `LuaHook.getString(ptr)`
Retrieves a string from a `char*` pointer.
- `ptr`: lightuserdata, the address of the string.
- Returns: the string at that address.

### `LuaHook.registerArray(name, element_type, size)`
Registers a C array type.
- `name`: string, the array name (can be used later in signatures)
- `element_type`: string, the element type of the array.
  Signature format: custom structure types are enclosed in `|`.
- `size`: integer, the array size

## 🔢 Type Signature Mapping

### Basic Type Single Characters

| Char | C Type               | Lua Type   | Notes                      |
|------|----------------------|------------|----------------------------|
| `v`  | `void`               | `none`     | Only for return value      |
| `c`  | `signed char`        | `integer`  |                            |
| `C`  | `unsigned char`      | `integer`  |                            |
| `s`  | `signed short`       | `integer`  |                            |
| `S`  | `unsigned short`     | `integer`  |                            |
| `i`  | `signed int`         | `integer`  |                            |
| `I`  | `unsigned int`       | `integer`  |                            |
| `l`  | `signed long`        | `integer`  |                            |
| `L`  | `unsigned long`      | `integer`  |                            |
| `f`  | `float`              | `number`   |                            |
| `d`  | `double`             | `number`   |                            |
| `p`  | `void*` / pointer    | `userdata` | lightuserdata or full userdata |
| `o`  | `long double`        | `number`   |                            |
| `...`| variadic marker      | -          | Only for C function signatures |

### Structure Type
Use the registered structure name enclosed in `|` in signatures, e.g., if `"Point"` is registered, use `|Point|` as a parameter type.

Structures are represented in Lua as **arrays**, with elements in the same order as the structure fields. Nested structures are expanded recursively.

## 📝 Usage Examples

```lua
local luahook = require("LuaHook")

-- Set ABI (usually default 0)
luahook.setAbi(0)

-- Register structure Point { int x, int y }
luahook.registerStruct("Point", "ii")

-- Suppose there is a C function: int add(int a, int b);
-- ptr_add is a lightuserdata obtained elsewhere
local add = luahook.wrapNative(ptr_add, "ii")
local result = add(3, 5)   -- returns 8

-- Suppose there is a C function: void print_point(Point* p);
-- ptr_print_point is the function pointer
local print_point = luahook.wrapNative(ptr_print_point, "vp")
local point = {10, 20}     -- Lua array representing Point
print_point(point)          -- C function receives a pointer, automatically converted

-- Variadic C function: int sum(int count, ...);
local sum = luahook.wrapNative(ptr_sum, "ii...")
-- Variadic part repeats the last fixed parameter type (int) and is promoted
print(sum(3, 1, 2, 3))      -- outputs 6

-- Wrap a Lua function as a C callback
function lua_add(a, b)
    return a + b
end
local c_callback = luahook.wrapLua("lua_add", "iii")
-- c_callback can be passed to APIs expecting a C callback
-- Free when no longer needed
luahook.unwrapLua(c_callback)
```

## 🔧 Compilation and Dependencies

### Dependencies
- Lua 5.1 / 5.2 / 5.3 / 5.4
- libffi
- pthread (for thread safety)

### Build Example
Manual build:
```bash
cmake -B build && cd build && make
```

## ⚠️ Notes

1. **Variadic Limitations**: `wrapLua` does not support variadic signatures because libffi closures cannot handle them. If you need to expose a Lua function as a variadic callback, you must manually wrap it.
2. **Structure Registration Order**: Structures must be registered before they are used as parameters.
3. **Pointer Lifetimes**: The C function pointer passed to `wrapNative` must remain valid for the entire usage period; Lua does not manage C function memory.
4. **Thread Safety**: Global mapping tables are protected by mutexes, but Lua states themselves are not thread-safe. Do not share the same Lua state across multiple threads when using this library.
5. **Error Handling**: Signature parsing failures or type mismatches will throw Lua errors (`lua_error`).

---

Contributions in the form of Issues and PRs are welcome!
