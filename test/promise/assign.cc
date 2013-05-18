#include <ilias/promise.h>

int
main()
{
	ilias::promise<int> p = ilias::new_promise<int>();
	ilias::future<int> f = p;

	assert(!f.ready());
	p.set(42);
	p = ilias::new_promise<int>();

	assert(f.ready());
	assert(f.get() == 42);

	return 0;
}
