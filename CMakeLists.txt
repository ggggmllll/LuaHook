cmake_minimum_required(VERSION 3.25.0)

project(LuaFFI)

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

ExternalProject_Add(xshare
    PREFIX            ${CMAKE_CURRENT_BINARY_DIR}/xshare-prefix
    SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/XShare
    BINARY_DIR        ${CMAKE_CURRENT_BINARY_DIR}/xshare-build
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -DBUILD_STATIC_LIB=ON <SOURCE_DIR>
    BUILD_COMMAND     ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>
    INSTALL_COMMAND   ""
)
ExternalProject_Get_Property(xshare BINARY_DIR)

add_library(LuaFFI SHARED)
target_sources(LuaFFI
    PRIVATE
        src/LuaFFI.c
    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src 
        FILES src/LuaFFI.h src/StructMap.h src/LuaMap.h
)
target_include_directories(LuaFFI PRIVATE lua)
target_include_directories(LuaFFI PRIVATE libffi/out/include)
target_include_directories(LuaFFI PRIVATE XShare/src)
target_link_directories(LuaFFI PRIVATE lua)
target_link_directories(LuaFFI PRIVATE libffi/out/lib)
target_link_directories(LuaFFI PRIVATE ${BINARY_DIR})

add_dependencies(LuaFFI Lua libffi xshare)
target_link_libraries(LuaFFI PRIVATE lua ffi m XShare)

if(BUILD_STATIC_LIB)
    set(STATIC_LIB_NAME ${PROJECT_NAME}-static)    #设置静态库的原始名称

    add_library(${STATIC_LIB_NAME} STATIC $<TARGET_OBJECTS:${PROJECT_NAME}>)
    set_target_properties(${STATIC_LIB_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    
    target_link_libraries(${STATIC_LIB_NAME} PRIVATE lua ffi m XShare)
endif()