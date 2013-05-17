/*
 * Copyright (c) 2012 - 2013 Ariane van der Steldt <ariane@stack.nl>
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
#include <ilias/threadpool.h>
#if !HAS_TLS
#include "tls_fallback.h"
#endif
#include <ilias/ll.h>
#include <ilias/refcnt.h>
#include <ilias/workq.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ilias {


struct idle_tag {};
struct dead_tag {};

enum class thread_state : unsigned char
{
	BUSY,
	SLEEP_TEST,
	SLEEP,
	DYING,
	DEAD
};

class threadpool::impl
{
friend struct threadpool::impl_deleter;	/* Initiates destruction. */

public:
	static const unsigned int COLLECT_INTERVAL = 0x10000;

private:
	/* Worker thread. */
	class worker;

	/* TLS data: store if the current thread is in a workpool. */
	struct tp_tls_data
	{
		/* Threadpool owning current thread. */
		impl* tp;
		/* Worker owning current thread. */
		worker* w;
		/* If set, worker thread must kill threadpool on exit. */
		bool collect;
	};

	static tp_tls_data& get_tls() noexcept;

	/* List of idle threads. */
	using idle_type = ll_list<ll_base<worker, idle_tag>>;
	/* List of threads that are dead (and need to be collected). */
	using dead_type = ll_list<ll_base<worker, dead_tag>>;

	/* Number of threads in pool. */
	std::atomic<unsigned int> n_threads{ 0 };
	/* Number of threads exceeding pool size. */
	std::atomic<unsigned int> n_oversize{ 0 };

	/* Idle threads. */
	idle_type m_idle;
	/* Dead threads. */
	dead_type m_dead;

	/* Counter for active threads. */
	unsigned int n_active{ 0U };
	/* Mutex protecting active counter. */
	std::mutex m_active_mtx;
	/* Condition notifying n_active decrease. */
	std::condition_variable m_active_cnd;

	/* Service pointer. */
	threadpool_service_ptr<threadpool_service> m_serv;


	/* Test if there is work available. */
	bool
	has_work() const noexcept
	{
		auto serv = atomic_load(&this->m_serv);
		return (serv && serv->has_work());
	}

	/* Perform a unit of work. */
	bool
	do_work() const noexcept
	{
		auto serv = atomic_load(&this->m_serv);
		return (serv && serv->do_work());
	}

	/* Collect at most count dead worker threads. */
	unsigned int collect(unsigned int = UINT_MAX) noexcept;

	/*
	 * Create a worker thread.
	 */
	void create_worker();


	/*
	 * Reduce oversize by at most limit.
	 *
	 * Returns the reduce count.
	 */
	unsigned int
	reduce_oversize(unsigned int limit) noexcept
	{
		auto oversize = limit;
		while (oversize > 0U) {
			const auto reduce = std::min(oversize, limit);
			if (this->n_oversize.compare_exchange_weak(oversize,
			    oversize - reduce,
			    std::memory_order_relaxed,
			    std::memory_order_relaxed)) {
				return reduce;
			}
		}
		return 0U;
	}

	/*
	 * Increase oversize.
	 *
	 * Wakeup all threads after increment,
	 * so any idle threads will also be able to die.
	 */
	void
	increase_oversize(unsigned int add) noexcept
	{
		this->n_oversize.fetch_add(add, std::memory_order_acquire);
		this->wakeup(add);
	}

public:
	impl() = default;
	impl(const impl&) = delete;
	impl& operator=(const impl&) = delete;
	impl(impl&&) = delete;

private:
	/*
	 * Destructor.
	 *
	 * Set number of threads to zero (in case the constructor errored out).
	 * Wait until all threads have died, then collect them.
	 */
	~impl() noexcept
	{
		/* Wait until all active threads have terminated. */
		std::unique_lock<std::mutex> guard{ this->m_active_mtx };
		while (this->n_active > 0) {
			this->m_active_cnd.wait(guard);

			/*
			 * Might as well collect all dead threads
			 * while we wait.
			 */
			do_noexcept([&]() {
				guard.unlock();
				this->collect();
				guard.lock();
			    });
		}
		guard.unlock();
		this->collect();
	}

public:
	/* Change number of worker threads. */
	void set_nthreads(unsigned int n);

	/*
	 * Wakeup up to n idle worker threads.
	 * Returns the number of threads that were woken up by this invocation.
	 */
	unsigned int wakeup(unsigned int) noexcept;

	/* Read number of worker threads. */
	unsigned int
	get_nthreads() const noexcept
	{
		return this->n_threads.load(std::memory_order_acquire);
	}

	/* Attach service to threadpool. */
	void
	attach(threadpool_service_ptr<threadpool_service> p)
	{
		if (!p) {
			throw std::invalid_argument("threadpool: "
			    "cannot attach null service");
		}

		threadpool_service_ptr<threadpool_service> expect{ nullptr };
		if (!atomic_compare_exchange_strong(&this->m_serv,
		    &expect, std::move(p))) {
			throw std::runtime_error("threadpool: "
			    "cannot attach multiple services");
		}

		this->wakeup(UINT_MAX);
	}

	bool
	curthread_is_threadpool() const noexcept
	{
		return (get_tls().tp == this);
	}
};


