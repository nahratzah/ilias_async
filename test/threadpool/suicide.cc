/*
 * Don't use this code as a proper example of how to code!
 *
 * This test case verifies that a threadpool destroyed from within
 * will succesfully destroy.
 */
#include <ilias/threadpool.h>
#include <ilias/threadpool_intf.h>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <thread>
#include <chrono>


std::mutex lck;
std::condition_variable cv;
ilias::threadpool* tp{ nullptr };
bool detached{ false };

class simple_client
{
public:
	class threadpool_client
	:	public virtual ilias::threadpool_client_intf
	{
	public:
		threadpool_client(std::nullptr_t)
		{
			/* Empty body. */
		}

		~threadpool_client() = default;

	protected:
		bool
		has_work() noexcept
		{
			ilias::threadpool_client_lock c_lck{ *this };
			if (!this->has_client())
				return false;

			std::lock_guard<std::mutex> guard{ lck };
			return tp;
		}

		bool
		do_work() noexcept
		{
			using std::swap;

			ilias::threadpool* p = nullptr;
			{
				ilias::threadpool_client_lock c_lck{ *this };
				if (!this->has_client())
					return false;

				std::lock_guard<std::mutex> guard{ lck };
				swap(p, tp);
			}
			if (p)
				delete p;
			return p;
		}

		void
		on_service_detach() noexcept override
		{
			std::lock_guard<std::mutex> guard{ lck };
			assert(!detached);
			detached = true;
			cv.notify_all();
		}
	};

	std::nullptr_t
	threadpool_client_arg() const noexcept
	{
		return nullptr;
	}

	void
	attach(ilias::threadpool_client_ptr<threadpool_client> p)
	{
		this->p = std::move(p);
	}

private:
	ilias::threadpool_client_ptr<threadpool_client> p;
};


int
main()
{
	std::unique_lock<std::mutex> guard{ lck };
	tp = new ilias::threadpool();
	simple_client sc;

	threadpool_attach(sc, *tp);

	while (!detached)
		cv.wait(guard);

	assert(!tp);

	/*
	 * Sleep for a short while,
	 * so the detached thread can succesfully complete
	 * prior to the program exiting (once main completes,
	 * failures in the worker thread can no longer affect
	 * the return value of the program).
	 */
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	return 0;
}
