add_executable (test_list_compile list_compile.cc)

target_link_libraries (test_list_compile ilias_async)

add_test (test_list_compile test_list_compile)
