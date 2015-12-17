#include <ilias/ll_queue.h>
#include "test.h"

using namespace ilias;

int main() {
  ll_queue<int, no_intrusive_tag> q;
  q.push_front(0);
  q.push_front(1);
  q.push_front(2);
  q.push_front(3);

  test(!q.empty(), "queue is not empty");
  test(q.size() == 4, "queue has 4 elements");

  opt_data<int> popped;
  test((popped = q.pop_front()) == 3, "first popped element is 3");
  test((popped = q.pop_front()) == 2, "second popped element is 2");
  test((popped = q.pop_front()) == 1, "third popped element is 1");
  test((popped = q.pop_front()) == 0, "third popped element is 0");
  test(q.empty(), "after popping all elements, queue is empty");

  return 0;
}
