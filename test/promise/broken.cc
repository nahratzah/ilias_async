#include <ilias/future.h>

int
main()
{
	ilias::cb_future<int> f = ilias::cb_promise<int>().get_future();
	bool ok = false;

	try {
		f.get();
	} catch (const ilias::future_error& e) {
		ok = (e.code() == ilias::future_errc::broken_promise);
	}

	assert(ok && "promise should have been broken");
	return 0;
}
