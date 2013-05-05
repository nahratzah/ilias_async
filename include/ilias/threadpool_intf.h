#include <ilias/refcnt.h>
#include <ilias/ll.h>
#include <utility>
#include <climits>

namespace ilias {


class threadpool_client_intf;
class threadpool_service_intf;
class threadpool_service_lock;
class threadpool_client_lock;


namespace threadpool_intf_detail {


class threadpool_intf_refcnt
{
friend class ::ilias::threadpool_service_lock;
friend class ::ilias::threadpool_client_lock;

private:
	mutable std::atomic<uintptr_t> m_refcnt{ 0U };
	mutable std::atomic<unsigned int> m_client_locks{ 0U };
	mutable std::atomic<unsigned int> m_service_locks{ 0U };

	/* Implementation provided by combiner. */
	virtual bool _has_service() const noexcept = 0;
	virtual bool _has_client() const noexcept = 0;

protected:
	/* Spin-wait until all client locks are released. */
	void
	client_lock_wait() const noexcept
	{
		while (this->m_client_locks.load(std::memory_order_acquire) !=
		    0U);
	}

	/* Spin-wait until all service locks are released. */
	void
	service_lock_wait() const noexcept
	{
		while (this->m_client_locks.load(std::memory_order_acquire) !=
		    0U);
	}

public:
	threadpool_intf_refcnt() = default;
	threadpool_intf_refcnt(const threadpool_intf_refcnt&) = delete;
	threadpool_intf_refcnt(threadpool_intf_refcnt&&) = delete;

	threadpool_intf_refcnt&
	operator=(const threadpool_intf_refcnt&) noexcept
	{
		return *this;
	}

	ILIAS_ASYNC_EXPORT virtual ~threadpool_intf_refcnt() noexcept;

	ILIAS_ASYNC_EXPORT friend void acquire(const threadpool_intf_refcnt&)
	    noexcept;
	ILIAS_ASYNC_EXPORT friend void release(const threadpool_intf_refcnt&)
	    noexcept;

	/* Test if the service is attached. */
	bool
	has_service() const noexcept
	{
		return this->_has_service();
	}

	/* Test if the client is attached. */
	bool
	has_client() const noexcept
	{
		return this->_has_client();
	}
};

/* Client acquisition/release. */
struct client_acqrel
{
	ILIAS_ASYNC_EXPORT void acquire(const threadpool_client_intf&)
	    const noexcept;
	ILIAS_ASYNC_EXPORT void release(const threadpool_client_intf&)
	    const noexcept;
};

/* Service acquisition/release. */
struct service_acqrel
{
	ILIAS_ASYNC_EXPORT void acquire(const threadpool_service_intf&)
	    const noexcept;
	ILIAS_ASYNC_EXPORT void release(const threadpool_service_intf&)
	    const noexcept;
};

class refcount;


} /* namespace ilias::threadpool_intf_detail */


/*
 * Prevent service from releasing its pointer completely.
 * As long as each service-intf is properly deleted prior to deleting
 * the actual service,
 * this will guarantee that the service stays resident if has_service() returns
 * true.
 */
class threadpool_service_lock
{
private:
	const threadpool_intf_detail::threadpool_intf_refcnt& ti;

public:
	threadpool_service_lock(
	    const threadpool_intf_detail::threadpool_intf_refcnt& ti) noexcept
	:	ti(ti)
	{
		ti.m_service_locks.fetch_add(1U, std::memory_order_acquire);
	}

	~threadpool_service_lock() noexcept
	{
		ti.m_service_locks.fetch_sub(1U, std::memory_order_acquire);
	}

