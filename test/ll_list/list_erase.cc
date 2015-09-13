#include "list_test.h"

void
test()
{
	list lst;

	lst.link_back(new_test_obj());
	lst.link_back(new_test_obj());
	test_obj::ensure_count(2);

	lst.erase(lst.begin());
	test_obj::ensure_count(1);
	lst.begin()->ensure_index(1);
}
