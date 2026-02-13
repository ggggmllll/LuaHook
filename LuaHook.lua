local luahook = require("LuaHook")

-- 暴露类型枚举常量
local types = {
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
    VARIABLE_ARGS = '...' -- 可变参数标记
}

-- 注册数组（通过结构体模拟）
local function registerArray(name, element_type, size)
    -- 构建结构体签名：size 个相同类型的字段
    local signature = ""
    for i = 1, size do
        signature = signature .. element_type
    end
    registerStruct(name, signature)
end

-- 主模块
local LuaHook = {
    types = types,
    registerStruct = luahook.registerStruct,
    registerArray = registerArray,
    setAbi = luahook.setAbi,
    unregisterStruct = luahook.unregisterStruct,
    wrapNative = luahook.wrapNative,
    wrapLua = luahook.wrapLua,
    unwrapLua = luahook.unwrapLua,
    getString = luahook.getString
}

return LuaHook