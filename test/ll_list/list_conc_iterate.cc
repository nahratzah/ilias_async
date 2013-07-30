#include "list_test.h"
#include <thread>


constexpr unsigned int COUNT = 100000;


void
thrfun(list* lst)
{
	unsigned int count = 0;
	lst->visit([&count](const test_obj&) { ++count; });

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
		lst.push_back(new_test_obj());

	test_obj::ensure_count(COUNT);

	std::thread t0{ &thrfun, &lst };
	std::thread t1{ &thrfun, &lst };
	std::thread t2{ &thrfun, &lst };
	std::thread t3{ &thrfun, &lst };

	t0.join();
	t1.join();
	t2.join();
	t3.join();

	test_obj::ensure_count(COUNT);
}
