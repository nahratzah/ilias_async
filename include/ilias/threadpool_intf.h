/*
 * Copyright (c) 2013 Ariane van der Steldt <ariane@stack.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef ILIAS_THREADPOOL_INTF_H
#define ILIAS_THREADPOOL_INTF_H

#include <ilias/ll_list.h>
#include <ilias/refcnt.h>
#include <ilias/util.h>
#include <climits>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ilias {


class threadpool_client_intf;
class threadpool_service_intf;
class threadpool_service_lock;
class threadpool_client_lock;


namespace threadpool_intf_detail {


class ILIAS_ASYNC_EXPORT threadpool_intf_refcnt
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

	threadpool_intf_refcnt(const threadpool_intf_refcnt&) noexcept
	:	threadpool_intf_refcnt()
	{
		/* Empty body. */
	}

	threadpool_intf_refcnt(threadpool_intf_refcnt&&) = delete;

	threadpool_intf_refcnt&
	operator=(const threadpool_intf_refcnt&) noexcept
	{
		return *this;
	}

	virtual ~threadpool_intf_refcnt() noexcept;

	ILIAS_ASYNC_EXPORT friend void refcnt_acquire(
	    const threadpool_intf_refcnt&, std::uintptr_t) noexcept;
	ILIAS_ASYNC_EXPORT friend void refcnt_release(
	    const threadpool_intf_refcnt&, std::uintptr_t) noexcept;

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
	ILIAS_ASYNC_EXPORT static void acquire(const threadpool_client_intf&,
	    unsigned int = 1) noexcept;
	ILIAS_ASYNC_EXPORT static void release(const threadpool_client_intf&,
	    unsigned int = 1) noexcept;
};

