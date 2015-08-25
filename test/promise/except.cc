#include <ilias/future.h>
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
	ilias::cb_future<int> f = ilias::async_lazy([]() -> int {
		throw test_error();
	    });
	bool ok = false;

	try {
		f.get();
	} catch (const test_error&) {
		ok = true;
	}
	assert(ok && "error should cascade into this body");
	return 0;
}
