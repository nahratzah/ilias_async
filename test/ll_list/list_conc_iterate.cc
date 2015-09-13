#include "list_test.h"
#include <thread>


constexpr unsigned int COUNT = 100000;


void
thrfun(list* lst, int thrnum)
{
	unsigned int count = 0;
	std::cerr << "start " << thrnum << std::endl;
	lst->visit([&count, thrnum](const test_obj&) {
		++count;
#if 0
		if (thrnum == 0)
			std::cout << "visited: " << count << std::endl;
#endif
		if (count > COUNT) {
			std::cerr << "Too many iterations: " << count <<
			    std::endl;
			std::abort();
		}
	});
	std::cerr << "done  " << thrnum << std::endl;

	if (count != COUNT) {
		std::cerr << "Expected " << COUNT << " elements, "
		    "but found " << count << " elements instead." << std::endl;
		std::abort();
	}
}


void
test()
{
	list lst;
	for (unsigned int i = 0; i < COUNT; ++i)
		lst.link_back(new_test_obj());

	test_obj::ensure_count(COUNT);

	std::thread t0{ &thrfun, &lst, 0 };
	std::thread t1{ &thrfun, &lst, 1 };
	std::thread t2{ &thrfun, &lst, 2 };
	std::thread t3{ &thrfun, &lst, 3 };

	t0.join();
	t1.join();
	t2.join();
	t3.join();

	test_obj::ensure_count(COUNT);
}
