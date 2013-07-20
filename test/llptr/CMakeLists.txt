add_executable (test_llptr llptr.cc)

target_link_libraries (test_llptr ilias_async)

add_test (test_llptr test_llptr)
