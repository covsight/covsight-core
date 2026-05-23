# Phase 2 of docs/ucis-impl-plan.md: NCDB UCIS C API shim.

set(NCDB_UCIS_SOURCES
    src/ucis_lifecycle.c
    src/ucis_filehandle.c
    src/ucis_scope.c
    src/ucis_history.c
    src/ucis_cover.c
    src/ucis_property.c
    src/ucis_covergroup.c)

add_library(ncdb_ucis STATIC ${NCDB_UCIS_SOURCES})
target_include_directories(ncdb_ucis
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../src) # for ncdb_impl.h (in-tree shim)
target_link_libraries(ncdb_ucis PUBLIC ncdb)

add_subdirectory(tests)
