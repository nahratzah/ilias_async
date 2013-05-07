#include <ilias/threadpool_intf.h>
#include <utility>


class mock_client
{
public:
	class threadpool_client
	:	public virtual ilias::threadpool_client_intf
	{
	public:
		mock_client* m_self;

		threadpool_client(mock_client* self) noexcept
		:	m_self(self)
		{
			/* Empty body. */
		}

		bool
		has_work() noexcept
		{
			return false;
		}

		bool
		do_work() noexcept
		{
			return false;
		}

		void
		on_service_detach() noexcept
		{
			return;
		}
	};

	ilias::threadpool_client_ptr<threadpool_client> m_client;

	void
	attach(ilias::threadpool_client_ptr<threadpool_client> client)
	{
		this->m_client = std::move(client);
	}

	mock_client*
	threadpool_client_arg() noexcept
	{
		return this;
	}
};

class mock_service
{
public:
	class threadpool_service
	:	public virtual ilias::threadpool_service_intf
	{
	public:
		mock_service* m_self;

		threadpool_service(mock_service* self) noexcept
		:	m_self(self)
		{
			/* Empty body. */
		}

		unsigned int
		wakeup(unsigned int) noexcept
		{
			return 0;
		}

		void
		on_client_detach() noexcept
		{
			return;
		}
	};

	ilias::threadpool_service_ptr<threadpool_service> m_service;

	void
	attach(ilias::threadpool_service_ptr<threadpool_service> service)
	{
		this->m_service = std::move(service);
	}

	mock_service*
	threadpool_service_arg() noexcept
	{
		return this;
	}
};
