# ucis_writer - vendorable UCIS write-streaming library.
# See docs/ucis-writer-impl-plan.md.
#
# Two artifacts from one source tree:
#   ucis_writer          static library built from src/, for our own tests
#   ucis_writer.h        generated single-header deliverable, for vendoring
#
# Consumers of either include "ucis_writer.h"; the static library exposes the
# generated header so examples and integration tests are written once and
# compile unchanged in both configurations (test T-6.2).

# Configurable standalone as well as from c/CMakeLists.txt. It has no
# dependencies at all -- not libncdb, not minizip, not zlib -- so a consumer
# evaluating it can build and test it on its own, and so tests/ucis_writer/
# can build the examples without pulling in the rest of the tree.
if(NOT DEFINED PROJECT_NAME)
    cmake_minimum_required(VERSION 3.16)
    project(ucis_writer LANGUAGES C)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)
endif()

set(UCIS_WRITER_SOURCES
    src/uw_buf.c
    src/uw_text.c
    src/uw_error.c
    src/uw_stack.c
    src/uw_tables.c
    src/uw_xml.c
    src/uw_cvg.c
    src/uw_desc.c
    src/uw_props.c
    src/uw_api.c)

set(UCIS_WRITER_HEADERS
    src/uw_public.h
    src/ucis_writer_api.h
    src/uw_config.h
    src/uw_buf.h
    src/uw_types.h
    src/uw_text.h
    src/uw_stack.h
    src/uw_tables.h
    src/uw_xml.h
    src/uw_cvg.h
    src/uw_api.h)

set(UCIS_WRITER_AMALGAM ${CMAKE_CURRENT_SOURCE_DIR}/include/ucis_writer.h)
set(UCIS_WRITER_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    # Regenerate the committed deliverable whenever a source changes, so the
    # working tree is never in the state the CI freshness check rejects.
    add_custom_command(
        OUTPUT ${UCIS_WRITER_AMALGAM}
        COMMAND ${Python3_EXECUTABLE}
                ${UCIS_WRITER_REPO_ROOT}/tools/amalgamate.py
                --output ${UCIS_WRITER_AMALGAM}
        DEPENDS ${UCIS_WRITER_REPO_ROOT}/tools/amalgamate.py
                ${UCIS_WRITER_SOURCES} ${UCIS_WRITER_HEADERS}
        WORKING_DIRECTORY ${UCIS_WRITER_REPO_ROOT}
        COMMENT "Amalgamating ucis_writer.h")
    add_custom_target(ucis_writer_amalgam ALL DEPENDS ${UCIS_WRITER_AMALGAM})
else()
    message(STATUS "ucis_writer: no Python3; using the committed ucis_writer.h")
    add_custom_target(ucis_writer_amalgam)
endif()

add_library(ucis_writer STATIC ${UCIS_WRITER_SOURCES})
add_dependencies(ucis_writer ucis_writer_amalgam)
target_include_directories(ucis_writer
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The vendored artifact has to be clean in a consumer's build, which will not
# be as forgiving as ours. Success criterion 3 in the plan.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ucis_writer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Unit tests reach into src/ directly; that is the point of developing split.
add_library(ucis_writer_internal INTERFACE)
target_include_directories(ucis_writer_internal
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The generated header on its own, with nothing to link -- what a vendoring
# consumer actually has.
add_library(ucis_writer_public INTERFACE)
target_include_directories(ucis_writer_public
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
add_subdirectory(tests)
add_subdirectory(examples)
