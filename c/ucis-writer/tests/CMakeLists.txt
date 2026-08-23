# One executable per test file, matching c/ucis/tests. Tests link the static
# library and also see src/, because developing split is what makes per-module
# unit tests possible (decision D1).

set(UCIS_WRITER_TESTS
    test_uw_buffer
    test_uw_intfmt
    test_uw_escape
    test_uw_sanitize
    test_uw_stack
    test_uw_tables
    test_uw_order
    test_uw_codecov
    test_uw_cvg
    test_uw_desc
    test_uw_minimal)

foreach(t ${UCIS_WRITER_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE ucis_writer ucis_writer_internal)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# T-6.2: the same fixture through the generated single header, with no library
# to link. This is exactly how a vendoring consumer builds, so it must not see
# src/ and must not link ucis_writer.
add_executable(test_uw_amalgam test_uw_amalgam.c)
target_link_libraries(test_uw_amalgam PRIVATE ucis_writer_public)
target_include_directories(test_uw_amalgam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(test_uw_amalgam ucis_writer_amalgam)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_uw_amalgam PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_uw_amalgam COMMAND test_uw_amalgam)
