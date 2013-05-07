#include <ilias/promise.h>
#include <ilias/threadpool_intf.h>
#include <stdexcept>
#include <cassert>
#include <iostream>


class client
{
public:
	ilias::future<int> result;

	class threadpool_client
	:	public virtual ilias::threadpool_client_intf
	{
	public:
		ilias::promise<int> result;

	private:
		client* m_client;

	public:
		threadpool_client(std::tuple<ilias::promise<int>, client*> arg)
		:	result(std::get<0>(arg)),
			m_client(std::get<1>(arg))
		{
			/* Empty body. */
		}

		~threadpool_client() noexcept
		{
			std::cout << "client: destructor" << std::endl;
		}

	protected:
		bool
		do_work()
		{
			if (result.ready()) {
				std::cout << "Client work already completed."
				    << std::endl;
				return false;
			}
			result.set(42);
			std::cout << "Client: assigned 42 as result of work."
			    << std::endl;
			return true;
		}

		bool
		has_work()
		{
			bool rv = !result.ready();
			std::cout << "client::has_work() -> " << rv
			    << std::endl;
			return rv;
		}

		void
		on_service_detach() noexcept
		{
			std::cout << "client: service detached."
			    << std::endl;
			if (!m_client)
				return;

			m_client->ptr = nullptr;
			m_client = nullptr;
		}
	};

	ilias::threadpool_client_ptr<threadpool_client> ptr;

	void
	attach(ilias::threadpool_client_ptr<threadpool_client> ptr)
	{
		this->result = ptr->result;
		this->ptr = std::move(ptr);
	}

	std::tuple<ilias::promise<int>, client*>
	threadpool_client_arg()
	{
		return std::make_tuple(ilias::promise<int>::create(), this);
	}
};

class service
{
public:
	class threadpool_service
	:	public virtual ilias::threadpool_service_intf
	{
	private:
		service* m_service;

	public:
		threadpool_service(service* s)
		:	m_service(s)
		{
			/* Empty body. */
		}

		~threadpool_service() noexcept
		{
			std::cout << "service: destructor" << std::endl;
		}

	protected:
		unsigned int
		wakeup(unsigned int n) noexcept
		{
			return (this->m_service ?
			    this->m_service->wakeup(n): 0);
		}

		void
		on_client_detach() noexcept
		{
			using std::swap;

			std::cout << "service: client detached."
			    << std::endl;

			if (this->m_service) {
				decltype(this->m_service->ptr) p;
				swap(this->m_service->ptr, p);
				this->m_service = nullptr;
			}
		}

	public:
		using ilias::threadpool_service_intf::has_work;
		using ilias::threadpool_service_intf::do_work;
	};

	ilias::threadpool_service_ptr<threadpool_service> ptr;

	void
	attach(ilias::threadpool_service_ptr<threadpool_service> ptr)
	{
		this->ptr = ptr;
	}

	service*
	threadpool_service_arg() noexcept
	{
		return this;
	}

	bool m_work_avail{ false };

	unsigned int
	wakeup(unsigned int n) noexcept
	{
		std::cout << "service::wakeup(" << n << ")" << std::endl;
		if (n == 0)
			return 0;
		m_work_avail = true;
		return 1;
	}

	bool
	has_work() const noexcept
	{
		return this->m_work_avail ||
		    (this->ptr ? this->ptr->has_work() : false);
	}

	bool
	do_work() const noexcept
	{
		return (this->ptr ? this->ptr->do_work() : false);
	}
};


int
main()
{
	service s;
	client c;

	assert(!s.has_work());
	ilias::threadpool_attach(c, s);
	assert(s.has_work());

	while (s.has_work())
		while (s.do_work());

	assert(c.result.ready());
	assert(c.result.get());

	return 0;
}
