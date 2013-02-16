#include <memory>
#include <atomic>


int
main()
{
	std::shared_ptr<void> ptr;
	std::atomic_store(&ptr, std::shared_ptr<void>(nullptr));
	return 0;
}
