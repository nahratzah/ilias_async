add_executable (test_workq_workq_tp workq_tp.cc)

target_link_libraries (test_workq_workq_tp ilias_async)

add_test (test_workq_workq_tp test_workq_workq_tp)
