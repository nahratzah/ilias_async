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
#ifndef ILIAS_PROM_MSG_QUEUE_H
#define ILIAS_PROM_MSG_QUEUE_H

#include <ilias/msg_queue.h>
#include <ilias/promise.h>
#include <cassert>
#include <memory>

namespace ilias {


/*
 * A message queue which accepts promises and only emits them
 * once they are ready.
 *
 * Note that the message queue is empty when there are no ready promises,
 * irrespective of the number of promises in flight.
 * This message queue does not maintain fifo behaviour.
 */
template<typename Type, typename Allocator = std::allocator<Type>>
class promise_msg_queue
{
public:
	typedef future<Type> future_type;

private:
	typedef msg_queue<future_type, Allocator> mq_type;

public:
	using element_type = typename mq_type::element_type;

private:
	std::shared_ptr<mq_type> m_mq;

public:
	template<typename... Args>
	promise_msg_queue(Args&&... args)
	:	m_mq(std::make_shared<msg_queue<future_type, Allocator>>(
		    std::forward<Args>(args)...))
	{
		/* Empty body. */
	}

	void
	enqueue(future_type f, promise_start ps = PROM_START)
	{
		if (f.ready())
			this->m_mq->enqueue(f);
		else {
			auto prep = std::make_shared<prepare_enqueue<mq_type>>(
			    *this->m_mq);
			auto mq = this->m_mq;
			callback(f, [prep, mq](future_type f) {
				prep->assign(std::move(f));
				prep->commit();
			    }, ps);
		}
	}

	const std::shared_ptr<msg_queue<future_type, Allocator>>&
	impl() const noexcept
	{
		return this->m_mq;
	}

	template<typename... Args>
	auto
	dequeue(Args&&... args) ->
	    decltype(m_mq->dequeue(std::forward<Args>(args)...))
	{
		this->impl()->dequeue(std::forward<Args>(args)...);
	}

	bool
	empty() const noexcept
	{
		return this->impl()->empty();
	}

	/*
	 * Allow all callbacks to be installed on
	 * the implementation message queue.
	 */
	template<typename... Args>
	friend void
	callback(promise_msg_queue& self, Args&&... args)
	    noexcept(
		noexcept(output_callback(*self.impl(),
		    std::forward<Args>(args)...)))
	{
		output_callback(*self.impl(), std::forward<Args>(args)...);
	}
};


} /* namespace ilias */

#endif /* ILIAS_PROM_MSG_QUEUE_H */
