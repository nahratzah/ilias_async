add_executable (test_threadpool_workq_tp workq_tp.cc)

target_link_libraries (test_threadpool_workq_tp ilias_async)

add_test (test_threadpool_workq_tp test_threadpool_workq_tp)
