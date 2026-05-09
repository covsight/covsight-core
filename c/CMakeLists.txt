cmake_minimum_required(VERSION 3.16)
project(ncdb LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MZ_COMPAT    ON  CACHE BOOL "" FORCE)
set(MZ_BZIP2     OFF CACHE BOOL "" FORCE)
set(MZ_LZMA      OFF CACHE BOOL "" FORCE)
set(MZ_PPMD      OFF CACHE BOOL "" FORCE)
set(MZ_ZSTD      OFF CACHE BOOL "" FORCE)
set(MZ_WZAES     OFF CACHE BOOL "" FORCE)
set(MZ_OPENSSL   OFF CACHE BOOL "" FORCE)
set(MZ_LIBBSD    OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_TESTS      OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../packages/minizip-ng ${CMAKE_CURRENT_BINARY_DIR}/minizip-ng)

set(NCDB_SOURCES
    src/ncdb_varint.c
    src/ncdb_zip.c
    src/ncdb_strings.c
    src/ncdb_scope_tree.c
    src/ncdb_counts.c
    src/ncdb_manifest.c
    src/ncdb_history.c
    src/ncdb_sources.c
    src/ncdb_attrs.c
    src/ncdb_toggle.c
    src/ncdb_cross.c
    src/ncdb_fsm.c
    src/ncdb_db.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../packages/cjson/cJSON.c)

add_library(ncdb SHARED ${NCDB_SOURCES})
target_include_directories(ncdb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../packages/cjson
        ${CMAKE_CURRENT_SOURCE_DIR}/../packages/minizip-ng
        ${CMAKE_CURRENT_BINARY_DIR}/minizip-ng)
target_link_libraries(ncdb PUBLIC MINIZIP::minizip)

enable_testing()
add_subdirectory(tests)
