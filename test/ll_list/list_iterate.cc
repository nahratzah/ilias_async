#include "list_test.h"

void
test()
{
	list lst;
	lst.link_back(new_test_obj());	/* 0 */
	lst.link_back(new_test_obj());	/* 0, 1 */
	lst.link_back(new_test_obj());	/* 0, 1, 2 */
	lst.link_back(new_test_obj());	/* 0, 1, 2, 3 */
	lst.link_front(new_test_obj());	/* 4, 0, 1, 2, 3 */
	lst.link_front(new_test_obj());	/* 5, 4, 0, 1, 2, 3 */
	lst.link_front(new_test_obj());	/* 6, 5, 4, 0, 1, 2, 3 */
	lst.link_front(new_test_obj());	/* 7, 6, 5, 4, 0, 1, 2, 3 */

	ensure_equal(lst, { 7, 6, 5, 4, 0, 1, 2, 3 });
}