/* Service acquisition/release. */
struct service_acqrel
{
	ILIAS_ASYNC_EXPORT static void acquire(const threadpool_service_intf&,
	    unsigned int = 1) noexcept;
	ILIAS_ASYNC_EXPORT static void release(const threadpool_service_intf&,
	    unsigned int = 1) noexcept;
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
		ti.m_service_locks.fetch_sub(1U, std::memory_order_release);
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
		ti.m_client_locks.fetch_sub(1U, std::memory_order_release);
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
class ILIAS_ASYNC_EXPORT threadpool_service_intf
:	public virtual threadpool_intf_detail::threadpool_intf_refcnt
{
friend struct threadpool_intf_detail::service_acqrel;
friend class threadpool_intf_detail::refcount;

public:
	virtual ~threadpool_service_intf() noexcept;

protected:
	/* Client supplied: perform a unit of work for client. */
	virtual bool do_work() noexcept = 0;
	/* Client supplied: test if client has work available. */
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
	virtual bool service_acquire(unsigned int) const noexcept = 0;
	virtual bool service_release(unsigned int) const noexcept = 0;
};

/*
 * Client inheritance interface.
 *
 * Provides methods that the client needs to invoke in order to use a service.
 */
class ILIAS_ASYNC_EXPORT threadpool_client_intf
:	public virtual threadpool_intf_detail::threadpool_intf_refcnt
{
friend struct threadpool_intf_detail::client_acqrel;
friend class threadpool_intf_detail::refcount;

public:
	static constexpr unsigned int WAKE_ALL = UINT_MAX;

	ILIAS_ASYNC_EXPORT virtual ~threadpool_client_intf() noexcept;

protected:
	/*
	 * Service supplied: notify service of N units of work being
	 * available.
	 */
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
	virtual bool client_acquire(unsigned int) const noexcept = 0;
	virtual bool client_release(unsigned int) const noexcept = 0;
};


template<typename T> using threadpool_client_ptr =
    refpointer<T, threadpool_intf_detail::client_acqrel>;
template<typename T> using threadpool_service_ptr =
    refpointer<T, threadpool_intf_detail::service_acqrel>;


namespace threadpool_intf_detail {


class ILIAS_ASYNC_EXPORT refcount
:	public virtual threadpool_client_intf,
	public virtual threadpool_service_intf
{
public:
	refcount() = default;
	refcount(const refcount&) : refcount() {}
	virtual ~refcount() noexcept;

private:
	mutable std::atomic<uintptr_t> m_service_refcnt{ 0U };
	mutable std::atomic<uintptr_t> m_client_refcnt{ 0U };

	bool _has_service() const noexcept override final;
	bool _has_client() const noexcept override final;

protected:
	bool service_acquire(unsigned int) const noexcept override final;
	bool service_release(unsigned int) const noexcept override final;
	bool client_acquire(unsigned int) const noexcept override final;
	bool client_release(unsigned int) const noexcept override final;
};


/* Glue a client and service implementation together. */
template<typename Client, typename Service>
class threadpool_combiner final
:	public Client,
	public Service,
	private threadpool_intf_detail::refcount
{
	static_assert(std::is_base_of<threadpool_client_intf, Client>::value,
	    "Client must inherit from threadpool_client, "
	    "in order to use its methods.");
	static_assert(std::is_base_of<threadpool_service_intf, Service>::value,
	    "Service must inherit from threadpool_service, "
	    "in order to use its methods.");

private:
	bool
	do_work() noexcept override final
	{
		return this->Client::do_work();
	}

	bool
	has_work() noexcept override final
	{
		return this->Client::has_work();
	}

	unsigned int
	wakeup(unsigned int n) noexcept override final
	{
		return this->Service::wakeup(n);
	}

	using threadpool_intf_detail::refcount::client_acquire;
	using threadpool_intf_detail::refcount::client_release;
	using threadpool_intf_detail::refcount::service_acquire;
	using threadpool_intf_detail::refcount::service_release;

public:
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


} /* namespace ilias::threadpool_intf_detail */


/*
 * Attach a client of a threadpool service to a provider of said service.
 *
 * Client:
 * - has member type: threadpool_client:
 *   public virtual threadpool_client_intf
 * - has member function: threadpool_client_arg()
 *   returning constructor arg for threadpool_client.
 * - has member function:
 *   attach(threadpool_client_ptr<threadpool_client_intf>)
 *
 * Service:
 * - has member type:
 *   threadpool_service: public virtual threadpool_service_intf
 * - has member function: threadpool_service_arg()
 *   returning constructor arg for threadpool_service.
 * - has member function:
 *   attach(threadpool_service_ptr<threadpool_service_intf>)
 */
template<typename Client, typename Service>
void
threadpool_attach(Client& client, Service& service)
{
	using client_intf = typename Client::threadpool_client;
	using service_intf = typename Service::threadpool_service;
	using impl_type = threadpool_intf_detail::threadpool_combiner<
	    client_intf, service_intf>;

	/* Implement combined type. */
	const auto impl = make_refpointer<impl_type>(
	    do_noexcept(std::bind(
	      &Client::threadpool_client_arg, std::ref(client))),
	    do_noexcept(std::bind(
	      &Service::threadpool_service_arg, std::ref(service)))
	    );

	/*
	 * Bind to client/service pointers.
	 *
	 * This step is important: if the binding fails, both the client
	 * and service must be informed of the failure (which is done
	 * automatically by the client/service pointers).
	 */
	threadpool_client_ptr<client_intf> c_ptr{ impl };
	threadpool_service_ptr<service_intf> s_ptr{ impl };

	/* Bind client/service pointers to argument client/service. */
	service.attach(std::move(s_ptr));
	client.attach(std::move(c_ptr));
}


/*
 * Service implementation, that will allow multiple clients to share a service.
 */
class tp_service_multiplexer final
{
private:
	struct data_all {};
	struct data_active {};

public:
	/*
	 * Implementation of threadpool service provider.
	 */
	class ILIAS_ASYNC_EXPORT threadpool_service
	:	public virtual threadpool_service_intf,
		public ll_list_hook<data_all>,
		public ll_list_hook<data_active>
	{
	friend class tp_service_multiplexer;

	private:
		enum class work_avail : unsigned char {
			NO,
			MAYBE,
			YES,
			DETACHED
		};

		tp_service_multiplexer& m_self;
		std::atomic<work_avail> m_work_avail{ work_avail::YES };

		ILIAS_ASYNC_LOCAL bool post_deactivate() noexcept;
		ILIAS_ASYNC_LOCAL void activate() noexcept;

	public:
		threadpool_service(tp_service_multiplexer& self)
		:	m_self(self)
		{
			/* Empty body. */
		}

		~threadpool_service() noexcept;

	private:
		/* Invoke work on the client. */
		bool invoke_work() noexcept;
		/* Test for work availability. */
		bool invoke_test() noexcept;

	public:
		/* Wakeup N threads. */
		unsigned int wakeup(unsigned int) noexcept;

	private:
		/* When client detaches, remove this from the set. */
		void on_client_detach() noexcept override;
	};

	/*
	 * Threadpool service provider interface.
	 */
	tp_service_multiplexer&
	threadpool_service_arg() noexcept
	{
		return *this;
	}

	/*
	 * Service provider attach functionality.
	 */
	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_service_ptr<threadpool_service>);

