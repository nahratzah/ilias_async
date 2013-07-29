#include <ilias/ll_list.h>
#include <ilias/refcnt.h>
#include <atomic>
#include <exception>
#include <iostream>

class test_obj
:	public ilias::ll_list_hook<>,
	public ilias::refcount_base<test_obj>
{
public:
	static std::atomic<unsigned int> count;

	test_obj() noexcept
	{
		count.fetch_add(1U, std::memory_order_relaxed);
	}

	static void ensure_count(unsigned int);

	test_obj(const test_obj&) = delete;
	test_obj(test_obj&&) = delete;
	test_obj& operator=(const test_obj&) = delete;
	test_obj& operator=(test_obj&&) = delete;

	~test_obj() noexcept
	{
		count.fetch_sub(1U, std::memory_order_relaxed);
	}
};

ilias::refpointer<test_obj>
new_test_obj()
{
	return new test_obj{};
}

using list = ilias::ll_smartptr_list<test_obj>;


void test();	/* forward declaration, filled in by each test */


int
main()
{
	test();

	const unsigned int count = test_obj::count;
	if (count != 0) {
		std::cerr << count << " dangling test objects." << std::endl;
		return 1;
	}

	return 0;
}

void
test_obj::ensure_count(unsigned int expected)
{
	const unsigned int count = test_obj::count;
	if (count != expected) {
		std::cerr << "Expected " << expected << " test objects, "
		    "but found " << count << " instead." << std::endl;
		std::terminate();
	}
}

std::atomic<unsigned int> test_obj::count{ 0 };
