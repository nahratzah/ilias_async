add_executable (test_promise_assign assign.cc)
add_executable (test_promise_lazy assign.cc)
add_executable (test_promise_broken assign.cc)
add_executable (test_promise_except assign.cc)

target_link_libraries (test_promise_assign ilias_async)
target_link_libraries (test_promise_lazy ilias_async)
target_link_libraries (test_promise_broken ilias_async)
target_link_libraries (test_promise_except ilias_async)

add_test (test_promise_assign test_promise_assign)
add_test (test_promise_lazy test_promise_lazy)
add_test (test_promise_broken test_promise_broken)
add_test (test_promise_except test_promise_except)
