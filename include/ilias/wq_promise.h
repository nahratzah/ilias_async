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
#ifndef ILIAS_WQ_PROMISE_H
#define ILIAS_WQ_PROMISE_H

#include <ilias/workq.h>
#include <ilias/promise.h>
#include <functional>

namespace ilias {
namespace wqprom_detail {


/*
 * Workq based promise callback.
 *
 * Will execute at most once.
 * If the promise is never started, the callback may never be invoked.
 */
template<typename PromType>
class wq_promise_event
:	public workq_job,
	public std::enable_shared_from_this<wq_promise_event<PromType>>
{
private:
	PromType m_prom;
	std::shared_ptr<wq_promise_event> m_self;
	std::function<void(PromType)> m_fn;

public:
	wq_promise_event(workq_ptr wq, std::function<void(PromType)> fn,
	    unsigned int flags)
	:	workq_job(wq, flags | workq_job::TYPE_ONCE),
		m_fn(std::move(fn))
	{
		if (flags & workq_job::TYPE_PERSIST) {
			throw std::invalid_argument(
			    "promise workq job cannot be persistant");
		}
	}

	/*
	 * Fill in promise, activate the workq job and store the pointer to
	 * self in order to keep the job alive.
	 */
	void
	pfcb(PromType prom) noexcept
	{
		this->m_prom = std::move(prom);
		this->activate(workq_job::ACT_IMMED);
		this->m_self = this->shared_from_this();
	}

	virtual void
	run() noexcept
	{
		this->m_self.reset();	/* Cancel our self reference. */
		PromType p = std::move(this->m_prom);
		auto q = p;
		try {
			this->m_fn(std::move(p));
		} catch (...) {
			q.set_exception(std::current_exception());
		}
	}
};

/*
 * Workq based future callback.
 *
 * Will execute at most once.
 * If the promise is never started, the callback may never be invoked.
 * The callback must not throw exceptions.
 */
template<typename FutType>
class wq_future_event
:	public workq_job,
	public std::enable_shared_from_this<wq_future_event<FutType>>
{
private:
	FutType m_fut;
	std::shared_ptr<wq_future_event> m_self;
	std::function<void(FutType)> m_fn;

public:
	wq_future_event(workq_ptr wq, std::function<void(FutType)> fn,
	    unsigned int flags)
	:	workq_job(wq, flags | workq_job::TYPE_ONCE),
		m_fn(std::move(fn))
	{
		if (flags & workq_job::TYPE_PERSIST) {
			throw std::invalid_argument(
			    "promise workq job cannot be persistant");
		}
	}

	/*
	 * Fill in promise, activate the workq job and store the pointer to
	 * self in order to keep the job alive.
	 */
	void
	pfcb(FutType prom) noexcept
	{
		this->m_fut = std::move(prom);
		this->activate(workq_job::ACT_IMMED);
		this->m_self = this->shared_from_this();
	}

	virtual void
	run() noexcept
	{
		this->m_self.reset();	/* Cancel our self reference. */
		FutType p = std::move(this->m_fut);
		this->m_fn(std::move(p));
	}
};

extern template class ILIAS_ASYNC_EXPORT wq_promise_event<promise<void>>;
extern template class ILIAS_ASYNC_EXPORT wq_future_event<promise<void>>;


} /* namespace ilias::wqprom_detail */


/* Attach asynchronous callback to promise. */
template<typename Type, typename Functor>
void
callback(promise<Type>& prom, workq_ptr wq, Functor&& fn,
    unsigned int fl = 0)
{
	typedef wqprom_detail::wq_promise_event<promise<Type>> event;
	using namespace std::placeholders;

	callback(prom, std::bind(&event::pfcb,
	    new_workq_job<event>(std::move(wq), std::forward<Functor>(fn), fl),
	    _1));
}

/* Attach asynchronous callback to future. */
template<typename Type, typename Functor>
void
callback(future<Type>& fut, workq_ptr wq, Functor&& fn,
    unsigned int fl = 0, promise_start ps = PROM_START)
{
	typedef wqprom_detail::wq_future_event<future<Type>> event;
	using namespace std::placeholders;

	callback(fut, std::bind(&event::pfcb,
	    new_workq_job<event>(std::move(wq), std::forward<Functor>(fn), fl),
	    _1), ps);
}


} /* namespace ilias */

#endif /* ILIAS_WQ_PROMISE_H */
