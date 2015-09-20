#include <iostream>

void test(bool predicate, const std::string file, const std::string func,
          const int line, const std::string msg) noexcept {
  if (!predicate) {
    std::cerr << "Test failure at " << func << "() in "
              << file << ":" << line << "\n" << msg << std::endl;
    std::terminate();
  }
}

#define test(pred, msg)                                                 \
            test((pred), __FILE__, __func__, __LINE__, (msg));
