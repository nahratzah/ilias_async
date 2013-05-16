#include <ilias/threadpool_intf.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
#include <cassert>


class client
{
public:
	enum state {
		FIRST,
		WAKEUP,
		SECOND,
		SECOND_DONE
	};

	state m_state{ FIRST };
	std::mutex m_lck;
	std::condition_variable m_cnd;
	std::mutex wakeup_block;
	std::condition_variable wakeup_cnd;
	bool wakeup_signaled{ false };

	client()
	{
		/* Empty body. */
	}

	class threadpool_client
	:	public virtual ilias::threadpool_client_intf
	{
	public:
		client* m_client;

	private:
		std::thread m_thr;

		void
		thread_fn() noexcept
		{
			std::lock_guard<std::mutex> block {
				this->m_client->wakeup_block
			};
			std::unique_lock<std::mutex> guard {
				this->m_client->m_lck
			};
			auto& m_state = this->m_client->m_state;
			auto& cnd = this->m_client->m_cnd;

			while (m_state != WAKEUP)
				cnd.wait(guard);

			m_state = SECOND;
			this->wakeup(1);

			this->m_client->wakeup_signaled = true;
			this->m_client->wakeup_cnd.notify_all();
		}

	public:
		threadpool_client(client* c)
		:	m_client(c),
			m_thr{ &threadpool_client::thread_fn, this }
		{
			/* Empty body. */
		}

		~threadpool_client() noexcept
		{
			this->m_thr.join();
		}

	protected:
		bool
		do_work() noexcept
		{
			if (!this->m_client)
				return false;

			bool rv;
			switch (this->m_client->m_state) {
			case FIRST:
				rv = true;
				this->m_client->m_state = WAKEUP;
				break;
			case SECOND:
				rv = true;
				this->m_client->m_state = SECOND_DONE;
				break;
			case WAKEUP:
				{
					std::cerr << "Notifying thead to send wakeup." << std::endl;
					std::lock_guard<std::mutex> guard {
						this->m_client->m_lck
					};
					this->m_client->m_cnd.notify_all();
				}
				rv = false;
				break;
			case SECOND_DONE:
				rv = false;
				this->m_client->ptr = nullptr;
			}
			return rv;
		}

		bool
		has_work() noexcept
		{
			switch (this->m_client->m_state) {
			default:
				break;
			case FIRST:
			case SECOND:
				return true;
			}
			return false;
		}

		void
		on_service_detach() noexcept
		{
			if (this->m_client)
				this->m_client->ptr = nullptr;
		}
	};

	ilias::threadpool_client_ptr<threadpool_client> ptr;

	~client() noexcept
	{
		if (this->ptr)
			this->ptr->m_client = nullptr;
	}

	void
	attach(ilias::threadpool_client_ptr<threadpool_client> ptr)
	{
		this->ptr = std::move(ptr);
	}

	client*
	threadpool_client_arg()
	{
		return this;
	}
};


bool
race_test() noexcept
{
	client c;
	ilias::tp_aid_service s;
	bool race;

	threadpool_attach(c, s);

	while (s.has_work())
		while (s.do_work());

	{
		/* Wait until wakeup fires... */
		std::cerr << "Waiting for wakeup to complete..." << std::endl;
		std::unique_lock<std::mutex> block {
			c.wakeup_block
		};
		race = c.wakeup_signaled;	/* Race detection. */
		while (!c.wakeup_signaled)
			c.wakeup_cnd.wait(block);
	}

	while (s.has_work())
		while (s.do_work());

	return race;
}


int
main()
{
	unsigned int race = 0;
	const unsigned int N = 1000;
	unsigned int i;
	for (i = 0; i < N; ++i) {
		std::cout << "--- race attempt " << i << std::endl;
		if (race_test()) {
			std::cout << "Race detected at " << i << std::endl;
			++race;
		}
	}

	if (race == 0) {
		std::cout << "Failed to trigger race." << std::endl;
		return 1;
	}

	std::cout << "Race triggered in " << race << " of " << N << " cases."
	    << std::endl;
	return 0;
}
