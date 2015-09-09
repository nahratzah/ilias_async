#include "list_test.h"
#include <thread>


void
push_back(list* lst, int n)
{
	while (n-- > 0)
		lst->link_back(new_test_obj());
}


void
test()
{
	constexpr int COUNT = 100000;
	list lst;

	std::thread t0{ &push_back, &lst, COUNT };
	std::thread t1{ &push_back, &lst, COUNT };
	std::thread t2{ &push_back, &lst, COUNT };
	std::thread t3{ &push_back, &lst, COUNT };

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
