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
add_ncdb_test(test_issues)
add_ncdb_test(test_attrs)
add_ncdb_test(test_toggle)
add_ncdb_test(test_fsm)
add_ncdb_test(test_manifest_vendor)
add_ncdb_test(test_manifest_v4)
add_ncdb_test(test_tests_assoc)
add_ncdb_test(test_metrics)
add_ncdb_test(test_scope_trailer)
add_ncdb_test(test_v4_benchmark)
add_ncdb_test(test_history_tree)
add_ncdb_test(test_tags)
add_ncdb_test(test_du_link)
add_ncdb_test(test_cover_fields)
add_ncdb_test(test_covergroup)
add_ncdb_test(test_du_files)

add_executable(test_migration test_migration.c)
target_link_libraries(test_migration PRIVATE ncdb_test_common)
add_test(NAME test_migration COMMAND test_migration)
set_tests_properties(test_migration PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ENVIRONMENT "NCDB_GOLDEN_DIR=${PROJECT_SOURCE_DIR}/../tests/compat/golden")

add_executable(test_v3_to_v4_migration test_v3_to_v4_migration.c)
target_link_libraries(test_v3_to_v4_migration PRIVATE ncdb_test_common)
add_test(NAME test_v3_to_v4_migration COMMAND test_v3_to_v4_migration)
set_tests_properties(test_v3_to_v4_migration PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ENVIRONMENT "NCDB_GOLDEN_DIR=${PROJECT_SOURCE_DIR}/../tests/compat/golden")

file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen_issues_fixture.py CONTENT [=[
import os, sys
sys.path.insert(0, os.path.abspath(r"${PROJECT_SOURCE_DIR}/../python"))
from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.ncdb.issues import (
    IssueSet, IssueSpec,
    SEV_HIGH, SEV_LOW,
    KIND_DESIGN_BUG, KIND_TEST_BUG,
    STATE_OPEN, STATE_IN_PROGRESS, STATE_RESOLVED,
    RES_NONE, RES_FIXED,
    LINK_BLOCKED_BY, LINK_RELATED,
)
from covsight.core.ncdb.issues_history import IssueHistoryWriter

db = MemUCIS()
issues = IssueSet()
issues.add_issue(IssueSpec(
    id="I-001", ext="EXT-1",
    severity=SEV_HIGH, kind=KIND_DESIGN_BUG,
    state=STATE_OPEN, resolution=RES_NONE,
    created_at=1700000000, updated_at=1700000001, synced_at=1700000002))
issues.add_issue(IssueSpec(
    id="I-002", ext="",
    severity=SEV_LOW, kind=KIND_TEST_BUG,
    state=STATE_RESOLVED, resolution=RES_FIXED,
    created_at=1700000010, updated_at=1700000011, synced_at=1700000012))
issues.add_waiver_link("W-001", "I-001")
issues.add_testpoint_link("tp.alpha", "I-001", LINK_BLOCKED_BY)
issues.add_coverage_link("top.block.cp", "bin0", "I-001", LINK_RELATED)
db._issues = issues
db._issues_dirty = True

hist = IssueHistoryWriter()
hist.add("I-001", 1700000100, STATE_OPEN)
hist.add("I-001", 1700000200, STATE_IN_PROGRESS, "triaged")
hist.add("I-001", 1700000300, STATE_RESOLVED, "fixed")
db._issues_history_raw = hist.seal_fast()
db._issues_history_dirty = True

out_path = os.path.join(r"${CMAKE_CURRENT_BINARY_DIR}", "issues_fixture.cdb")
NcdbWriter().write(db, out_path)
print("generated:", out_path)
]=])
add_test(NAME gen_issues_fixture
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/gen_issues_fixture.py)
set_tests_properties(gen_issues_fixture PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(test_issues PROPERTIES
    DEPENDS "gen_issues_fixture;test_roundtrip")

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
