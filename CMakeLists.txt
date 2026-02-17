cmake_minimum_required(VERSION 3.25.0)

project(LuaHook)

find_package(XShare REQUIRED)

include(ExternalProject)

find_program(MAKE_EXE make)

ExternalProject_Add(
    Lua 
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${MAKE_EXE} CFLAGS="-fPIC" a
    BUILD_IN_SOURCE TRUE
    INSTALL_COMMAND ""
)

ExternalProject_Add(
    libffi
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libffi
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE TRUE 
    BUILD_COMMAND ./autogen.sh
    COMMAND ./configure CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN} --prefix=${CMAKE_CURRENT_SOURCE_DIR}/libffi/out
    COMMAND ${MAKE_EXE}
    INSTALL_COMMAND ${MAKE_EXE} install
)

add_library(LuaHook SHARED)
target_sources(LuaHook
    PRIVATE
        src/LuaHook.c
    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src 
        FILES src/LuaHook.h src/StructMap.h src/LuaMap.h
)
target_include_directories(LuaHook PRIVATE lua)
target_include_directories(LuaHook PRIVATE libffi/out/include)
target_link_directories(LuaHook PRIVATE lua)
target_link_directories(LuaHook PRIVATE libffi/out/lib)

add_dependencies(LuaHook Lua libffi)
target_link_libraries(LuaHook PRIVATE lua ffi m XShare::XShare)

if(BUILD_STATIC_LIB)
    set(STATIC_LIB_NAME ${PROJECT_NAME}-static)    #设置静态库的原始名称

    add_library(${STATIC_LIB_NAME} STATIC $<TARGET_OBJECTS:${PROJECT_NAME}>)
    set_target_properties(${STATIC_LIB_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    
    target_link_libraries(${STATIC_LIB_NAME} PRIVATE lua ffi m XShare::XShare)
endif()