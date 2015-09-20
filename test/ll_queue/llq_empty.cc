#include <ilias/ll_queue.h>
#include "test.h"

using namespace ilias;

int main() {
  ll_queue<int, no_intrusive_tag> q;

  test(q.empty(), "empty queue is empty");
  test(q.size() == 0U, "empty queue has size() == 0");
  return 0;
}