	/* Client for (simple) threadpool service. */
	class ILIAS_ASYNC_EXPORT threadpool_client
	:	public virtual threadpool_client_intf
	{
	friend class tp_service_multiplexer;

	private:
		tp_service_multiplexer& m_self;

	public:
		threadpool_client(tp_service_multiplexer& self)
		:	m_self(self)
		{
			/* Empty body. */
		}

		~threadpool_client() noexcept;

	protected:
		bool do_work() noexcept;
		bool has_work() noexcept;
	};

	tp_service_multiplexer&
	threadpool_client_arg() noexcept
	{
		return *this;
	}

	/*
	 * Attach multiplexed service. */
	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_client_ptr<threadpool_client>);


private:
	/* All clients of the service set. */
	using data_t = ll_smartptr_list<threadpool_service,
	    data_all,
	    threadpool_intf_detail::service_acqrel>;

	/* All active clients (i.e. the ones with work to do). */
	using active_t = ll_smartptr_list<threadpool_service,
	    data_active,
	    threadpool_intf_detail::service_acqrel>;

	/* All clients. */
	data_t m_data;
	/* All active clients. */
	active_t m_active;
	/* Client to service that is multiplexed. */
	threadpool_client_ptr<threadpool_client> m_impl;

public:
	ILIAS_ASYNC_EXPORT void clear() noexcept;

	tp_service_multiplexer() = default;
	ILIAS_ASYNC_EXPORT ~tp_service_multiplexer() noexcept;
};

/*
 * Client implementation, that will allow multiple services to be used
 * by a client.
 *
 * Attach to a client C using:
 *   tp_client_multiplexer tcm;
 *   threadpool_attach(C, tcm);
 */
class tp_client_multiplexer final
{
public:
	/*
	 * Multiplexed client implementation.
	 * Attach to services that will aid in running the attached client.
	 */
	class ILIAS_ASYNC_EXPORT threadpool_client
	:	public virtual threadpool_client_intf,
		public ll_list_hook<>
	{
	friend class tp_client_multiplexer;

	private:
		tp_client_multiplexer& m_client;

	public:
		threadpool_client(tp_client_multiplexer& c)
		:	m_client(c)
		{
			/* Empty body. */
		}

		~threadpool_client() noexcept;

	protected:
		bool do_work() noexcept;
		bool has_work() noexcept;
	};

	tp_client_multiplexer&
	threadpool_client_arg() noexcept
	{
		return *this;
	}

	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_client_ptr<threadpool_client>);


	/*
	 * Service provided to (simple) client.
	 */
	class ILIAS_ASYNC_EXPORT threadpool_service
	:	public virtual threadpool_service_intf
	{
	friend class tp_client_multiplexer;

	private:
		tp_client_multiplexer& m_self;

	public:
		threadpool_service(tp_client_multiplexer& self)
		:	m_self(self)
		{
			/* Empty body. */
		}

		~threadpool_service() noexcept;

	protected:
		unsigned int wakeup(unsigned int) noexcept;
	};

	tp_client_multiplexer&
	threadpool_service_arg() noexcept
	{
		return *this;
	}

	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_service_ptr<threadpool_service> p);

private:
	/* All services. */
	using data_t = ll_smartptr_list<threadpool_client,
	    void,
	    threadpool_intf_detail::client_acqrel>;

	/* All services. */
	data_t m_data;

	/* Client that is being multiplexed. */
	threadpool_service_ptr<threadpool_service> m_impl;

public:
	ILIAS_ASYNC_EXPORT void clear() noexcept;

	tp_client_multiplexer() = default;
	ILIAS_ASYNC_EXPORT ~tp_client_multiplexer() noexcept;
};


/*
 * Wrap a service in order to multiplex it.
 *
 * The actual service can be reached by operator* and operator->.
 */
template<typename Service>
class tp_service_mp_wrap
{
public:
	/* Behave as a pointer to the wrapped service. */
	using element_type = Service;
	using pointer = element_type*;
	using reference = element_type&;
	using const_pointer = const element_type*;
	using const_reference = const element_type&;

	/*
	 * Expose multiplexer interface
	 * as threadpool service implementation.
	 */
	using threadpool_service = tp_service_multiplexer::threadpool_service;

private:
	tp_service_multiplexer m_mp;
	element_type m_wrapped;

public:
	template<typename... Args>
	tp_service_mp_wrap(Args&&... args)
	:	m_wrapped{ std::forward<Args>(args)... }
	{
		threadpool_attach(this->m_mp, this->m_wrapped);
	}

	tp_service_mp_wrap(const tp_service_mp_wrap&) = delete;
	tp_service_mp_wrap& operator=(const tp_service_mp_wrap&) = delete;
	tp_service_mp_wrap(tp_service_mp_wrap&&) = delete;

