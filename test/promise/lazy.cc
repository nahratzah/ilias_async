#include <ilias/future.h>

int
main()
{
	volatile bool done = false;
	auto f = ilias::async_lazy(
	    [&done]() -> int {
		done = true;
		return 42;
	    });

	assert(done == false);
	assert(f.get() == 42);
	assert(done == true);
	return 0;
}
