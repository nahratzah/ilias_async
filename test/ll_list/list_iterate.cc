#include "list_test.h"

void
test()
{
	list lst;
	lst.push_back(new_test_obj());	/* 0 */
	lst.push_back(new_test_obj());	/* 0, 1 */
	lst.push_back(new_test_obj());	/* 0, 1, 2 */
	lst.push_back(new_test_obj());	/* 0, 1, 2, 3 */
	lst.push_front(new_test_obj());	/* 4, 0, 1, 2, 3 */
	lst.push_front(new_test_obj());	/* 5, 4, 0, 1, 2, 3 */
	lst.push_front(new_test_obj());	/* 6, 5, 4, 0, 1, 2, 3 */
	lst.push_front(new_test_obj());	/* 7, 6, 5, 4, 0, 1, 2, 3 */

	ensure_equal(lst, { 7, 6, 5, 4, 0, 1, 2, 3 });
}
