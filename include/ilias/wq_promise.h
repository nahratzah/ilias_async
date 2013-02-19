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
#ifndef ILIAS_PROMISE_H
#define ILIAS_PROMISE_H

#include <ilias/workq.h>
#include <ilias/future.h>
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
	public std::enable_shared_from_this<wq_promise_event>
{
private:
	PromType m_prom;
	workq_ptr m_self;
	std::function<void()> m_fn;

public:
	wq_promise_event(workq_ptr wq, std::function<void()> fn,
	    unsigned int flags)
	:	workq_job(wq, flags | workq_job::TYPE_ONCE),
		m_fn(std::move(fn))
	{
		if (flags & workq::TYPE_PERSIST) {
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
		this->activate();
		this->m_self = this->shared_from_this();
	}

	virtual void
	run() noexcept
	{
		this->m_self.reset();	/* Cancel our self reference. */
		this->m_fn(this->m_prom);
	}
};


} /* namespace ilias::wqprom_detail */


/* Attach asynchronous callback to promise. */
template<typename Type, typename Functor>
void
callback(promise<Type>& prom, workq_ptr wq, Functor&& fn,
    unsigned int fl = 0)
{
	typedef wqprom_detail::wq_promise_event<promise<Type>> event;
	using namespace std::place_holders;

	callback(prom, std::bind(&event::pfcb,
	    new_workq_job<event>(std::move(wq), std::forward<Functor>(fn), fl),
	    _1));
}

/* Attach asynchronous callback to future. */
template<typename Type, typename Functor>
void
callback(future<Type>& fut, workq_ptr wq, Functor&& fn,
    unsigned int fl = 0)
{
	typedef wqprom_detail::wq_promise_event<future<Type>> event;
	using namespace std::place_holders;

	callback(fut, std::bind(&event::pfcb,
	    new_workq_job<event>(std::move(wq), std::forward<Functor>(fn), fl),
	    _1));
}


} /* namespace ilias */

#endif /* ILIAS_PROMISE_H */
