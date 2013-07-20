#include <ilias/llptr.h>
#include <string>
#include <iostream>


void
test(bool predicate,
    const std::string file, const std::string func, const int line,
    const std::string msg) noexcept
{
	if (!predicate) {
		std::cerr <<
		    "Test failure at " << func << "() in " <<
		      file << ":" << line << "\n" <<
		    msg << std::endl;
		std::terminate();
	}
}

#define test(pred, msg)							\
	test((pred), __FILE__, __func__, __LINE__, (msg));


class test_class
{
public:
	mutable std::atomic<unsigned int> refcnt{ 0U };

	friend void
	refcnt_acquire(const test_class& tc, unsigned int n) noexcept
	{
		tc.refcnt.fetch_add(n);
	}

	friend void
	refcnt_release(const test_class& tc, unsigned int n) noexcept
	{
		tc.refcnt.fetch_sub(n);
	}
};

using pointer = ilias::llptr<test_class, ilias::default_refcount_mgr<test_class>, 2>;
using simple_ptr = ilias::refpointer<test_class, ilias::default_refcount_mgr<test_class>>;
using expect_type = std::tuple<simple_ptr, std::bitset<2>>;

const expect_type expect_nil{ nullptr, 0U };

expect_type
expect_value(simple_ptr ptr) noexcept
{
	return expect_type{ std::move(ptr), 0 };
}

expect_type
expect_value(test_class* tc_ptr) noexcept
{
	return expect_value(simple_ptr{ tc_ptr });
}

std::tuple<test_class*, std::bitset<2>>
expect_noacq(test_class* tc_ptr) noexcept
{
	return std::make_tuple(std::move(tc_ptr), 0);
}


/*
 * TEST CASES
 */


void
load_null() noexcept
{
	pointer p;
	test(p.load() == expect_nil, "p.load() should return nil");
}

void
test_assign() noexcept
{
	test_class tc;
	{
		pointer p;

		/* Scope, to release refcount to e for later. */
		{
			p.store(expect_value(&tc));
			test(p.load() == expect_value(&tc),
			    "p.load() did not return assigned value");
		}

		test(p.load_no_acquire() == expect_noacq(&tc),
		    "p.load_no_acquire() did not return expected value");

		test(tc.refcnt == 1, "expected refcnt of 1");
	}
	test(tc.refcnt == 0, "expected refcnt of 1");
}

void
test_exchange() noexcept
{
	test_class v1, v2;
	{
		pointer p{ expect_value(&v1) };

		test(p.load() == expect_value(&v1),
		    "expected p == &v1");

		{
			auto q = p.exchange(expect_value(&v2));
			test(q == expect_value(&v1),
			    "exchange must return previous value");
			test(p.load() == expect_value(&v2),
			    "exchange must assign new value");
		}

		test(v1.refcnt == 0, "v1 refcnt must be 0");
		test(v2.refcnt == 1, "v2 refcnt must be 0");
	}
	test(v2.refcnt == 0, "v2 refcnt must be 0");
}

void
test_cas_strong() noexcept
{
	test_class v1, v2;
	{
		pointer p{ expect_value(&v1) };

		test(p.load() == expect_value(&v1),
		    "expected p == &v1");

		{
			auto expect = expect_value(&v1);
			bool cas1 = p.compare_exchange_strong(expect, expect_value(&v2));

			test(cas1, "compare_exchange_strong with matchin value may never fail");
			test(expect == expect_value(&v1), "expected old value to be v1");
			test(p.load() == expect_value(&v2), "expected new value to be v2");
		}

		{
			auto expect = expect_value(&v1);
			bool cas1 = p.compare_exchange_strong(expect, expect_value(&v2));

			test(!cas1, "compare_exchange_strong must fail with non-matching expectation");
			test(expect == expect_value(&v2), "expected value was v2");
			test(p.load() == expect_value(&v2), "expected new value to be v2");
		}
	}
	test(v1.refcnt == 0, "v1 refcnt must be 0");
	test(v2.refcnt == 0, "v2 refcnt must be 0");
}

void
test_cas_weak() noexcept
{
	test_class v1, v2;
	{
		pointer p{ expect_value(&v1) };

		test(p.load() == expect_value(&v1),
		    "expected p == &v1");


		/* This must eventually succeed. */
		bool cas1;
		auto expect1 = expect_value(&v1);
		do {
			cas1 = p.compare_exchange_strong(
			    expect1, expect_value(&v2));

			test(expect1 == expect_value(&v1),
			    "expected value is still v1");
			if (!cas1) {
				test(p.load() == expect_value(&v1),
				    "failed cas -> p still points at v1");
			}
		} while (!cas1);
		test(p.load() == expect_value(&v2), "expected new value to be v2");


		/* This may never succeed. */
		for (int i = 0; i < 1000; ++i) {
			auto expect2 = expect_value(&v1);
			bool cas2 = p.compare_exchange_strong(
			    expect2, expect_value(&v2));

			test(!cas2, "cas should fail: "
			    "expected value is a mismatch for p");
			test(expect2 == expect_value(&v2),
			    "expected value is actually v2");
			test(p.load() == expect_value(&v2),
			    "failed cas -> p still points at v2");
		}
		test(p.load() == expect_value(&v2), "expected p == &v2");
	}
	test(v1.refcnt == 0, "v1 refcnt must be 0");
	test(v2.refcnt == 0, "v2 refcnt must be 0");
}


/*
 * MAIN
 */


int
main()
{
	load_null();
	test_assign();
	test_exchange();
	test_cas_strong();
	test_cas_weak();
	return 0;
}
