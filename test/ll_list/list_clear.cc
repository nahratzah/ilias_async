#include "list_test.h"

void
test()
{
	const unsigned int COUNT = 10;
	list lst;

	for (unsigned int i = 0; i < COUNT; ++i)
		lst.push_back(new_test_obj());
	test_obj::ensure_count(COUNT);
	lst.clear();
	test_obj::ensure_count(0);
}
