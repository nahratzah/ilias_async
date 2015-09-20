#include <ilias/ll_queue.h>
#include "test.h"

using namespace ilias;

int main() {
  ll_queue<int, no_intrusive_tag> q;

  test(q.empty(), "before pushfront, queue is empty");
  test(q.size() == 0U, "before pushfront, queue has size 0");

  q.push_front(17);

  test(!q.empty(), "after pushfront, queue is not empty");
  test(q.size() == 1U, "after pushfront, queue has size 1");

  return 0;
}
