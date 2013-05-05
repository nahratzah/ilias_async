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
#include <ilias/threadpool_intf.h>
#include <ilias/util.h>
#include <stdexcept>


namespace ilias {
namespace threadpool_intf_detail {


void
acquire(const threadpool_intf_refcnt& self) noexcept
{
	const auto c = self.m_refcnt.fetch_add(1U, std::memory_order_acquire);
	assert(c + 1U != 0U);
}

void
release(const threadpool_intf_refcnt& self) noexcept
{
	const auto c = self.m_refcnt.fetch_sub(1U, std::memory_order_release);
	if (c == 1U)
		delete &self;
}

void
client_acqrel::acquire(const threadpool_client_intf& tpc) const noexcept
{
	if (tpc.client_acquire())
		acquire(tpc);
}

void
client_acqrel::release(const threadpool_client_intf& tpc) const noexcept
{
	if (tpc.client_release()) {
		tpc.client_lock_wait();
		release(tpc);
	}
}

void
service_acqrel::acquire(const threadpool_service_intf& tps) const noexcept
{
	if (tps.service_acquire())
		acquire(tps);
}

void
service_acqrel::release(const threadpool_service_intf& tps) const noexcept
{
	if (tps.service_release()) {
		tps.service_lock_wait();
		release(tps);
	}
}

bool
refcount::service_acquire() const noexcept
{
	return (this->m_service_refcnt.fetch_add(1U,
	    std::memory_order_acquire) == 0U);
}

bool
refcount::service_release() const noexcept
{
	const bool last = (this->m_service_refcnt.fetch_sub(1U,
	    std::memory_order_release) == 1U);
	if (last)
		const_cast<refcount*>(this)->on_service_detach();
	return last;
}

bool
refcount::client_acquire() const noexcept
{
	return (this->m_client_refcnt.fetch_add(1U,
	    std::memory_order_release) == 0U);
}

bool
refcount::client_release() const noexcept
{
	const bool last = (this->m_client_refcnt.fetch_sub(1U,
	    std::memory_order_release) == 1U);
	if (last)
		const_cast<refcount*>(this)->on_client_detach();
	return last;
}

bool
refcount::_has_service() const noexcept
{
	return (this->m_service_refcnt.load(std::memory_order_relaxed) > 0U);
}

bool
refcount::_has_client() const noexcept
{
	return (this->m_service_refcnt.load(std::memory_order_relaxed) > 0U);
}

threadpool_intf_refcnt::~threadpool_intf_refcnt() noexcept
{
	/* Empty body. */
}


} /* namespace ilias::threadpool_intf_detail */


void
threadpool_client_intf::on_service_detach() noexcept
{
	/* Default implementation: do nothing. */
}

void
threadpool_service_intf::on_client_detach() noexcept
{
	/* Default implementation: do nothing. */
}

threadpool_client_intf::~threadpool_client_intf() noexcept
{
	/* Empty body. */
}

threadpool_service_intf::~threadpool_service_intf() noexcept
{
	/* Empty body. */
}


void
tp_service_set::threadpool_service::activate() noexcept
{
	this->m_self.m_active.push_back(*this);
}

bool
tp_service_set::threadpool_service::post_deactivate() noexcept
{
	switch (this->m_work_avail.load(std::memory_order_relaxed)) {
	case work_avail::DETACHED:
		return true;
	case work_avail::YES:
	case work_avail::MAYBE:
		this->activate();
		/* FALLTHROUGH */
	default:
		return false;
	}
}

bool
tp_service_set::threadpool_service::invoke_work() noexcept
{
	work_avail wa = work_avail::YES;
	this->m_work_avail.compare_exchange_strong(wa, work_avail::MAYBE,
	    std::memory_order_relaxed,
	    std::memory_order_relaxed);
	switch (wa) {
	case work_avail::YES:
	case work_avail::MAYBE:
		break;
	case work_avail::NO:
	case work_avail::DETACHED:
		return false;
	}

	/* m_work_avail = MAYBE */

	const bool rv = this->do_work();

	if (rv) {
		/*
		 * We always need to set work_avail to YES if we completed
		 * work, since another worker thread may have set this to
		 * NO.
		 * Another thread may have set this to NO, because the job
		 * was for instance only single threaded and the thread
		 * could not pick up any work.
		 */
		wa = this->m_work_avail.exchange(work_avail::YES,
		    std::memory_order_relaxed);
		switch (wa) {
		case work_avail::YES:
		case work_avail::MAYBE:
			break;
		case work_avail::NO:
			/*
			 * Reactivate, this may fail if another thread is still
			 * deactivating this, but in that case the epilogue of
			 * the deactivate call will reactivate it again.
			 */
			this->activate();
			break;
		case work_avail::DETACHED:
			return false;
		}
		return true;
	}

	wa = work_avail::MAYBE;
	this->m_work_avail.compare_exchange_strong(wa, work_avail::NO,
	    std::memory_order_relaxed, std::memory_order_relaxed);
	return false;
}

bool
tp_service_set::threadpool_service::invoke_test() noexcept
{
	switch (this->m_work_avail.load(std::memory_order_relaxed)) {
	case work_avail::MAYBE:
	case work_avail::YES:
		return true;
	default:
		break;
	}

	const bool rv = this->has_work();
	if (rv) {
		this->m_work_avail.store(work_avail::YES,
		    std::memory_order_relaxed);
	}
	return rv;
}

unsigned int
tp_service_set::threadpool_service::wakeup(unsigned int n) noexcept
{
	if (n == 0)
		return 0;

	threadpool_service_lock lck(*this);

	if (!this->has_service())
		return 0;

	const auto c = this->m_work_avail.exchange(work_avail::YES,
	    std::memory_order_acq_rel);
	if (c == work_avail::DETACHED) {
		this->m_work_avail.store(work_avail::DETACHED,
		    std::memory_order_release);
		return 0;
	}

	this->m_self.m_active.push_back(*this);
	return this->m_self.wakeup(n);
}

void
tp_service_set::threadpool_service::on_client_detach() noexcept
{
	threadpool_service_lock lck(*this);

	if (!this->has_service())
		return;

	this->m_work_avail.store(work_avail::DETACHED);
	this->m_self.m_active.unlink_robust(
	    this->m_self.m_active.iterator_to(*this));
	this->m_self.m_data.unlink_robust(
	    this->m_self.m_data.iterator_to(*this));
}

bool
tp_service_set::do_work() noexcept
{
	this->m_active.remove_and_dispose_if(
	    [](threadpool_service& s) -> bool {
		return !s.invoke_work();
	    },
	    [](threadpool_service* s) -> void {
		s->post_deactivate();
	    });
	return !this->m_active.empty();
}

bool
tp_service_set::has_work() noexcept
{
	this->m_active.remove_if([](threadpool_service& s) {
		return !s.invoke_test();
	    });
	return !this->m_active.empty();
}

void
tp_service_set::attach(threadpool_service_ptr<threadpool_service> p)
{
	if (!p) {
		throw std::invalid_argument("cannot attach "
		    "null threadpool service");
	}

	do_noexcept([&]() {
		this->m_data.push_back(p);
		this->m_active.push_back(*p);
		p->wakeup(threadpool_client_intf::WAKE_ALL);
	    });
}


}