class threadpool::impl::worker
:	public ll_base_hook<idle_tag>,
	public ll_base_hook<dead_tag>
{
private:
	std::atomic<thread_state> m_state{ thread_state::BUSY };
	std::mutex m_sleep_mtx;
	std::condition_variable m_sleep_cnd;
	impl& tp;
	std::thread m_thread;

public:
	/* Initialization mutex, protects worker from starting too early. */
	std::mutex m_init_mtx;

	/*
	 * Inform worker that it is to wakeup and process work.
	 * Returns true if the thread transitioned from sleeping to busy.
	 */
	bool
	wakeup() noexcept
	{
		if (transition(thread_state::SLEEP_TEST, thread_state::BUSY,
		      std::memory_order_acquire, std::memory_order_relaxed) ||
		    transition(thread_state::SLEEP, thread_state::BUSY,
		      std::memory_order_acquire, std::memory_order_relaxed)) {
			std::lock_guard<std::mutex> guard{
				this->m_sleep_mtx
			};
			this->m_sleep_cnd.notify_one();
			return true;
		}
		return false;
	}

	/* Inform worker that it is to die. */
	bool
	kill() noexcept
	{
		switch (this->m_state.exchange(thread_state::DYING,
		    std::memory_order_acquire)) {
		case thread_state::SLEEP:
		case thread_state::SLEEP_TEST:
			{
				std::lock_guard<std::mutex> guard{
					this->m_sleep_mtx
				};
				this->m_sleep_cnd.notify_one();
			}
			/* FALLTHROUGH */
		default:
			return true;
		case thread_state::DYING:
			/* Already marked for death. */
			return false;
		case thread_state::DEAD:
			assert(0);
			for (;;);	/* Undefined behaviour: spin eternally. */
			break;
		}
	}

private:
	/*
	 * Compare exchange strong,
	 * but we don't care about which state was present on failure.
	 */
	bool
	transition(thread_state expect, thread_state set,
	    std::memory_order success = std::memory_order_seq_cst,
	    std::memory_order failure = std::memory_order_seq_cst) noexcept
	{
		return this->m_state.compare_exchange_strong(expect, set,
		    success, failure);
	}

	/*
	 * Do the sleep operation.
	 *
	 * The worker thread first transitions to SLEEP_TEST and
	 * adds itself to the idle set.
	 * Then the test is performed,
	 * which validates there is indeed no work to be done.
	 * If there is no work, the sleep mutex is acquire
	 * (to prevent missed wakeups) and the worker transitions to sleeping.
	 */
	void do_sleep() noexcept;

	/*
	 * Test if this thread is to die.
	 */
	bool must_die() noexcept;

public:
	worker(threadpool::impl& tp)
	:	tp(tp)
	{
		/* Empty body. */
	}

	/* Assign thread to this worker. */
	void
	assign_thread(std::thread&& t) noexcept
	{
		this->m_thread = std::move(t);
		++this->tp.n_active;
	}

private:
	/*
	 * Worker thread function.
	 *
	 * Returns true if the worker thread is to destroy the threadpool.
	 */
	bool _run() noexcept;

public:
	/* Worker thread thread-fun. */
	static void
	run(worker* w) noexcept
	{
		if (w->_run()) {
			delete &w->tp;
			delete w;
		}
	}

	void
	join() noexcept
	{
		this->m_thread.join();
	}
};


threadpool::impl::tp_tls_data&
threadpool::impl::get_tls() noexcept
{
#if HAS_TLS
	static THREAD_LOCAL tp_tls_data impl;
	return impl;
#else
	static tls<tp_tls_data> impl;
	return *impl;
#endif
}

unsigned int
threadpool::impl::wakeup(unsigned int n) noexcept
{
	unsigned int c = 0;
	while (c < n) {
		auto i = this->m_idle.pop_front();
		if (!i)
			break;
		if (i->wakeup())
			++c;
	}
	return c;
}

unsigned int
threadpool::impl::collect(unsigned int count) noexcept
{
	unsigned int rv = 0;
	while (rv < count) {
		auto d = this->m_dead.pop_front();
		if (!d)
			break;
		++rv;
		d->join();
		delete d;
	}
	return rv;
}

