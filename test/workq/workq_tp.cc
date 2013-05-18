#include <ilias/workq.h>
#include <ilias/threadpool.h>
#include <atomic>
#include <thread>

const unsigned int COUNTER = 1000;

int
main()
{
	std::atomic<unsigned int> counter{ 0U };
	ilias::threadpool tp;
	{
		auto wqs = ilias::new_workq_service();

		for (unsigned int i = 0; i < COUNTER; ++i)
			wqs->new_workq()->once([&counter]() {
				counter.fetch_add(1U);
			    });

		threadpool_attach(*wqs, tp);
	}

	while (counter != COUNTER)
		std::this_thread::yield();

	assert(counter == COUNTER);
	return 0;
}
