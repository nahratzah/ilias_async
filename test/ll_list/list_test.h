#include <ilias/ll_list.h>
#include <ilias/refcount.h>

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


void test() noexcept;


int
main()
{
	test();
	return 0;
}
