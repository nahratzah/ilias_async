#include "list_test.h"
#include <thread>


void
push_front(list* lst, int n)
{
	while (n-- > 0)
		lst->push_front(new_test_obj());
}


void
test()
{
	constexpr int COUNT = 100000;
	list lst;

	std::thread t0{ &push_front, &lst, COUNT };
	std::thread t1{ &push_front, &lst, COUNT };
	std::thread t2{ &push_front, &lst, COUNT };
	std::thread t3{ &push_front, &lst, COUNT };

	t0.join();
	t1.join();
	t2.join();
	t3.join();

	test_obj::ensure_count(4 * COUNT);
	if (lst.size() != 4 * COUNT) {
		std::cerr << "List count mismatch." << std::endl;
		std::abort();
	}
}