	threadpool_service_lock(const threadpool_service_lock&) = delete;
	threadpool_service_lock& operator=(const threadpool_service_lock&) =
	    delete;
	threadpool_service_lock(threadpool_service_lock&&) = delete;
};

/*
 * Prevent client from releasing its pointer completely.
 * As long as each client-intf is properly deleted prior to deleting
 * the actual client,
 * this will guarantee that the client stays resident if has_client() returns
 * true.
 */
class threadpool_client_lock
{
private:
	const threadpool_intf_detail::threadpool_intf_refcnt& ti;

public:
	threadpool_client_lock(
	    const threadpool_intf_detail::threadpool_intf_refcnt& ti) noexcept
	:	ti(ti)
	{
		ti.m_client_locks.fetch_add(1U, std::memory_order_acquire);
	}

	~threadpool_client_lock() noexcept
	{
		ti.m_client_locks.fetch_sub(1U, std::memory_order_acquire);
	}

	threadpool_client_lock(const threadpool_client_lock&) = delete;
	threadpool_client_lock& operator=(const threadpool_client_lock&) =
	    delete;
	threadpool_client_lock(threadpool_client_lock&&) = delete;
};


/*
 * Service inheritance interface.
 *
 * Provides methods that the service needs to invoke in order to use a client.
 *
 * Service must provide: unsigned int wakeup(unsigned int).
 */
class threadpool_service_intf
:	public threadpool_intf_detail::threadpool_intf_refcnt
{
friend struct threadpool_intf_detail::service_acqrel;
friend class threadpool_intf_detail::refcount;

public:
	ILIAS_ASYNC_EXPORT virtual ~threadpool_service_intf() noexcept;

	/* Perform a unit of work for client. */
	virtual bool do_work() noexcept = 0;
	/* Test if client has work available. */
	virtual bool has_work() noexcept = 0;

private:
	/*
	 * Invoked when client goes away,
	 * override to respond to this event.
	 */
	ILIAS_ASYNC_EXPORT virtual void on_client_detach() noexcept;

	/*
	 * Implementation provided by combiner.
	 */
	virtual bool service_acquire() const noexcept = 0;
	virtual bool service_release() const noexcept = 0;
};

/*
 * Client inheritance interface.
 *
 * Provides methods that the client needs to invoke in order to use a service.
 */
class threadpool_client_intf
:	public threadpool_intf_detail::threadpool_intf_refcnt
{
friend struct threadpool_intf_detail::client_acqrel;
friend class threadpool_intf_detail::refcount;

public:
	static constexpr unsigned int WAKE_ALL = UINT_MAX;

	ILIAS_ASYNC_EXPORT virtual ~threadpool_client_intf() noexcept;

	/* Notify service of N units of work being available. */
	virtual unsigned int wakeup(unsigned int = 1) noexcept = 0;

private:
	/*
	 * Invoked when service goes away,
	 * override to respond to this event.
	 */
	ILIAS_ASYNC_EXPORT virtual void on_service_detach() noexcept;

	/*
	 * Implementation provided by combiner.
	 */
	virtual bool client_acquire() const noexcept = 0;
	virtual bool client_release() const noexcept = 0;
};


template<typename T> using threadpool_client_ptr =
    refpointer<T, threadpool_intf_detail::client_acqrel>;
template<typename T> using threadpool_service_ptr =
    refpointer<T, threadpool_intf_detail::service_acqrel>;


namespace threadpool_intf_detail {


class refcount
:	public virtual threadpool_client_intf,
	public virtual threadpool_service_intf
{
public:
	refcount(const refcount&) {}

private:
	mutable std::atomic<uintptr_t> m_service_refcnt{ 0U };
	mutable std::atomic<uintptr_t> m_client_refcnt{ 0U };

	ILIAS_ASYNC_EXPORT bool _has_service() const noexcept override final;
	ILIAS_ASYNC_EXPORT bool _has_client() const noexcept override final;

protected:
	ILIAS_ASYNC_EXPORT bool service_acquire()
	    const noexcept override final;
	ILIAS_ASYNC_EXPORT bool service_release()
	    const noexcept override final;
	ILIAS_ASYNC_EXPORT bool client_acquire()
	    const noexcept override final;
	ILIAS_ASYNC_EXPORT bool client_release()
	    const noexcept override final;
};


} /* namespace ilias::threadpool_intf_detail */


/* Glue a client and service implementation together. */
template<typename Client, typename Service>
class threadpool_combiner
:	public Client,
	public Service,
	private threadpool_intf_detail::refcount
{
	static_assert(std::is_base_of<Client, threadpool_client_intf>::value,
	    "Client must inherit from threadpool_client, "
	    "in order to use its methods.");
	static_assert(std::is_base_of<Service, threadpool_service_intf>::value,
	    "Service must inherit from threadpool_service, "
	    "in order to use its methods.");

	using Client::do_work;
	using Client::has_work;
	using Service::wakeup;
	using threadpool_intf_detail::refcount::client_acquire;
	using threadpool_intf_detail::refcount::client_release;
	using threadpool_intf_detail::refcount::service_acquire;
	using threadpool_intf_detail::refcount::service_release;

	threadpool_combiner() = delete;
	threadpool_combiner(const threadpool_combiner&) = delete;
	threadpool_combiner(threadpool_combiner&&) = delete;
	threadpool_combiner& operator=(const threadpool_combiner&) = delete;

	template<typename ClientArg, typename ServiceArg>
	threadpool_combiner(ClientArg&& client_arg, ServiceArg&& service_arg)
	:	Client(std::forward<ClientArg>(client_arg)),
		Service(std::forward<ServiceArg>(service_arg))
	{
		/* Empty body. */
	}

	~threadpool_combiner() noexcept = default;
};


class tp_service_set
{
private:
	struct data_all {};
	struct data_active {};

public:
	class threadpool_service
	:	public virtual threadpool_service_intf,
		public ll_base_hook<data_all>,
		public ll_base_hook<data_active>
	{
	friend class tp_service_set;

	private:
		enum class work_avail : unsigned char {
			NO,
			MAYBE,
			YES,
			DETACHED
		};

		tp_service_set& m_self;
		std::atomic<work_avail> m_work_avail{ work_avail::YES };

		bool post_deactivate() noexcept;
		void activate() noexcept;

	public:
		threadpool_service(tp_service_set& self)
		:	m_self(self)
		{
			/* Empty body. */
		}

	private:
		/* Invoke work on the client. */
		ILIAS_ASYNC_EXPORT bool invoke_work() noexcept;
		/* Test for work availability. */
		ILIAS_ASYNC_EXPORT bool invoke_test() noexcept;

	public:
		/* Wakeup N threads. */
		ILIAS_ASYNC_EXPORT unsigned int wakeup(unsigned int) noexcept;

	private:
		/* When client detaches, remove this from the set. */
		void on_client_detach() noexcept override;
	};


