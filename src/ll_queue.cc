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
#include <ilias/ll_queue.h>

namespace ilias {
namespace ll_queue_detail {


const ll_qhead::token_ ll_qhead::token = {};


void
ll_qhead::push_back_(elem* e) noexcept
{
	e->ensure_unused();
	e->m_succ.store(&this->m_head, std::memory_order_relaxed);

	hazard_t hz;
	elem* p = this->m_tail.load(std::memory_order_relaxed);
	bool done = false;

	while (!done) {
		hz.do_hazard(*p,
		    [&]() {
			elem* p_ = this->m_tail.load(
			    std::memory_order_relaxed);
			if (p != p_) {
				p = p_;
				return;
			}

			elem* expect = &this->m_head;
			if (p->m_succ.compare_exchange_weak(expect, e,
			    std::memory_order_release,
			    std::memory_order_relaxed)) {
				this->m_tail.compare_exchange_strong(p, e,
				    std::memory_order_relaxed,
				    std::memory_order_relaxed);
				done = true;
			} else if (this->m_tail.compare_exchange_weak(
			    p, expect,
			    std::memory_order_relaxed,
			    std::memory_order_relaxed))
				p = expect;
		    },
		    []() {
			assert(false);
		    });
	}

	this->m_size.fetch_add(1, std::memory_order_release);
}

ll_qhead::elem*
ll_qhead::pop_front_() noexcept
{
	hazard_t hz;
	elem* e = this->m_head.m_succ.load(std::memory_order_consume);
	bool done = false;

	while (!done && e != &this->m_head) {
		hz.do_hazard(*e,
		    [&]() {
			elem* e_ = this->m_head.m_succ.load(
			    std::memory_order_relaxed);
			if (e != e_) {
				e = e_;
				return;
			}

			elem* succ = e->m_succ.load(std::memory_order_relaxed);
			if (this->m_head.m_succ.compare_exchange_strong(
			    e,
			    succ,
			    std::memory_order_relaxed,
			    std::memory_order_consume)) {
				this->m_tail.compare_exchange_strong(e_, succ,
				    std::memory_order_relaxed,
				    std::memory_order_relaxed);
				done = true;
			}
		    },
		    []() {
			assert(false);
		    });
	}

	if (e == &this->m_head)
		e = nullptr;

	if (e)
		this->m_size.fetch_sub(1U, std::memory_order_release);

	return e;
}


}} /* namespace ilias::ll_queue_detail */
