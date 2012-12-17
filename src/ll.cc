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
#include <ilias/ll.h>


#ifdef _MSC_VER
#pragma warning( disable: 4290 )
#endif


namespace ilias {
namespace ll_detail {


/* Find the successor of this node. */
pointer_flag
hook_ptr::succ() const noexcept
{
	pointer_flag s = (*this)->m_succ.get();
	if (!s.first)
		return s;

	while (s.first.deleted()) {
		hook_ptr ss = s.first->m_succ.get().first;
		if ((*this)->m_succ.compare_exchange(s, ss))
			s.first = std::move(ss);
		else
			s = (*this)->m_succ.get();
	}
	return s;
}

/* Find the predecessor of this node. */
pointer_flag
hook_ptr::pred() const noexcept
{
	pointer_flag p = (*this)->m_pred.get();
	if (!p.first)
		return p;

	for (;;) {
		/* Move forward until p is the direct successor of this. */
		hook_ptr ps;
		while (!p.second /* p.second => this->deleted() */ &&
		    (ps = p.first.succ().first) != this->get()) {
			if ((*this)->m_pred.compare_exchange(p, ps))
				p.first = ps;
			else
				p = (*this)->m_pred.get();
		}

		/* p is a direct successor and hopefully not deleted. */
		if (!p.first.deleted())
			return p;

		/* Move to predecessor of p, since p is deleted. */
		hook_ptr pp = p.first->m_pred.get().first;
		if ((*this)->m_pred.compare_exchange(p, pp))
			p.first = pp;
		else
			p = (*this)->m_pred.get();
	}
}

std::size_t
hook_ptr::succ_end_distance(const hook* end) const noexcept
{
	size_t dist = 0;

	for (hook_ptr i = *this;
	    i != end;
	    i = std::move(i->m_succ.get().first)) {
		if (!i.deleted())
			++dist;
	}
	return dist;
}

bool
hook_ptr::unlink_nowait() const noexcept
{
	for (;;) {
		/* Read predecessor. */
		pointer_flag p = this->pred();
		if (!p.first || p.second)
			return false; /* Someone else is unlinking. */

		/* Acquire predecessor, iff its successor points at this. */
		ll_ptr::deref_lock<ll_ptr> lck(p.first->m_succ, false);
		if (!lck.lock_conditional(this->get()))
			continue; /* pred() changed. */

		/* Mark this for deletion (by setting m_pred to flagged). */
		if (!(*this)->m_pred.compare_exchange(p, pointer_flag(p.first, true))) {
			if (!p.first || p.second)
				return false; /* Someone else is unlinking. */
			continue; /* Restart attempt to unlink. */
		}

		/* Unlock predecessor. */
		lck.unlock();

		/* Update predecessor to skip this. */
		p.first.succ();

		return true;
	}
}

void
hook_ptr::unlink_wait(const hook& list_head) const noexcept
{
	assert(this->pred().second);
	for (hook_ptr s = std::move(this->succ().first);
	    (*this)->m_refcnt.load(std::memory_order_relaxed) > 1;
	    s = std::move(s.succ().first)) {
		if (s->m_pred.get_ptr() == *this)
			s.pred(); /* Fix predecessor. */

		/*
		 * Only after reaching the head, stop the search
		 * (since the head may still point at *this via its predecessor pointer.
		 */
		if (s == &list_head)
			break;
	}

	while ((*this)->m_refcnt.load(std::memory_order_relaxed) > 1) {
		//SPINWAIT();
	}

	const pointer_flag clear(hook_ptr(), false);
	(*this)->m_succ.exchange(hook_ptr());
	(*this)->m_pred.exchange(clear);
}

void
hook_ptr::unlink_wait_inslock(const hook& list_head) const noexcept
{
	for (hook_ptr s = std::move(this->succ().first);
	    (*this)->m_refcnt.load(std::memory_order_relaxed) > 1;
	    s = std::move(s.succ().first)) {
		if (s->m_pred.get_ptr() == *this)
			s.pred(); /* Fix predecessor. */

		/*
		 * Only after reaching the head, stop the search
		 * (since the head may still point at *this via its predecessor pointer.
		 */
		if (s == &list_head)
			break;
	}

	while ((*this)->m_refcnt.load(std::memory_order_relaxed) > 1) {
		//SPINWAIT();
	}

	const pointer_flag lck(hook_ptr(), true);
	(*this)->m_succ.exchange(lck);
	const pointer_flag clear(hook_ptr(), false);
	(*this)->m_pred.exchange(clear);
}

bool
hook_ptr::unlink(const hook& list_head) const noexcept
{
	if (!this->unlink_nowait())
		return false;
	this->unlink_wait(list_head);
	return true;
}

/*
 * This call unlinks the element (even if it is still being inserted).
 * It then acquires the insert lock, to prevent all insert operations.
 * This guarantees the element will not join any list.
 *
 * Returns true if an unlink operation was required to reach this stage.
 */
bool
hook_ptr::unlink_robust(const hook& list_head) const noexcept
{
	/*
	 * Attempt to unlink this and acquire the insert lock.
	 */
	do {
		/* Attempt to acquire insert lock (this is the final stage). */
		if (this->insert_lock())
			return false;
		/* Wait for the insert lock to unlock. */
		while ((*this)->m_succ.get_flag()) {
			//SPINWAIT();
		}
	} while (!this->unlink_nowait());

	/*
	 * The element is marked deleted.
	 * We will now acquire the insert lock.
	 *
	 * This blocks all list operations:
	 * - no insert is possible, since we hold the insert lock,
	 * - no delete is possible, since the element is not on the list.
	 */
	this->unlink_wait_inslock(list_head);
	return true;
}

bool
hook_ptr::insert_lock() const noexcept
{
	pointer_flag expect(nullptr, false);
	pointer_flag assign(nullptr, true);
	if (!(*this)->m_succ.compare_exchange(expect, assign))
		return false;

	/* Wait until m_pred is cleared. */
	while ((*this)->m_pred.is_set()) {
		// SPINWAIT()
	}
	return true;
}

bool
hook_ptr::insert_between(const hook_ptr& pred, const hook_ptr& succ) const noexcept
{
	pointer_flag orig_pred = (*this)->m_pred.exchange(pred, false);
	pointer_flag orig_succ = (*this)->m_succ.exchange(succ, true);
	assert(orig_pred.first == nullptr && !orig_pred.second);
	assert(orig_succ.first == nullptr && orig_succ.second);

	/*
	 * Ensure pred will not go away from under us.
	 *
	 * We use a conditional lock, to ensure pred will only lock
	 * if it isn't deleted.
	 */
	ll_ptr::deref_lock<ll_ptr> pred_lck(pred->m_pred, false);
	if (!pred_lck.lock_conditional(false)) {
		(*this)->m_succ.exchange(std::move(orig_succ));
		(*this)->m_pred.exchange(std::move(orig_pred));
		return false;
	}

	/* Change successor in pred from succ to this. */
	hook_ptr expect = succ;
	if (!pred->m_succ.compare_exchange(expect, *this)) {
		(*this)->m_succ.exchange(std::move(orig_succ));
		(*this)->m_pred.exchange(std::move(orig_pred));
		return false;
	}

	/*
	 * No failure past this point.
	 */

	/* Clear newly insert bit. */
	(*this)->m_succ.clear_flag();

	/* We are linked, unlock predecessor. */
	pred_lck.unlock();

	/* Fix predessor of succ. */
	succ.pred();

	return true;
}

void
hook_ptr::insert_after_locked(const hook_ptr& pred) const noexcept
{
	hook_ptr succ;
	do {
		succ = std::move(pred.succ().first);
	} while (!this->insert_between(pred, succ));
}

void
hook_ptr::insert_before_locked(const hook_ptr& succ) const noexcept
{
	hook_ptr pred;
	do {
		pred = std::move(succ.pred().first);
	} while (!this->insert_between(pred, succ));
}

list::simple_iterator&
list::first(list::simple_iterator& v) const noexcept
{
	hook_ptr headnode(const_cast<hook*>(&this->m_head));
	hook_ptr element = headnode.succ().first;
	v.reset(std::move(headnode), std::move(element));
	return v;
}

list::simple_iterator&
list::last(list::simple_iterator& v) const noexcept
{
	hook_ptr headnode(const_cast<hook*>(&this->m_head));
	hook_ptr element = headnode.pred().first;
	v.reset(std::move(headnode), std::move(element));
	return v;
}

list::simple_iterator&
list::listhead(list::simple_iterator& v) const noexcept
{
	hook_ptr headnode(const_cast<hook*>(&this->m_head));
	v.reset(std::move(headnode));
	return v;
}

hook*
list::pop_front() noexcept
{
	hook_ptr i(&this->m_head);
	for (i = std::move(i.succ().first); i != &this->m_head; i = std::move(i.succ().first)) {
		if (i.unlink(this->m_head))
			return i.get();
	}
	return nullptr;
}

hook*
list::pop_back() noexcept
{
	hook_ptr i(&this->m_head);
	for (i = std::move(i.pred().first); i != &this->m_head; i = std::move(i.pred().first)) {
		if (i.unlink(this->m_head))
			return i.get();
	}
	return nullptr;
}

bool
list::push_back(const hook_ptr& hp) noexcept
{
	return hp.insert_before(hook_ptr(&this->m_head));
}

bool
list::push_front(const hook_ptr& hp) noexcept
{
	return hp.insert_after(hook_ptr(&this->m_head));
}

list::simple_iterator&
list::pop_front_nowait(list::simple_iterator& v) noexcept
{
	hook_ptr head(&this->m_head);
	for (hook_ptr i = std::move(head.succ().first); i != &this->m_head; i = std::move(i.succ().first)) {
		if (i.unlink_nowait()) {
			v.reset(std::move(head), std::move(i));
			return v;
		}
	}

	v.reset(std::move(head));
	return v;
}

list::simple_iterator&
list::pop_back_nowait(list::simple_iterator& v) noexcept
{
	hook_ptr head(&this->m_head);
	for (hook_ptr i = std::move(head.pred().first); i != &this->m_head; i = std::move(i.pred().first)) {
		if (i.unlink_nowait()) {
			v.reset(std::move(head), std::move(i));
			return v;
		}
	}

	v.reset(std::move(head));
	return v;
}

list::simple_iterator&
list::push_back_nowait(list::simple_iterator& hp) noexcept
{
	hook_ptr head(&this->m_head);
	hp.element.unlink_wait_inslock(*hp.listhead);
	hp.element.insert_before_locked(head);
	hp.listhead = head;
	return hp;
}

list::simple_iterator&
list::push_front_nowait(list::simple_iterator& hp) noexcept
{
	hook_ptr head(&this->m_head);
	hp.element.unlink_wait_inslock(*hp.listhead);
	hp.element.insert_after_locked(head);
	hp.listhead = head;
	return hp;
}

list::simple_iterator&
list::iter_to(list::simple_iterator& v, hook& h) const noexcept
{
	hook_ptr headnode(const_cast<hook*>(&this->m_head));
	hook_ptr element(&h);
	v.reset(std::move(headnode), std::move(element));
	return v;
}

void
list::simple_iterator::step_forward() noexcept
{
	element = std::move(element.succ().first);
	if (element == listhead)
		element.reset();
}

void
list::simple_iterator::step_backward() noexcept
{
	element = std::move(element.pred().first);
	if (element == listhead)
		element.reset();
}


}} /* namespace ilias::ll_detail */
