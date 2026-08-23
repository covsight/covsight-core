# ucis_xml - header-only UCIS-XML coverage writer.
# See docs/ucis-xml-cpp-impl-plan.md.
#
# Two targets from one source tree, mirroring c/ucis-writer's arrangement:
#   ucis_xml_internal   the split modules in src/, for unit tests
#   ucis_xml            the generated single header in include/, which is what a
#                       consumer vendors
# Tests and examples compile against whichever is asked for, so the same source
# proves both configurations work (T-13).
#
# Configurable standalone as well as from the top of the tree, and with no
# dependencies at all, so a consumer evaluating it can build and test it on its
# own.
if(NOT DEFINED PROJECT_NAME)
    cmake_minimum_required(VERSION 3.16)
    project(ucis_xml LANGUAGES CXX)
endif()

# C++17 is the floor, verified against Verilator 5.041's
# CFG_CXXFLAGS_STD_NEWEST = -std=gnu++17 (decision D-6). Extensions off so the
# header cannot come to depend on one.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(UCIS_XML_SOURCES
    src/ux_config.hpp
    src/ux_util.hpp
    src/ux_buf.hpp
    src/ux_text.hpp
    src/ux_error.hpp
    src/ux_arena.hpp
    src/ux_key.hpp
    src/ux_stage.hpp
    src/ux_render.hpp
    src/ux_render_code.hpp
    src/ux_render_func.hpp
    src/ux_intern.hpp
    src/ux_spool.hpp
    src/ux_doc.hpp
    src/ux_api.hpp
    src/ux_points.hpp)

set(UCIS_XML_AMALGAM ${CMAKE_CURRENT_SOURCE_DIR}/include/ucis_xml.hpp)
set(UCIS_XML_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/amalgam.toml)
set(UCIS_XML_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    # Regenerate the committed deliverable whenever a source changes, so the
    # working tree is never in the state the CI freshness check rejects.
    add_custom_command(
        OUTPUT ${UCIS_XML_AMALGAM}
        COMMAND ${Python3_EXECUTABLE}
                ${UCIS_XML_REPO_ROOT}/tools/amalgamate.py
                --manifest ${UCIS_XML_MANIFEST}
                --output ${UCIS_XML_AMALGAM}
        DEPENDS ${UCIS_XML_REPO_ROOT}/tools/amalgamate.py
                ${UCIS_XML_MANIFEST} ${UCIS_XML_SOURCES}
        WORKING_DIRECTORY ${UCIS_XML_REPO_ROOT}
        COMMENT "Amalgamating ucis_xml.hpp")
    add_custom_target(ucis_xml_amalgam ALL DEPENDS ${UCIS_XML_AMALGAM})
else()
    message(STATUS "ucis_xml: no Python3; using the committed ucis_xml.hpp")
    add_custom_target(ucis_xml_amalgam)
endif()

# The split modules, for unit tests that reach into one module at a time --
# which is the whole reason the library is developed split (D-9).
add_library(ucis_xml_internal INTERFACE)
target_include_directories(ucis_xml_internal
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The generated header on its own: what a vendoring consumer actually has.
add_library(ucis_xml INTERFACE)
add_dependencies(ucis_xml ucis_xml_amalgam)
target_include_directories(ucis_xml
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The vendored artifact has to be clean in a consumer's build, which will not be
# as forgiving as ours.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_subdirectory(tests)
add_subdirectory(examples)
