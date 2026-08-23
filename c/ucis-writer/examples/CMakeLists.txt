# Examples double as the schema-validation fixture corpus (plan 7.2): each one
# is built, run, and its output validated. An example that stops compiling or
# stops producing a valid document fails CI like any other test.
#
# Outputs land in ${CMAKE_CURRENT_BINARY_DIR}/out, which tests/ucis_writer/
# walks.

set(UCIS_WRITER_EXAMPLE_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${UCIS_WRITER_EXAMPLE_OUT})

set(UCIS_WRITER_EXAMPLES
    01_hello
    05_statement_coverage
    06_branch_coverage
    07_condition_coverage
    08_toggle_coverage
    09_covergroup
    10_cross
    11_descriptor_api)

foreach(e ${UCIS_WRITER_EXAMPLES})
    add_executable(example_${e} ${e}.c)
    target_link_libraries(example_${e} PRIVATE ucis_writer)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(example_${e} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME example_${e}
             COMMAND example_${e} ${UCIS_WRITER_EXAMPLE_OUT}/${e}.xml)
endforeach()
