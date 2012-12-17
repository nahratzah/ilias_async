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
#include <ilias/threadpool.h>
#if !HAS_TLS
#include "tls_fallback.h"
#endif

namespace ilias {


const int threadpool::thread::STATE_ACTIVE = 0;
const int threadpool::thread::STATE_SLEEP_TEST = 1;
const int threadpool::thread::STATE_SLEEP = 2;
const int threadpool::thread::STATE_DYING = 0xff;
const int threadpool::thread::STATE_SUICIDE = 0xfe;


threadpool::thread*&
threadpool::thread::tls_self() noexcept
{
#if HAS_TLS
	static THREAD_LOCAL threadpool::thread* tls;
	return tls;
#else
	static tls<threadpool::thread*> impl;
	return *impl;
#endif
}


threadpool::threadpool(std::function<bool()> pred, std::function<bool()> work, unsigned int threads) :
	m_factory(std::bind(&factory_impl,
	    std::placeholders::_1, std::placeholders::_2,
	    std::move(pred), std::move(work))),
	m_idle(new idle_threads()),
	m_all()
{
	for (unsigned int i = 0; i < threads; ++i) {
		auto thr = this->m_factory(*this, i);
		assert(thr != nullptr);
		this->m_all.push_back(std::move(thr));
	}
}

threadpool::threadpool(threadpool&& o) noexcept :
	m_factory(std::move(o.m_factory)),
	m_idle(std::move(o.m_idle)),
	m_all(std::move(o.m_all))
{
	/* Empty body. */
}

threadpool::~threadpool() noexcept
{
	for (auto& thr : this->m_all) {
		switch (thr->kill()) {
		case thread::KILL_OK:
			thr->join();
			break;
		case thread::KILL_SUICIDE:
			thr.release();	/* Thread will free itself. */
			break;
		default:
			std::terminate();
		}
	}
	this->m_all.clear();
}

bool
threadpool::curthread_is_threadpool() noexcept
{
	const thread* t = thread::tls_self();
	return (t && &t->m_idle == this->m_idle.get());
}

bool
threadpool::thread::wakeup() noexcept
{
	int pstate = STATE_SLEEP;

	/* Change from sleep to active. */
	while (!this->m_state.compare_exchange_weak(pstate, STATE_ACTIVE,
	    std::memory_order_acquire, std::memory_order_relaxed)) {
		/* Failure: worker is not sleeping. */
		if (pstate != STATE_SLEEP || pstate != STATE_SLEEP_TEST)
			return false;
	}

	/* Signal wakeup. */
	if (pstate == STATE_SLEEP) {
		std::lock_guard<std::mutex> slck(this->m_sleep_mtx);
		this->m_idle.erase_element(this->m_idle.iterator_to(*this));
		this->m_wakeup.notify_one();
	}
	return true;
}

threadpool::thread::kill_result
threadpool::thread::kill() noexcept
{
	const int setstate = (this->get_id() == std::this_thread::get_id() ?
	    STATE_SUICIDE : STATE_DYING);

	int pstate = STATE_ACTIVE;
	while (!this->m_state.compare_exchange_weak(pstate, setstate,
	    std::memory_order_release, std::memory_order_relaxed)) {
		if (pstate == STATE_DYING || pstate == STATE_SUICIDE)
			return KILL_TWICE;
	}

	/* Signal wakeup. */
	if (pstate == STATE_SLEEP) {
		std::lock_guard<std::mutex> slck(this->m_sleep_mtx);
		this->m_idle.erase_element(this->m_idle.iterator_to(*this));
		this->m_wakeup.notify_one();
	}

	if (setstate == STATE_SUICIDE)
		this->m_self.detach();	/* Will destroy this on thread finish. */

	return (setstate == STATE_SUICIDE ? KILL_SUICIDE : KILL_OK);
}

void
threadpool::thread::join() noexcept
{
	assert(this->get_state() == STATE_DYING);
	this->m_self.join();
}

threadpool::thread::~thread() noexcept
{
	return;
}


threadpool::thread_ptr
threadpool::factory_impl(threadpool& self, unsigned int idx,
    const std::function<bool()>& pred, const std::function<bool()>& work)
{
	return thread_ptr(new thread(self, idx, pred, work));
}


std::unique_ptr<threadpool::thread>
threadpool::thread::run(const std::function<bool()>& pred, const std::function<bool()>& work) noexcept
{
	std::unique_ptr<threadpool::thread> cyanide;	/* Used to fulfill suicide. */

	struct tls_storage
	{
		tls_storage(thread& self) noexcept
		{
			assert(!tls_self());
			tls_self() = &self;
		}

		~tls_storage() noexcept
		{
			assert(tls_self());
			tls_self() = nullptr;
		}
	};

	tls_storage identification(*this);

	/* Keep running work while active. */
	while (this->get_state() == STATE_ACTIVE) {
		if (!work())
			do_sleep(pred);
	}

	/* If this thread killed itself, use cyanide to fulfill suicide promise. */
	if (this->get_state() == STATE_SUICIDE)
		cyanide.reset(this);
	return cyanide;
}

void
threadpool::thread::do_sleep(const std::function<bool()>& pred) noexcept
{
	int pstate = STATE_ACTIVE;

	/* Move to sleep-test state. */
	if (this->m_state.compare_exchange_strong(pstate, STATE_SLEEP_TEST,
	    std::memory_order_acquire, std::memory_order_relaxed)) {
		pstate = STATE_SLEEP_TEST;

		/*
		 * Publish idle state prior to testing predicate:
		 * if we publish after, there is a race where a wakeup will be missed,
		 * between the point the idle test completes and the publish operation
		 * happens.
		 */
		publish_idle pub(*this);

		if (pred()) {
			/* Don't go to sleep. */
			this->m_state.compare_exchange_strong(pstate, STATE_ACTIVE,
			    std::memory_order_acquire, std::memory_order_relaxed);
		} else {
			/* Go to sleep: predicate test failed to indicate more work is available. */
			std::unique_lock<std::mutex> slck(this->m_sleep_mtx);
			if (this->m_state.compare_exchange_strong(pstate, STATE_SLEEP,
			    std::memory_order_acq_rel, std::memory_order_relaxed)) {
				/* Sleep until our state changes to either active or dying. */
				this->m_wakeup.wait(slck, [this]() -> bool {
					return (this->get_state() != STATE_SLEEP);
				    });
			}
		}
	}

	/* Ensure state is valid on exit. */
	const auto end_state = this->get_state();
	assert(end_state == STATE_ACTIVE || end_state == STATE_DYING || end_state == STATE_SUICIDE);
}


} /* namespace ilias */