void
threadpool::impl::create_worker()
{
	/* Create worker structure. */
	std::unique_ptr<worker> w{ new worker{ *this } };

	/*
	 * Start thread for the worker.
	 * Block thread until the initialization (assigning
	 * the thread variable) is complete.
	 * This prevents the thread from being able to call delete until we are
	 * finished with the worker structure and enables the worker to verify
	 * initialization happened correctly.
	 */
	{
		std::lock_guard<std::mutex> init_guard{ w->m_init_mtx };
		do_noexcept([&]() {
			w->assign_thread(std::thread{ &worker::run, w.get() });
			w.release();
		    });
	}
}

void
threadpool::impl::set_nthreads(unsigned int n)
{
	auto old = this->n_threads.exchange(n,
	    std::memory_order_relaxed);
	const auto growth = (n > old ? n - old : 0U);

	/*
	 * Reduce oversize.
	 *
	 * This operation basically cancels thread death to grow
	 * the number of threads.
	 */
	if (old < n)
		old += this->reduce_oversize(n - old);

	/* Create new threads on growth. */
	try {
		for (; old < n; ++old)
			this->create_worker();
	} catch (...) {
		const auto deficit = n - old;
		/* XXX handle insufficiently created threads. */
		throw;
	}

	/* Destroy idle threads on shrinking. */
	while (old > n) {
		/*
		 * Pop from the back:
		 * those threads have been sleeping the longest and
		 * probably don't have much relevant data in the cpu
		 * cache.
		 * This choice leaves threads with better cpu cache
		 * affinity on the runqueue, making them perform a bit
		 * better than the threads we kill.
		 */
		auto i = this->m_idle.pop_back_nowait();
		if (!i)
			break;
		if (i->kill())
			--old;
	}

	/* If the current thread is a worker thread, suicide now. */
	if (old > n) {
		auto& tls = get_tls();
		if (tls.tp == this) {
			assert(tls.w);
			if (tls.w->kill())
				--old;
		}
	}

	/*
	 * Publish number of threads that are to die.
	 * Threads that die will reduce this number.
	 */
	if (old > n)
		this->increase_oversize(old - n);
}


/*
 * Implementation deleter.
 *
 * Destroys the implementation preferably instantly, but if the implementation
 * is destroyed from within its worker thread, the worker thread will be
 * instructed to perform the cleanup.
 */
void
threadpool::impl_deleter::operator()(impl* i) const noexcept
{
	i->set_nthreads(0U);
	atomic_store(&i->m_serv, nullptr);

	/*
	 * If the threadpool is destroyed from within, collect the threadpool
	 * after the worker thread ends.
	 */
	auto& tls = threadpool::impl::get_tls();
	if (tls.tp == i)
		tls.collect = true;
	else
		delete i;
}

threadpool::threadpool()
:	threadpool(std::max(1U, std::thread::hardware_concurrency()))
{
	/* Empty body. */
}

threadpool::threadpool(unsigned int n_threads)
:	m_impl(new impl())
{
	this->m_impl->set_nthreads(n_threads);
}

threadpool::~threadpool() noexcept
{
	/* Empty body. */
}

void
threadpool::set_nthreads(unsigned int n)
{
	if (!this->m_impl) {
		throw std::runtime_error("threadpool: "
		    "no implementation present");
	}
	this->m_impl->set_nthreads(n);
}

unsigned int
threadpool::get_nthreads() const noexcept
{
	return (this->m_impl ? this->m_impl->get_nthreads() : 0U);
}

bool
threadpool::curthread_is_threadpool() const noexcept
{
	return (this->m_impl && this->m_impl->curthread_is_threadpool());
}

void
threadpool::impl::worker::do_sleep() noexcept
{
	/* Publisher: puts this worker on the idle queue. */
	class idle_set_guard
	{
	private:
		worker& self;
		impl& tp;

	public:
		idle_set_guard() = delete;
		idle_set_guard(const idle_set_guard&) = delete;
		idle_set_guard& operator=(const idle_set_guard&) =
		    delete;
		idle_set_guard(idle_set_guard&&) = delete;

		idle_set_guard(worker& self) noexcept
		:	self(self),
			tp(self.tp)
		{
			/*
			 * We push towards the front:
			 * this will make the threads that went to sleep
			 * the most recently be woken up first,
			 * providing better cache usage, since there is
			 * less chance of all data to have been flushed
			 * from the cpu caches.
			 */
			this->tp.m_idle.push_front(this->self);
		}

		~idle_set_guard() noexcept
		{
			this->tp.m_idle.erase(
			    this->tp.m_idle.iterator_to(this->self));
		}
	};

	/* Transition from BUSY to SLEEP_TEST. */
	if (!this->transition(thread_state::BUSY,
	    thread_state::SLEEP_TEST,
	    std::memory_order_acquire,
	    std::memory_order_relaxed))
		return;
	/* Publish as idle. */
	idle_set_guard pub{ *this };
	/* Perform sleep test. */
	if (this->tp.has_work()) {
		this->transition(thread_state::SLEEP_TEST,
		    thread_state::BUSY,
		    std::memory_order_release,
		    std::memory_order_relaxed);
		return;
	}

	/* Prevent missing of wakeup calls. */
	std::unique_lock<std::mutex> guard{ this->m_sleep_mtx };
	/* Transition to SLEEP. */
	if (!this->transition(thread_state::SLEEP_TEST,
	    thread_state::SLEEP,
	    std::memory_order_acq_rel, std::memory_order_release))
		return;
	/* Wait until state changes to non-sleep. */
	do {
		if (!this->must_die())
			this->m_sleep_cnd.wait(guard);
	} while (this->m_state.load(std::memory_order_acq_rel) ==
	    thread_state::SLEEP);
}

