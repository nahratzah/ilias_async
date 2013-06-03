add_executable (test_huge_sort huge_sort.cc)

target_link_libraries (test_huge_sort ilias_async)

add_test (test_huge_sort test_huge_sort)
