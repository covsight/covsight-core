# The examples are the fixture corpus. They are built against the generated
# single header -- the artifact a consumer vendors -- and each writes a document
# that tests/ucis_xml/ validates against ucis.xsd. An example that stops
# compiling, stops running, or starts emitting something the schema rejects
# fails the suite.
#
# They are also the published documentation: the doc pages pull these files in
# with literalinclude between the --8<-- markers, so nothing in the docs can
# drift from code that compiles.

set(UCIS_XML_EXAMPLES
    01_hello
    02_statement
    03_branch
    04_condition
    05_toggle
    06_pointsink
    07_covergroup
    08_cross
    09_fsm_assertion
    10_full_coverage)

set(UCIS_XML_EXAMPLE_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${UCIS_XML_EXAMPLE_OUT})

foreach(e ${UCIS_XML_EXAMPLES})
    add_executable(${e} ${e}.cpp)
    target_link_libraries(${e} PRIVATE ucis_xml)
    target_include_directories(${e} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME example_${e}
             COMMAND ${e} ${UCIS_XML_EXAMPLE_OUT}/${e}.xml)
endforeach()
