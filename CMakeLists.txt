cmake_minimum_required(VERSION 3.25.0)

project(LuaHook)

include(ExternalProject)

ExternalProject_Add(
    lua 
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua
    BUILD_COMMAND "make a"
    BUILD_IN_SOURCE TRUE
)

ExternalProject_Add(
    libffi
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libffi
    BUILD_IN_SOURCE TRUE 
    BUILD_COMMAND "./autogen.sh"
    COMMAND "./configure CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN} --prefix=${SOURCE_DIR}/out"
    COMMAND "make"
    COMMAND "make install"
)
