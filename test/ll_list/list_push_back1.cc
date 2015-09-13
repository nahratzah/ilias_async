#include "list_test.h"

void
test()
{
	list lst;

	lst.link_back(new_test_obj());
	test_obj::ensure_count(1);
}
