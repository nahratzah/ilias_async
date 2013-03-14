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
#ifndef ILIAS_WQ_MSG_QUEUE_H
#define ILIAS_WQ_MSG_QUEUE_H

#include <ilias/msg_queue.h>
#include <ilias/workq.h>
#include <type_traits>
#include <functional>

namespace ilias {
namespace wq_mq_detail {


template<typename MqType>
class wq_mq_job
:	public workq_job
{
public:
	using mq_type = MqType;
	using functor_type = std::function<void (mq_type&)>;

private:
	std::atomic<mq_type*> m_args{ nullptr };
	const functor_type m_functor;

public:
	wq_mq_job(workq_ptr wq, functor_type fn, unsigned int fl = 0)
	:	workq_job(std::move(wq), fl | workq_job::TYPE_PERSIST),
		m_functor(std::move(fn))
	{
		if (!this->m_functor) {
			throw std::invalid_argument(
			    "No functor for message queue event.");
		}
	}

	wq_mq_job(const wq_mq_job&) = delete;
	wq_mq_job& operator=(const wq_mq_job&) = delete;
	wq_mq_job(wq_mq_job&&) = delete;

	virtual void
	run() noexcept override
	{
		mq_type* mq = this->m_args.load(std::memory_order_relaxed);
		assert(mq);
		if (mq->empty())
			mq->deactivate();
		else
			this->m_functor(*mq);
	}

	static void
	mq_callback(const std::shared_ptr<wq_mq_job>& self, mq_type& mq)
	{
		self->m_args.store(&mq, std::memory_order_relaxed);
		self->activate();
	}
};


} /* namespace ilias::wq_mq_detail */


template<typename MqEvDerived>
void
callback(msg_queue_events<MqEvDerived>& mqev, workq_ptr wq,
    std::function<void(MqEvDerived&)> functor,
    unsigned int wq_flags = 0)
{
	typedef wq_mq_detail::wq_mq_job<MqEvDerived> impl_type;
	using namespace std::placeholders;

	callback(mqev, std::bind(&impl_type::mq_callback,
	    new_workq_job<impl_type>(std::move(wq),
	    std::move(functor), wq_flags), _1));
}

extern template ILIAS_ASYNC_EXPORT void callback(
    msg_queue_events<msg_queue<void>>&, workq_ptr,
    std::function<void(msg_queue<void>&)>, unsigned int);


} /* namespace ilias */

#endif /* ILIAS_WQ_MSG_QUEUE_H */
