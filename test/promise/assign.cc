#include <ilias/future.h>

int
main()
{
	ilias::cb_promise<int> p;
	ilias::cb_future<int> f = p.get_future();

	p.set_value(42);
	p = ilias::cb_promise<int>();

	assert(f.get() == 42);

	return 0;
}
