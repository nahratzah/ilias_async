#include <ilias/promise.h>

int
main()
{
	auto f = ilias::new_promise<int>(
	    [](ilias::promise<int> p) {
		p.set(42);
	    });

	assert(f.get() == 42);
	return 0;
}
