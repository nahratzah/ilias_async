#include <ilias/promise.h>

int
main()
{
	future<int> f = ilias::new_promise<int>();
	bool ok = false;

	try {
		f.get();
	} catch (const ilias::broken_promise&) {
		ok = true;
	}

	assert(ok && "promise should have been broken");
	return 0;
}
