#include <ilias/ll_queue.h>
#include "test.h"

using namespace ilias;

int main() {
  ll_queue<int, no_intrusive_tag> q;

  test(q.empty(), "before pushback, queue is empty");
  test(q.size() == 0U, "before pushback, queue has size 0");

  q.push_back(17);

  test(!q.empty(), "after pushback, queue is not empty");
  test(q.size() == 1U, "after pushback, queue has size 1");

  return 0;
}
