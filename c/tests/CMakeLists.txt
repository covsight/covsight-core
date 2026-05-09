find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(ncdb_test_common INTERFACE)
target_include_directories(ncdb_test_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ncdb_test_common INTERFACE ncdb)

function(add_ncdb_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ncdb_test_common)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

add_ncdb_test(test_varint)
add_ncdb_test(test_strings)
add_ncdb_test(test_counts)
add_ncdb_test(test_roundtrip)

file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/verify_python_read.py CONTENT [=[
import os, sys
sys.path.insert(0, os.path.abspath(r"${PROJECT_SOURCE_DIR}/../python"))
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.api import ScopeTypeT, CoverTypeT
path = os.path.join(r"${CMAKE_CURRENT_BINARY_DIR}", "roundtrip.cdb")
db = NcdbReader().read(path)
roots = list(db.scopes(ScopeTypeT.ALL))
assert len(roots) == 1
assert roots[0].getScopeName() == "top"
children = list(roots[0].scopes(ScopeTypeT.ALL))
assert len(children) == 2
stmt = [s for s in children if s.getScopeName() == "stmt_scope"][0]
branch = [s for s in children if s.getScopeName() == "branch_scope"][0]
stmt_bins = list(stmt.coverItems(CoverTypeT.ALL))
branch_bins = list(branch.coverItems(CoverTypeT.ALL))
assert stmt_bins[0].getName() == "stmt-bin" and stmt_bins[0].getCoverData().data == 7
assert {ci.getName(): ci.getCoverData().data for ci in branch_bins} == {"0 -> 1": 3, "1 -> 0": 4}
print("python verification passed")
]=])
add_test(NAME test_python_read
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/verify_python_read.py)
set_tests_properties(test_python_read PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS test_roundtrip)
