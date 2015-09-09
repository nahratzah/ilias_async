#include "list_test.h"

void
test()
{
	list lst;

	lst.link(lst.end(), new_test_obj());		/* 0 */
	test_obj::ensure_count(1);
	lst.link(lst.end(), new_test_obj());		/* 0, 1 */
	test_obj::ensure_count(2);

	lst.link(lst.begin(), new_test_obj());		/* 2, 0, 1 */
	test_obj::ensure_count(3);

	ensure_equal(lst, { 2, 0, 1 });
}
