# Unit tests. Each is a plain executable returning non-zero on failure; a test
# framework would be the only dependency in a directory whose whole point is
# having none.
#
# Every test is built twice: once against the split modules in src/, and once
# against the generated single header in include/. That is T-13 -- the artifact
# consumers vendor has to behave identically to the sources this suite tests.

set(UCIS_XML_TESTS
    test_ux_order      # T-3
    test_ux_contract   # T-5
    test_ux_sanitize   # T-6
    test_ux_sources)   # T-7, writer half

foreach(t ${UCIS_XML_TESTS})
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE ucis_xml_internal)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${t} COMMAND ${t})

    add_executable(${t}_amalgam ${t}.cpp)
    target_link_libraries(${t}_amalgam PRIVATE ucis_xml)
    target_include_directories(${t}_amalgam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # The amalgamated build includes ucis_xml.hpp, not ux_points.hpp.
    target_compile_definitions(${t}_amalgam PRIVATE UCIS_XML_AMALGAM_TEST=1)
    add_test(NAME ${t}_amalgam COMMAND ${t}_amalgam)
endforeach()

# The contract suite again with UCIS_XML_ASSERT defined, where every violation
# must abort rather than latch. Each case is run in its own process, since the
# first abort ends the run.
add_executable(test_ux_assert test_ux_assert.cpp)
target_link_libraries(test_ux_assert PRIVATE ucis_xml_internal)
target_include_directories(test_ux_assert PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_ux_assert COMMAND test_ux_assert)

# T-13: the generated header has to compile on its own in a bare translation
# unit, with nothing else included first.
add_executable(test_ux_standalone test_ux_standalone.cpp)
target_link_libraries(test_ux_standalone PRIVATE ucis_xml)
add_test(NAME test_ux_standalone COMMAND test_ux_standalone)

# T-12: the no-std::-member invariant (design §9.4). Compiling with
# UCIS_XML_NO_STL is what keeps it true, so it is gated from P1 rather than
# discovered late.
add_executable(test_ux_no_stl test_ux_no_stl.cpp)
target_link_libraries(test_ux_no_stl PRIVATE ucis_xml_internal)
target_include_directories(test_ux_no_stl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_ux_no_stl PRIVATE UCIS_XML_NO_STL=1)
add_test(NAME test_ux_no_stl COMMAND test_ux_no_stl)

# T-11's driver. Built always -- it must not be allowed to rot -- but not
# registered as a test: it needs a 241 MB capture that is not in the
# repository. tests/ucis_xml/test_scale.py drives it when the file is present.
add_executable(scale_replay scale_replay.cpp)
target_link_libraries(scale_replay PRIVATE ucis_xml_internal)
