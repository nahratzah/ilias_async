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
#include <ilias/msg_queue.h>
#include <limits>


namespace ilias {
namespace mq_detail {


msg_queue_events::~msg_queue_events() noexcept
{
	/* Empty body. */
}

void
msg_queue_events::_fire_output() noexcept
{
	workq_deactivate(this->ev_empty);
	workq_activate(this->ev_output, workq_job::ACT_IMMED);
}

void
msg_queue_events::_fire_empty() noexcept
{
	workq_deactivate(this->ev_output);
	workq_activate(this->ev_empty);
}

void
msg_queue_events::_assign_output(workq_job_ptr job, bool fire) noexcept
{
	atomic_exchange_jobptr(this->ev_output, job,
	    std::memory_order_relaxed);
	if (fire && job)
		job->activate();
}

void
msg_queue_events::_assign_empty(workq_job_ptr job, bool fire) noexcept
{
	atomic_exchange_jobptr(this->ev_empty, job,
	    std::memory_order_relaxed);
	if (fire && job)
		job->activate();
}

void
msg_queue_events::_clear_events() noexcept
{
	this->_clear_output();
	this->_clear_empty();
}


uintptr_t
void_msg_queue::_dequeue(uintptr_t max) noexcept
{
	auto sz = this->m_size.load(std::memory_order_relaxed);
	uintptr_t subtract;

	do {
		if (sz == 0)
			return 0;
		subtract = std::min(sz, max);
	} while (!this->m_size.compare_exchange_weak(sz, sz - subtract,
	    std::memory_order_relaxed, std::memory_order_relaxed));
	if (sz == subtract)
		this->_fire_empty();
	return subtract;
}


} /* namespace ilias::mq_detail */


} /* namespace ilias */
