#include <ilias/future.h>

int
main()
{
	ilias::cb_promise<int> p;
	ilias::cb_future<int> f = p.get_future();

	assert(!f.ready());
	p.set_value(42);
	p = ilias::cb_promise<int>();

	assert(f.ready());
	assert(f.get() == 42);

	return 0;
}