bool
threadpool::impl::worker::must_die() noexcept
{
	switch (this->m_state.load(std::memory_order_relaxed)) {
	case thread_state::BUSY:
	case thread_state::SLEEP:
	case thread_state::SLEEP_TEST:
		break;
	default:
		return true;
	}

	if (this->tp.reduce_oversize(1U)) {
		if (this->transition(thread_state::BUSY,
		      thread_state::DYING,
		      std::memory_order_release,
		      std::memory_order_relaxed) ||
		    this->transition(thread_state::SLEEP_TEST,
		      thread_state::DYING,
		      std::memory_order_release,
		      std::memory_order_relaxed) ||
		    this->transition(thread_state::SLEEP,
		      thread_state::DYING,
		      std::memory_order_release,
		      std::memory_order_relaxed))
			return true;
		else
			this->tp.increase_oversize(1U);
	}

	return false;
}

unsigned int
threadpool::threadpool_service::wakeup(unsigned int n) noexcept
{
	threadpool_service_lock lck{ *this };
	if (!this->has_service() || n == 0)
		return 0;
	return this->m_self.wakeup(n);
}

bool
threadpool::impl::worker::_run() noexcept
{
	/* Wait with running until initialization is complete. */
	{
		std::lock_guard<std::mutex> init_guard{
			this->m_init_mtx
		};
		assert(this->m_thread.get_id() ==
		    std::this_thread::get_id());
	}

	/* Publish worker thread. */
	auto& tls = get_tls();
	tls.tp = &this->tp;
	tls.w = this;
	tls.collect = false;

	/* Collection of dead threads is done every interval. */
	unsigned int interval = 0;

	while (!this->must_die()) {
		/*
		 * Perform a unit of work.
		 */
		if (!this->tp.do_work()) {
			interval = (this->tp.collect() == 0 ?
			    0U : COLLECT_INTERVAL);
			this->do_sleep();
		}

		/*
		 * Cleanup dead threads every once in a while.
		 */
		if (interval >= COLLECT_INTERVAL) {
			/*
			 * If the collect call actually did something,
			 * try to run it again next time.
			 */
			if (this->tp.collect() == 0)
				interval = 0;
		} else
			++interval;
	}


	/*
	 * Epilogue: mark thread as dead and publish it for collection.
	 * In the case that this worker thread is responsible for final
	 * cleanup, do_destroy will be set to true (and this thread
	 * will not be published).
	 */

	/* Mark as dead. */
	this->m_state.store(thread_state::DEAD);
	/*
	 * Inform blocking threads that
	 * this thread is collectible.
	 */
	std::lock_guard<std::mutex> lck{
		this->tp.m_active_mtx
	};
	assert(this->tp.n_active > 0U);
	--this->tp.n_active;

	if (!tls.collect) {
		/*
		 * This worker thread is not responsible
		 * for destroying the threadpool,
		 * thus the threadpool is responsible
		 * for destroying this worker.
		 *
		 * Push on collector queue.
		 */
		this->tp.m_dead.push_back(*this);
	} else {
		/*
		 * This thread will survive the destruction of threadpool,
		 * ensure it will clean itself up.
		 */
		this->m_thread.detach();
	}

	/* Notify dead thread collector. */
	this->tp.m_active_cnd.notify_all();

	return tls.collect;
}

threadpool::threadpool_service::~threadpool_service() noexcept
{
	/* Empty body. */
}

void
threadpool::attach(threadpool_service_ptr<threadpool_service> p)
{
	if (!this->m_impl) {
		throw std::logic_error("threadpool: "
		    "cannot use uninitialized");
	}

	this->m_impl->attach(std::move(p));
}

template void threadpool_attach<tp_service_multiplexer, threadpool>(
    tp_service_multiplexer&, threadpool&);
template void threadpool_attach<workq_service, threadpool>(
    workq_service&, threadpool&);


} /* namespace ilias */
