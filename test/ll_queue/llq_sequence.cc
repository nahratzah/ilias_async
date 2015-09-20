#include <ilias/ll_queue.h>
#include "test.h"

using namespace ilias;

int main() {
  ll_queue<int, no_intrusive_tag> q;
  q.push_back(0);
  q.push_back(1);
  q.push_back(2);
  q.push_back(3);

  test(!q.empty(), "queue is not empty");
  test(q.size() == 4, "queue has 4 elements");

  test(q.pop_front() == 0, "first popped element is 0");
  test(q.pop_front() == 1, "second popped element is 1");
  test(q.pop_front() == 2, "third popped element is 2");
  test(q.pop_front() == 3, "third popped element is 3");
  test(q.empty(), "after popping all elements, queue is empty");

  return 0;
}
