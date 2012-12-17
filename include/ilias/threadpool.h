/*
 * Copyright (c) 2012 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef ILIAS_THREADPOOL_H
#define ILIAS_THREADPOOL_H

#include <ilias/ilias_async_export.h>
#include <ilias/ll.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ilias {


class ILIAS_ASYNC_LOCAL threadpool
{
private:
	struct idle_tag {};
	class thread;
	typedef std::unique_ptr<thread> thread_ptr;
	typedef std::vector<thread_ptr> all_threads;
	typedef ll_list<ll_base<thread, idle_tag> > idle_threads;

	std::function<thread_ptr(threadpool&, unsigned int)> m_factory;
	std::unique_ptr<idle_threads> m_idle;
	all_threads m_all;

	ILIAS_ASYNC_LOCAL static thread_ptr
	factory_impl(threadpool& self, unsigned int idx,
	    const std::function<bool()>& pred, const std::function<bool()>& work);

public:
	static unsigned int
	default_thread_count() noexcept
	{
		return std::max(1U, std::thread::hardware_concurrency());
	}

	ILIAS_ASYNC_EXPORT threadpool(std::function<bool()> pred, std::function<bool()> work,
	    unsigned int threads = default_thread_count());
	ILIAS_ASYNC_EXPORT threadpool(threadpool&& o) noexcept;
	ILIAS_ASYNC_EXPORT ~threadpool() noexcept;

	ILIAS_ASYNC_EXPORT bool curthread_is_threadpool() noexcept;


	threadpool() = delete;
	threadpool(const threadpool&) = delete;
	threadpool& operator=(const threadpool&) = delete;
};


class ILIAS_ASYNC_LOCAL threadpool::thread final :
	public ll_base_hook<threadpool::idle_tag>
{
public:
	/*
	 * DFA.  Allowed transisitions:
	 *             -> { ACTIVE }
	 * ACTIVE      -> { SLEEP_TEST, DYING, SUICIDE }
	 * SLEEP_TEST  -> { ACTIVE, SLEEP, DYING, SUICIDE }
	 * SLEEP       -> { ACTIVE, DYING, SUICIDE }
	 * DYING       -> {  }
	 * SUICIDE     -> {  }
	 *
	 * Const outside class, since gcc 4.6.2 blows up during link stage.
	 */
	static const int STATE_ACTIVE;
	static const int STATE_SLEEP_TEST;
	static const int STATE_SLEEP;
	static const int STATE_DYING;	/* Worker died and needs to be joined. */
	static const int STATE_SUICIDE;	/* Worker killed itself and detached. */

	enum kill_result {
		KILL_TWICE,	/* Was already dying. */
		KILL_OK,	/* Was killed by current invocation. */
		KILL_SUICIDE	/* Call to kill was suicide. */
	};

private:
	class publish_idle
	{
	private:
		thread& m_self;

	public:
		publish_idle(thread& s) noexcept;
		~publish_idle() noexcept;


		publish_idle() = delete;
		publish_idle(const publish_idle&) = delete;
		publish_idle& operator=(const publish_idle&) = delete;
	};

	std::atomic<int> m_state;
	std::mutex m_sleep_mtx;
	std::condition_variable m_wakeup;

public:
	idle_threads& m_idle;
	const unsigned int m_idx;
	std::thread m_self;	/* Must be the last variable in this class. */

	int
	get_state() const noexcept
	{
		return this->m_state.load(std::memory_order_relaxed);
	}

private:
	ILIAS_ASYNC_LOCAL void do_sleep(const std::function<bool()>& pred) noexcept;
	ILIAS_ASYNC_LOCAL std::unique_ptr<threadpool::thread> run(const std::function<bool()>& pred,
	    const std::function<bool()>& functor) noexcept;

public:
	thread(threadpool& tp, unsigned int idx,
	    const std::function<bool()>& pred, const std::function<bool()>& worker) :
		m_state(STATE_ACTIVE),
		m_sleep_mtx(),
		m_wakeup(),
		m_idle(*tp.m_idle),
		m_idx(idx),
		m_self(&thread::run, this, pred, worker)
	{
		/* Empty body. */
	}

	bool wakeup() noexcept;
	kill_result kill() noexcept;
	void join() noexcept;
	~thread() noexcept;

	static thread*& tls_self() noexcept;

	std::thread::id
	get_id() const noexcept
	{
		return this->m_self.get_id();
	}


	thread() = delete;
	thread(const thread&) = delete;
	thread& operator=(const thread&) = delete;
};


inline
threadpool::thread::publish_idle::publish_idle(thread& s) noexcept :
	m_self(s)
{
	this->m_self.m_idle.push_back(this->m_self);
}

inline
threadpool::thread::publish_idle::~publish_idle() noexcept
{
	this->m_self.m_idle.erase_element(this->m_self.m_idle.iterator_to(this->m_self));
}


} /* namespace ilias */

#endif /* ILIAS_THREADPOOL_H */
