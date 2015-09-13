#include "list_test.h"


void test() {
  list lst;
  unsigned int count = 0;
  lst.visit([&count](const test_obj&) {
              ++count;
            });

  if (count != 0) {
    std::cerr << "Expected 0 elements, "
                 "but found " << count << " elements instead." << std::endl;
    std::abort();
  }
}