	class listener
	:	public ll_base_hook<>
	{
	private:
		tp_service_set& m_self;

	public:
		listener(tp_service_set& self)
		:	m_self(self)
		{
			/* Empty body. */
		}

		void
		run()
		{
			this->m_self.do_work();
		}

		virtual bool wakeup() const noexcept;
	};


private:
	struct tps_acquire
	{
		threadpool_service_ptr<threadpool_service>
		operator()(threadpool_service* p)
		    const noexcept
		{
			return threadpool_service_ptr<threadpool_service>(p,
			    false);
		}
	};

	struct tps_release
	{
		threadpool_service*
		operator()(threadpool_service_ptr<threadpool_service> p)
		    const noexcept
		{
			return p.release();
		}
	};

	using data_t = ll_smartptr_list<
	    threadpool_service_ptr<threadpool_service>,
	    ll_base<threadpool_service, data_all>,
	    tps_acquire, tps_release>;

	using active_t = ll_list<ll_base<threadpool_service, data_active>>;

	/* All clients. */
	data_t m_data;
	/* All active clients. */
	active_t m_active;

public:
	ILIAS_ASYNC_EXPORT bool do_work() noexcept;
	ILIAS_ASYNC_EXPORT bool has_work() noexcept;
	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_service_ptr<threadpool_service>);

	unsigned int
	wakeup(unsigned int)
	{
		return 0; /* XXX implement */
	}
};


}
