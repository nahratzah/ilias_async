#include <thread>
#include <memory>

thread_local std::unique_ptr<int> i(new int);

int
main()
{
	*i = 0;
	std::thread t([]() { *i = 1; });
	t.join();
	return *i;
}
