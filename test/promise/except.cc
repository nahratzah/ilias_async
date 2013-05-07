#include <ilias/promise.h>
#include <stdexcept>

class test_error
:	public virtual std::runtime_error
{
public:
	test_error()
	:	std::runtime_error("This exception is supposed to occur.")
	{
		/* Empty body. */
	}
};

int
main()
{
	ilias::future<int> f = ilias::new_promise<int>([](promise<int> p) {
		throw test_error();
	    });
	bool ok = false;

	try {
		f.get();
	} catch (const test_error&) {
		ok = true;
	}
	assert(ok && "error should cascade into this body");

	assert(f.ready());
	assert(f.is_broken());
	return 0;
}
