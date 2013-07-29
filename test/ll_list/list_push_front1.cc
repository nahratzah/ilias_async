#include "list_test.h"

void
test()
{
	list lst;

	lst.push_front(new_test_obj());
	test_obj::ensure_count(1);
}
