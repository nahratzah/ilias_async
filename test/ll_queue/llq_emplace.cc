#include <ilias/ll_queue.h>
#include "test.h"

using namespace ilias;

int main() {
  ll_queue<int, no_intrusive_tag> q;

  q.emplace_back(17);
  q.emplace_front(13);
  q.emplace_back(19);
  q.emplace_front(11);

  test(q.pop_front() == 11, "first popped element is 11");
  test(q.pop_front() == 13, "second popped element is 13");
  test(q.pop_front() == 17, "third popped element is 17");
  test(q.pop_front() == 19, "fourth popped element is 19");

  return 0;
}