	reference
	operator*() noexcept
	{
		return this->m_wrapped;
	}

	const_reference
	operator*() const noexcept
	{
		return this->m_wrapped;
	}

	pointer
	operator->() noexcept
	{
		return &this->m_wrapped;
	}

	const_pointer
	operator->() const noexcept
	{
		return &this->m_wrapped;
	}

	tp_service_multiplexer&
	threadpool_service_arg() noexcept
	{
		return this->m_mp.threadpool_service_arg();
	}

	void
	attach(threadpool_service_ptr<threadpool_service> p)
	{
		this->m_mp.attach(std::move(p));
	}
};

/*
 * Wrap a client in order to multiplex it.
 *
 * The actual client can be reached by operator* and operator->.
 */
template<typename Client>
class tp_client_mp_wrap
{
public:
	/* Behave as a pointer to the wrapped service. */
	using element_type = Client;
	using pointer = element_type*;
	using reference = element_type&;
	using const_pointer = const element_type*;
	using const_reference = const element_type&;

	/*
	 * Expose multiplexer interface
	 * as threadpool service implementation.
	 */
	using threadpool_client = tp_client_multiplexer::threadpool_client;

private:
	tp_client_multiplexer m_mp;
	element_type m_wrapped;

public:
	template<typename... Args>
	tp_client_mp_wrap(Args&&... args)
	:	m_wrapped{ std::forward<Args>(args)... }
	{
		threadpool_attach(this->m_wrapped, this->m_mp);
	}

	tp_client_mp_wrap(const tp_client_mp_wrap&) = delete;
	tp_client_mp_wrap& operator=(const tp_client_mp_wrap&) = delete;
	tp_client_mp_wrap(tp_client_mp_wrap&&) = delete;

	reference
	operator*() noexcept
	{
		return this->m_wrapped;
	}

	const_reference
	operator*() const noexcept
	{
		return this->m_wrapped;
	}

	pointer
	operator->() noexcept
	{
		return &this->m_wrapped;
	}

	const_pointer
	operator->() const noexcept
	{
		return &this->m_wrapped;
	}

	tp_client_multiplexer&
	threadpool_client_arg() noexcept
	{
		return this->m_mp.threadpool_service_arg();
	}

	void
	attach(threadpool_client_ptr<threadpool_client> p)
	{
		this->m_mp.attach(std::move(p));
	}
};


/*
 * A simple service that will allow aid invocations.
 */
class tp_aid_service
{
public:
	using callback_fn = unsigned int(unsigned int);

	class ILIAS_ASYNC_EXPORT threadpool_service
	:	public virtual threadpool_service_intf
	{
	friend class tp_aid_service;

	private:
		tp_aid_service& m_self;

	protected:
		unsigned int wakeup(unsigned int) noexcept;

	public:
		threadpool_service(tp_aid_service& self)
		:	m_self(self)
		{
			/* Empty body. */
		}

		~threadpool_service() noexcept;
	};

	tp_aid_service&
	threadpool_service_arg() noexcept
	{
		return *this;
	}

	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_service_ptr<threadpool_service>);

private:
	threadpool_service_ptr<threadpool_service> m_p;
	std::mutex m_wakeup_mtx;
	std::function<callback_fn> m_wakeup_cb;

public:
	ILIAS_ASYNC_EXPORT bool has_work() noexcept;
	ILIAS_ASYNC_EXPORT bool do_work() noexcept;

	ILIAS_ASYNC_EXPORT friend void callback(tp_aid_service,
	    std::function<callback_fn>) noexcept;

	tp_aid_service() = default;
	tp_aid_service(const tp_aid_service&) = delete;
	tp_aid_service& operator=(const tp_aid_service&) = delete;
	tp_aid_service(tp_aid_service&&) = delete;
	~tp_aid_service() = default;
};

extern template ILIAS_ASYNC_EXPORT
	void threadpool_attach<tp_client_multiplexer, tp_service_multiplexer>(
	    tp_client_multiplexer&, tp_service_multiplexer&);
extern template ILIAS_ASYNC_EXPORT
	void threadpool_attach<tp_client_multiplexer, tp_aid_service>(
	    tp_client_multiplexer&, tp_aid_service&);
extern template ILIAS_ASYNC_EXPORT
	void threadpool_attach<tp_service_multiplexer, tp_aid_service>(
	    tp_service_multiplexer&, tp_aid_service&);


} /* namespace ilias */

#endif /* ILIAS_THREADPOOL_INTF_H */
