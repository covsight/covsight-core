add_executable(test_ucis_header test_ucis_header.c)
target_link_libraries(test_ucis_header PRIVATE ncdb_ucis)
add_test(NAME test_ucis_header COMMAND test_ucis_header)

add_executable(test_ucis_lifecycle test_ucis_lifecycle.c)
target_link_libraries(test_ucis_lifecycle PRIVATE ncdb_ucis)
add_test(NAME test_ucis_lifecycle COMMAND test_ucis_lifecycle)

add_executable(test_ucis_scope test_ucis_scope.c)
target_link_libraries(test_ucis_scope PRIVATE ncdb_ucis)
add_test(NAME test_ucis_scope COMMAND test_ucis_scope)

add_executable(test_ucis_cover test_ucis_cover.c)
target_link_libraries(test_ucis_cover PRIVATE ncdb_ucis)
target_include_directories(test_ucis_cover PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_cover COMMAND test_ucis_cover)

add_executable(test_ucis_property test_ucis_property.c)
target_link_libraries(test_ucis_property PRIVATE ncdb_ucis m)
target_include_directories(test_ucis_property PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_property COMMAND test_ucis_property)

add_executable(test_ucis_srcinfo test_ucis_srcinfo.c)
target_link_libraries(test_ucis_srcinfo PRIVATE ncdb_ucis)
target_include_directories(test_ucis_srcinfo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_srcinfo COMMAND test_ucis_srcinfo)

add_executable(test_ucis_testdata test_ucis_testdata.c)
target_link_libraries(test_ucis_testdata PRIVATE ncdb_ucis m)
target_include_directories(test_ucis_testdata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_testdata COMMAND test_ucis_testdata)

add_executable(test_ucis_covergroup test_ucis_covergroup.c)
target_link_libraries(test_ucis_covergroup PRIVATE ncdb_ucis)
target_include_directories(test_ucis_covergroup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_covergroup COMMAND test_ucis_covergroup)

add_executable(test_ucis_increment test_ucis_increment.c)
target_link_libraries(test_ucis_increment PRIVATE ncdb_ucis)
target_include_directories(test_ucis_increment PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_increment COMMAND test_ucis_increment)

add_executable(test_ucis_r7_assertion test_ucis_r7_assertion.c)
target_link_libraries(test_ucis_r7_assertion PRIVATE ncdb_ucis)
target_include_directories(test_ucis_r7_assertion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_r7_assertion COMMAND test_ucis_r7_assertion)

add_executable(test_ucis_props test_ucis_props.c)
target_link_libraries(test_ucis_props PRIVATE ncdb_ucis m)
target_include_directories(test_ucis_props PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_props COMMAND test_ucis_props)

add_executable(test_ucis_unique_id test_ucis_unique_id.c)
target_link_libraries(test_ucis_unique_id PRIVATE ncdb_ucis)
target_include_directories(test_ucis_unique_id PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_unique_id COMMAND test_ucis_unique_id)

add_executable(test_ucis_formal test_ucis_formal.c)
target_link_libraries(test_ucis_formal PRIVATE ncdb_ucis)
target_include_directories(test_ucis_formal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_test(NAME test_ucis_formal COMMAND test_ucis_formal)
