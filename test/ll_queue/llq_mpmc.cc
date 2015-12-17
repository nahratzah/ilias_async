#include <ilias/ll_queue.h>
#include <future>
#include <iterator>
#include <vector>
#include "test.h"

using std::string;
using std::vector;
using vtype = std::tuple<string, int>;
using std::get;

vector<vtype> generate_tagged_numbers(const string& first, int n) {
  vector<vtype> rv;
  rv.reserve(n);

  for (int i = 0; i < n; ++i)
    rv.emplace_back(first, i);
  return rv;
}

vector<vtype> select_tag(const vector<vtype>& data, const string& first) {
  vector<vtype> rv;
  std::copy_if(data.begin(), data.end(), std::back_inserter(rv),
               [&first](const vtype& elem) { return get<0>(elem) == first; });
  return rv;
}

bool vtype_compare(const vtype& a, const vtype& b) noexcept {
  return get<1>(a) < get<1>(b);
}

bool is_sorted(const vector<vtype>& data) {
  return std::is_sorted(data.begin(), data.end(), &vtype_compare);
}

using namespace ilias;


void push_back(ll_queue<vtype, no_intrusive_tag>* dst, vector<vtype>* elems) {
  for (const auto& elem : *elems)
    dst->push_back(elem);
}

vector<vtype> pop_front(ll_queue<vtype, no_intrusive_tag>* dst, int n) {
  vector<vtype> rv;
  rv.reserve(n);

  while (n-- > 0) {
    auto front = dst->pop_front();
    while (!front) front = dst->pop_front();
    rv.push_back(*front);
  }
  return rv;
}


const int COUNT = 100000;

int main() {
  auto elems_a = generate_tagged_numbers("a", COUNT);
  auto elems_b = generate_tagged_numbers("b", COUNT);
  ll_queue<vtype, no_intrusive_tag> q;

  /* Start 2 producers, 2 consumers. */
  auto pop_1 = std::async(std::launch::async, &pop_front, &q, elems_a.size());
  auto pop_2 = std::async(std::launch::async, &pop_front, &q, elems_b.size());
  auto push_a = std::async(std::launch::async, &push_back, &q, &elems_a);
  auto push_b = std::async(std::launch::async, &push_back, &q, &elems_b);
  std::cout << "started push and pop threads." << std::endl;
  /* Wait for producers to complete. */
  push_a.get();
  std::cout << "push thread for 'a' finished." << std::endl;
  push_b.get();
  std::cout << "push thread for 'b' finished." << std::endl;

  /* Get result from consumers. */
  auto elems_1 = pop_1.get();
  std::cout << "got " << elems_1.size() << " elements for pop thread 1"
            << std::endl;
  auto elems_2 = pop_2.get();
  std::cout << "got " << elems_2.size() << " elements for pop thread 2"
            << std::endl;

  test(is_sorted(select_tag(elems_1, "a")),
       "expect consumer 1 to see elements (tagged \"a\") in order");
  test(is_sorted(select_tag(elems_1, "b")),
       "expect consumer 1 to see elements (tagged \"b\") in order");
  test(is_sorted(select_tag(elems_2, "a")),
       "expect consumer 2 to see elements (tagged \"a\") in order");
  test(is_sorted(select_tag(elems_2, "b")),
       "expect consumer 2 to see elements (tagged \"b\") in order");

  return 0;
}
