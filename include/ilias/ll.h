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
#ifndef LL_H
#define LL_H

#include <ilias/ilias_async_export.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <type_traits>


#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable: 4251 )
#pragma warning( disable: 4800 )
#endif


namespace ilias {
namespace ll_detail {


class hook;
class hook_ptr;
class ll_ptr;
class list;

typedef std::pair<hook_ptr, bool> pointer_flag;


/*
 * Type of predecessor and successor pointers.
 */
class ILIAS_ASYNC_LOCAL ll_ptr
{
public:
	typedef std::uintptr_t internal_type;
	typedef std::atomic<internal_type> impl_type;
	typedef hook_ptr pointer;

private:
	static const internal_type FLAG = 0x2;
	static const internal_type DEREF = 0x1;
	static const internal_type MASK = (FLAG | DEREF);

	mutable impl_type m_value;

	static hook*
	decode_ptr(internal_type v) noexcept
	{
		return reinterpret_cast<hook*>(v & ~MASK);
	}

	static constexpr bool
	decode_flag(internal_type v) noexcept
	{
		return ((v & FLAG) == FLAG);
	}

	static pointer_flag decode(internal_type, bool = true) noexcept;

	static internal_type
	encode(const hook* p, bool f)
	{
		return reinterpret_cast<internal_type>(p) | (f ? FLAG : uintptr_t(0));
	}

	static internal_type encode(const hook_ptr&, bool);

	static internal_type encode(const pointer_flag&) noexcept;

	internal_type
	lock() const noexcept
	{
		internal_type v;
		do {
			v = this->m_value.fetch_or(DEREF, std::memory_order_acquire);
		} while (v & DEREF);
		return v;
	}

	bool
	lock_conditional(const hook* h, bool f) const noexcept
	{
		const internal_type v_clean = encode(h, f);

		internal_type v = v_clean;
		do {
			v &= ~DEREF;
			if (this->m_value.compare_exchange_weak(v, v | DEREF,
			    std::memory_order_acquire, std::memory_order_relaxed))
				return true;
		} while ((v & ~DEREF) == v_clean);
		return false;
	}

	bool
	lock_conditional(const hook* h) const noexcept
	{
		const internal_type v_clean = encode(h, false);

		internal_type v = v_clean;
		do {
			v &= ~DEREF;
			if (this->m_value.compare_exchange_weak(v, v | DEREF,
			    std::memory_order_acquire, std::memory_order_relaxed))
				return true;
		} while ((v & ~MASK) == v_clean);
		return false;
	}

	bool
	lock_conditional(bool f) const noexcept
	{
		internal_type v = (f ? FLAG : 0);
		do {
			v &= ~DEREF;
			if (this->m_value.compare_exchange_weak(v, v | DEREF,
			    std::memory_order_acquire, std::memory_order_relaxed))
				return true;
		} while ((v & FLAG) == (f ? FLAG : 0));
		return false;
	}

	void
	unlock() const noexcept
	{
		const internal_type old = this->m_value.fetch_and(~DEREF, std::memory_order_release);
		assert(old & DEREF);
	}

	internal_type
	unlock_exchange(internal_type nv) noexcept
	{
		assert(!(nv & DEREF));

		const internal_type old = this->m_value.exchange(nv, std::memory_order_release);
		assert(old & DEREF);
		return old;
	}

	bool
	locked() const noexcept
	{
		internal_type v = this->m_value.load(std::memory_order_relaxed);
		return (v & DEREF);
	}

public:
	template<typename LLPtr>
	class ILIAS_ASYNC_LOCAL deref_lock
	{
	private:
		LLPtr& m_self;
		bool m_locked;

	public:
		deref_lock(LLPtr& self, bool do_lock = true) noexcept :
			m_self(self),
			m_locked(false)
		{
			if (do_lock)
				this->lock();
		}

		~deref_lock() noexcept
		{
			if (this->m_locked)
				this->unlock();
		}

		internal_type
		lock() noexcept
		{
			assert(!this->m_locked);
			const internal_type rv = this->m_self.lock();
			this->m_locked = true;
			return rv;
		}

		bool
		lock_conditional(const hook* h, bool f) noexcept
		{
			assert(!this->m_locked);
			return (this->m_locked = this->m_self.lock_conditional(h, f));
		}

		bool
		lock_conditional(const hook* h) noexcept
		{
			assert(!this->m_locked);
			return (this->m_locked = this->m_self.lock_conditional(h));
		}

		bool
		lock_conditional(bool f) noexcept
		{
			assert(!this->m_locked);
			return (this->m_locked = this->m_self.lock_conditional(f));
		}

		void
		lock_take_ownership() noexcept
		{
			assert(!this->m_locked);
			assert(this->m_self.locked());
			this->m_locked = true;
		}

		void
		unlock() noexcept
		{
			assert(this->m_locked);
			this->m_self.unlock();
			this->m_locked = false;
		}

		pointer_flag unlock(const pointer_flag&) noexcept;
		pointer_flag unlock(pointer_flag&&) noexcept;

		bool
		locked() const noexcept
		{
			return this->m_locked;
		}

		LLPtr&
		lockable() const noexcept
		{
			return this->m_self;
		}

		deref_lock(const deref_lock&) = delete;
		deref_lock& operator=(const deref_lock&) = delete;
	};

	ll_ptr() noexcept :
		m_value(0)
	{
		/* Empty body. */
	}

	ll_ptr(std::nullptr_t) noexcept :
		m_value(0)
	{
		/* Empty body. */
	}

	~ll_ptr() noexcept;

	hook*
	get_ptr() const noexcept
	{
		return decode_ptr(this->m_value.load(std::memory_order_relaxed));
	}

	bool
	is_set() const noexcept
	{
		return (this->m_value.load(std::memory_order_relaxed) != 0);
	}

	pointer_flag get() const noexcept;

	bool
	get_flag() const noexcept
	{
		return decode_flag(this->m_value.load(std::memory_order_consume) & FLAG);
	}

	void
	clear_flag() const noexcept
	{
		internal_type old = this->m_value.fetch_and(~FLAG, std::memory_order_relaxed);
		assert(old & FLAG);
	}

	pointer_flag exchange(const hook_ptr&) noexcept;
	pointer_flag exchange(hook_ptr&&) noexcept;

	pointer_flag exchange(const hook_ptr&, bool) noexcept;
	pointer_flag exchange(hook_ptr&&, bool) noexcept;

	pointer_flag exchange(const pointer_flag&) noexcept;
	pointer_flag exchange(pointer_flag&&) noexcept;

private:
	bool cas_internal(pointer_flag&, internal_type, deref_lock<ll_ptr>*) noexcept;

public:
	bool compare_exchange(pointer_flag&, const pointer_flag&, deref_lock<ll_ptr>* = nullptr) noexcept;
	bool compare_exchange(pointer_flag&, pointer_flag&&, deref_lock<ll_ptr>* = nullptr) noexcept;

	bool compare_exchange(hook_ptr&, const hook_ptr&, deref_lock<ll_ptr>* = nullptr) noexcept;
	bool compare_exchange(hook_ptr&, hook_ptr&&, deref_lock<ll_ptr>* = nullptr) noexcept;

	bool compare_exchange(pointer_flag&, const hook_ptr&, deref_lock<ll_ptr>* = nullptr) noexcept;
	bool compare_exchange(pointer_flag&, hook_ptr&&, deref_lock<ll_ptr>* = nullptr) noexcept;
};


class ILIAS_ASYNC_LOCAL hook
{
friend class hook_ptr;
friend class list;

private:
	mutable ll_ptr m_pred, m_succ;
	mutable std::atomic<std::size_t> m_refcnt;

public:
	struct HEAD {};

	explicit hook(HEAD) noexcept;

	hook() noexcept :
		m_pred(),
		m_succ(),
		m_refcnt(0)
	{
		/* Empty body. */
	}

	hook(const hook&) = delete;
	hook& operator=(const hook&) = delete;
};


/*
 * Low level pointer to node.
 * Points at the hook, derived pointer is required to cast to actual elements.
 */
class ILIAS_ASYNC_LOCAL hook_ptr
{
public:
	typedef hook hook_type;

private:
	hook* m_ptr;

public:
	hook_ptr() noexcept :
		m_ptr(nullptr)
	{
		/* Empty body. */
	}

	hook_ptr(std::nullptr_t) noexcept :
		m_ptr(nullptr)
	{
		/* Empty body. */
	}

	hook_ptr(const hook_ptr& o) noexcept :
		m_ptr(o.m_ptr)
	{
		if (this->m_ptr)
			this->m_ptr->m_refcnt.fetch_add(1, std::memory_order_acquire);
	}

	hook_ptr(hook_ptr&& o) noexcept :
		m_ptr(o.m_ptr)
	{
		o.m_ptr = nullptr;
	}

	explicit hook_ptr(hook* p, bool acquire = true) noexcept :
		m_ptr(p)
	{
		if (this->m_ptr && acquire)
			this->m_ptr->m_refcnt.fetch_add(1, std::memory_order_acquire);
	}

	~hook_ptr() noexcept
	{
		this->reset();
	}

	hook_ptr&
	operator=(std::nullptr_t) noexcept
	{
		if (this->m_ptr) {
			this->m_ptr->m_refcnt.fetch_sub(1, std::memory_order_release);
			this->m_ptr = nullptr;
		}
		return *this;
	}

	hook_ptr&
	operator=(const hook_ptr& o) noexcept
	{
		if (this->m_ptr == o.m_ptr)
			return *this;

		if (this->m_ptr)
			this->m_ptr->m_refcnt.fetch_sub(1, std::memory_order_release);
		this->m_ptr = o.m_ptr;
		if (this->m_ptr)
			this->m_ptr->m_refcnt.fetch_add(1, std::memory_order_acquire);
		return *this;
	}

	hook_ptr&
	operator=(hook_ptr&& o) noexcept
	{
		assert(this != &o);

		if (this->m_ptr)
			this->m_ptr->m_refcnt.fetch_sub(1, std::memory_order_release);
		this->m_ptr = o.m_ptr;
		o.m_ptr = nullptr;
		return *this;
	}

	bool
	operator==(const hook_ptr& o) const noexcept
	{
		return (this->m_ptr == o.m_ptr);
	}

	bool
	operator==(const hook* p) const noexcept
	{
		return (this->m_ptr == p);
	}

	friend bool
	operator==(const hook* p, const hook_ptr& hp) noexcept
	{
		return (hp == p);
	}

	bool
	operator==(std::nullptr_t) const noexcept
	{
		return (this->m_ptr == nullptr);
	}

	bool
	operator!=(const hook_ptr& rhs) noexcept
	{
		return !(*this == rhs);
	}

	bool
	operator!=(const hook* p) const noexcept
	{
		return !(*this == p);
	}

	friend bool
	operator!=(const hook* p, const hook_ptr& hp) noexcept
	{
		return !(hp == p);
	}

	bool
	operator!=(std::nullptr_t) const noexcept
	{
		return !(*this == nullptr);
	}

	explicit operator bool() const noexcept
	{
		return (this->m_ptr != nullptr);
	}

	void
	reset() noexcept
	{
		*this = nullptr;
	}

	void
	reset(std::nullptr_t) noexcept
	{
		*this = nullptr;
	}

	void
	reset(const hook_ptr& o) noexcept
	{
		*this = o;
	}

	void
	reset(hook_ptr&& o) noexcept
	{
		*this = o;
	}

	void
	reset(hook_type* p, bool acquire = true) noexcept
	{
		*this = hook_ptr(p, acquire);
	}

	hook_type*
	get() const noexcept
	{
		return this->m_ptr;
	}

	hook_type*
	release() noexcept
	{
		hook_type*const rv = this->m_ptr;
		this->m_ptr = nullptr;
		return rv;
	}

	hook_type&
	operator*() const noexcept
	{
		return *this->get();
	}

	hook_type*
	operator->() const noexcept
	{
		return this->get();
	}

	static bool
	deleted(const hook_type& v) noexcept
	{
		return v.m_pred.get_flag();
	}

	bool
	deleted() const noexcept
	{
		return deleted(**this);
	}

	ILIAS_ASYNC_EXPORT pointer_flag succ() const noexcept;
	ILIAS_ASYNC_EXPORT pointer_flag pred() const noexcept;
	ILIAS_ASYNC_EXPORT std::size_t succ_end_distance(const hook*) const noexcept;

	ILIAS_ASYNC_EXPORT bool unlink_nowait() const noexcept;
	ILIAS_ASYNC_EXPORT void unlink_wait(const hook&) const noexcept;
	ILIAS_ASYNC_EXPORT void unlink_wait_inslock(const hook&) const noexcept;
	ILIAS_ASYNC_EXPORT bool unlink(const hook&) const noexcept;
	ILIAS_ASYNC_EXPORT bool unlink_robust(const hook&) const noexcept;

private:
	bool insert_lock() const noexcept;
	bool insert_between(const hook_ptr&, const hook_ptr&) const noexcept;

public:
	ILIAS_ASYNC_EXPORT void insert_after_locked(const hook_ptr&) const noexcept;
	ILIAS_ASYNC_EXPORT void insert_before_locked(const hook_ptr&) const noexcept;

	bool
	insert_after(const hook_ptr& pred) const noexcept
	{
		if (!this->insert_lock())
			return false;
		this->insert_after_locked(pred);
		return true;
	}

	bool
	insert_before(const hook_ptr& succ) const noexcept
	{
		if (!this->insert_lock())
			return false;
		this->insert_before_locked(succ);
		return true;
	}
};

/* Base list implementation. */
class ILIAS_ASYNC_LOCAL list
{
public:
	class simple_iterator;

private:
	hook m_head;

public:
	list() noexcept :
		m_head(hook::HEAD())
	{
		/* Empty body. */
	}

protected:
	/*
	 * This slightly weird initialization is to create a stable interface
	 * regardless of having move semantics.
	 * The stable interface ensures multiple compilers with different features
	 * will operate correct with the interface.
	 */
	ILIAS_ASYNC_EXPORT simple_iterator& first(simple_iterator&) const noexcept;
	ILIAS_ASYNC_EXPORT simple_iterator& last(simple_iterator&) const noexcept;
	ILIAS_ASYNC_EXPORT simple_iterator& listhead(simple_iterator&) const noexcept;
	ILIAS_ASYNC_EXPORT hook* pop_front() noexcept;
	ILIAS_ASYNC_EXPORT hook* pop_back() noexcept;
	ILIAS_ASYNC_EXPORT bool push_back(const hook_ptr& hp) noexcept;
	ILIAS_ASYNC_EXPORT bool push_front(const hook_ptr& hp) noexcept;
	ILIAS_ASYNC_EXPORT simple_iterator& iter_to(simple_iterator&, hook&) const noexcept;

	ILIAS_ASYNC_EXPORT simple_iterator& pop_front_nowait(simple_iterator&) noexcept;
	ILIAS_ASYNC_EXPORT simple_iterator& pop_back_nowait(simple_iterator&) noexcept;
	ILIAS_ASYNC_EXPORT simple_iterator& push_front_nowait(simple_iterator&) noexcept;
	ILIAS_ASYNC_EXPORT simple_iterator& push_back_nowait(simple_iterator&) noexcept;

public:
	bool
	empty() const noexcept
	{
		return (this->m_head.m_succ.get_ptr() == &this->m_head);
	}

	list(const list&) = delete;
	list& operator=(const list&) = delete;
};

class ILIAS_ASYNC_LOCAL list::simple_iterator
{
friend class list;

public:
	typedef std::ptrdiff_t difference_type;

private:
	hook_ptr listhead;
	hook_ptr element;

public:
	simple_iterator() noexcept :
		listhead(),
		element()
	{
		/* Empty body. */
	}

	simple_iterator(const simple_iterator& o) noexcept :
		listhead(o.listhead),
		element(o.element)
	{
		/* Empty body. */
	}

	simple_iterator(simple_iterator&& o) noexcept :
		listhead(std::move(o.listhead)),
		element(std::move(o.element))
	{
		/* Empty body. */
	}

private:
	void
	reset(const hook_ptr& listhead, const hook_ptr& element)
	{
		this->listhead = listhead;
		this->element = element;

		if (this->element == this->listhead)
			this->element = nullptr;
	}

	void
	reset(const hook_ptr& listhead)
	{
		this->listhead = listhead;
		this->element = nullptr;
	}

	void
	reset(hook_ptr&& listhead, hook_ptr&& element)
	{
		this->listhead = listhead;
		this->element = element;

		if (this->element == this->listhead)
			this->element = nullptr;
	}

	void
	reset(const hook_ptr& listhead, hook_ptr&& element)
	{
		this->listhead = listhead;
		this->element = element;

		if (this->element == this->listhead)
			this->element = nullptr;
	}

	void
	reset(hook_ptr&& listhead)
	{
		this->listhead = listhead;
		this->element = nullptr;
	}

	void
	reset()
	{
		this->listhead = nullptr;
		this->element = nullptr;
	}

public:
	ILIAS_ASYNC_EXPORT void step_forward() noexcept;
	ILIAS_ASYNC_EXPORT void step_backward() noexcept;

	const hook_ptr&
	get_internal() const noexcept
	{
		return element;
	}

	simple_iterator&
	operator=(const simple_iterator& o) noexcept
	{
		this->listhead = o.listhead;
		this->element = o.element;
		return *this;
	}
	simple_iterator&
	operator=(simple_iterator&& o) noexcept
	{
		this->listhead = std::move(o.listhead);
		this->element = std::move(o.element);
		return *this;
	}

	bool
	operator==(const simple_iterator& o) const noexcept
	{
		return (this->listhead == o.listhead && this->element == o.element);
	}

	bool
	operator!=(const simple_iterator& o) const noexcept
	{
		return !(*this == o);
	}

	/*
	 * Attempt to give an indication of distance between two iterators.
	 * Note that this is not an atomic operation, hence the distance is only an indication,
	 * unless the caller ensures no insert/unlink operations will take place.
	 *
	 * The only way to measure distance between two iterators, is to compare their distance to the list head.
	 * Trying to reach one from the other is unreliable, since either can be unlinked during the operation,
	 * causing the operation to fail.
	 */
	friend difference_type
	distance(const simple_iterator& first, const simple_iterator& last) noexcept
	{
		const std::size_t first_dist = (first.element ? first.element.succ_end_distance(first.listhead.get()) : 0);
		const std::size_t last_dist = (last.element ? last.element.succ_end_distance(last.listhead.get()) : 0);

		return difference_type(last_dist) - difference_type(first_dist);
	}

	bool
	unlink(list& lst) const noexcept
	{
		assert(this->listhead == &lst.m_head);
		assert(this->element);
		return this->element.unlink(lst.m_head);
	}

	bool
	unlink_robust(list& lst) const noexcept
	{
		assert(this->listhead == &lst.m_head);
		assert(this->element);
		return this->element.unlink_robust(lst.m_head);
	}

	void
	unlink_wait() const noexcept
	{
		assert(this->listhead);
		assert(this->element);
		return this->element.unlink_wait(*this->listhead);
	}
};


inline
hook::hook(HEAD) noexcept :
	m_pred(),
	m_succ(),
	m_refcnt(0)
{
	{
		hook_ptr self(this);
		this->m_pred.exchange(self);
		this->m_succ.exchange(self);
	}

	assert(this->m_pred.get_ptr() == this);
	assert(this->m_succ.get_ptr() == this);
	assert(!this->m_pred.get_flag());
	assert(!this->m_succ.get_flag());
	assert(this->m_refcnt.load(std::memory_order_relaxed) == 2);
}

inline pointer_flag
ll_ptr::get() const noexcept
{
	deref_lock<const ll_ptr> lck(*this, false);
	return decode(lck.lock());
}

inline pointer_flag
ll_ptr::decode(ll_ptr::internal_type v, bool acquire) noexcept
{
	hook_ptr p(reinterpret_cast<hook*>(v & ~MASK), acquire);
	return pointer_flag(std::move(p), v & FLAG);
}

inline ll_ptr::internal_type
ll_ptr::encode(const hook_ptr& p, bool f)
{
	return encode(p.get(), f);
}

inline ll_ptr::internal_type
ll_ptr::encode(const pointer_flag& pf) noexcept
{
	return encode(pf.first, pf.second);
}

inline
ll_ptr::~ll_ptr() noexcept
{
	hook_ptr hp_null;
	this->exchange(std::move(hp_null));
}

inline pointer_flag
ll_ptr::exchange(hook_ptr&& p) noexcept
{
	deref_lock<ll_ptr> lck(*this, false);
	bool f = decode_flag(lck.lock());
	return lck.unlock(pointer_flag(std::move(p), f));
}
inline pointer_flag
ll_ptr::exchange(const hook_ptr& p) noexcept
{
	hook_ptr copy = p;
	return this->exchange(std::move(copy));
}

inline pointer_flag
ll_ptr::exchange(hook_ptr&& p, bool f) noexcept
{
	pointer_flag pf(std::move(p), f);
	deref_lock<ll_ptr> lck(*this);
	return lck.unlock(std::move(pf));
}
inline pointer_flag
ll_ptr::exchange(const hook_ptr& p, bool f) noexcept
{
	hook_ptr copy = p;
	return this->exchange(std::move(copy), f);
}

inline pointer_flag
ll_ptr::exchange(const pointer_flag& pf) noexcept
{
	return this->exchange(pf.first, pf.second);
}
inline pointer_flag
ll_ptr::exchange(pointer_flag&& pf) noexcept
{
	return this->exchange(std::move(pf.first), std::move(pf.second));
}

template<typename LLPtr>
pointer_flag
ll_ptr::deref_lock<LLPtr>::unlock(pointer_flag&& pf) noexcept
{
	assert(this->m_locked);
	internal_type nv = encode(pf.first.release(), pf.second);
	assert(!(nv & DEREF));

	internal_type old = this->m_self.unlock_exchange(nv);
	this->m_locked = false;
	return decode(old, false);
}
template<typename LLPtr>
pointer_flag
ll_ptr::deref_lock<LLPtr>::unlock(const pointer_flag& pf) noexcept
{
	pointer_flag copy = pf;
	return this->unlock(std::move(copy));
}

inline bool
ll_ptr::cas_internal(pointer_flag& o_pf, ll_ptr::internal_type n, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	assert(opt_lck == nullptr || !opt_lck->locked());
	assert(opt_lck == nullptr || &opt_lck->lockable() == this);
	assert(!(n & DEREF));

	const std::memory_order succes = (opt_lck ? std::memory_order_acquire : std::memory_order_acq_rel);
	const internal_type o_expect = encode(o_pf);
	internal_type o = o_expect;
	if (opt_lck)
		n |= DEREF;

	do {
		if (this->m_value.compare_exchange_weak(o, n, succes, std::memory_order_relaxed)) {
			if (opt_lck)
				opt_lck->lock_take_ownership();
			decode(o, false);	/* Release ownership of o. */
			return true;
		}
		o &= ~DEREF;
	} while (o == o_expect);

	o_pf = decode(o);
	return false;
}

inline bool
ll_ptr::compare_exchange(pointer_flag& o_pf, pointer_flag&& n_pf, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	const bool rv = cas_internal(o_pf, encode(n_pf), opt_lck);
	if (rv)
		n_pf.first.release();
	return rv;
}
inline bool
ll_ptr::compare_exchange(pointer_flag& o_pf, const pointer_flag& n_pf, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	pointer_flag copy = n_pf;
	return this->compare_exchange(o_pf, std::move(copy), opt_lck);
}

inline bool
ll_ptr::compare_exchange(hook_ptr& ov, hook_ptr&& nv, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	pointer_flag o_pf(ov, false);
	pointer_flag n_pf(nv, false);

	while (!this->compare_exchange(o_pf, n_pf, opt_lck)) {
		if (o_pf.first != ov) {
			ov = std::move(o_pf.first);
			return false;
		}
		n_pf.second = o_pf.second;
	}
	return true;
}
inline bool
ll_ptr::compare_exchange(hook_ptr& ov, const hook_ptr& nv, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	hook_ptr copy = nv;
	return this->compare_exchange(ov, std::move(copy), opt_lck);
}

inline bool
ll_ptr::compare_exchange(pointer_flag& o_pf, hook_ptr&& nv, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	pointer_flag n_pf(nv, o_pf.second);
	return this->compare_exchange(o_pf, std::move(n_pf), opt_lck);
}
inline bool
ll_ptr::compare_exchange(pointer_flag& o_pf, const hook_ptr& nv, ll_ptr::deref_lock<ll_ptr>* opt_lck) noexcept
{
	hook_ptr copy = nv;
	return this->compare_exchange(o_pf, std::move(copy), opt_lck);
}

/*
 * Calculate offset of m_hook member in HookType.
 */
template<typename HookType>
struct hook_offset
{
public:
	static const std::ptrdiff_t offset = reinterpret_cast<std::ptrdiff_t>(&reinterpret_cast<HookType*>(0U)->m_hook);
};
/*
 * Given the m_hook member of HookType, find the address of HookType.
 */
template<typename HookType>
inline HookType*
hook_resolve(hook* h) noexcept
{
	if (h == nullptr)
		return nullptr;
	return reinterpret_cast<HookType*>(reinterpret_cast<uintptr_t>(h) - hook_offset<HookType>::offset);
}
/*
 * Given the m_hook member of HookType, find the address of HookType.
 */
template<typename HookType>
inline const HookType*
hook_resolve(const hook* h) noexcept
{
	if (h == nullptr)
		return nullptr;
	return reinterpret_cast<const HookType*>(reinterpret_cast<uintptr_t>(h) - hook_offset<HookType>::offset);
}


} /* namespace ilias::ll_detail */


class ll_member_hook;
template<typename Tag = void> class ll_base_hook;
template<typename Type, ll_member_hook Type::*MemberPtr> class ll_member;
template<typename Type, typename Tag = void> class ll_base;


class ILIAS_ASYNC_LOCAL ll_member_hook
{
template<typename Type, ll_member_hook Type::*MemberPtr> friend class ll_member;
friend struct ll_detail::hook_offset<ll_member_hook>;

private:
	ll_detail::hook m_hook;

public:
	ll_member_hook() noexcept :
		m_hook()
	{
		/* Empty body. */
	}

	ll_member_hook(const ll_member_hook&) noexcept :
		m_hook()
	{
		/* Empty body. */
	}

	ll_member_hook&
	operator=(const ll_member_hook&) noexcept
	{
		return *this;
	}
};

template<typename Tag>
class ILIAS_ASYNC_LOCAL ll_base_hook
{
template<typename Type, typename TTag> friend class ll_base;
friend struct ll_detail::hook_offset<ll_base_hook<Tag> >;

private:
	ll_detail::hook m_hook;

public:
	ll_base_hook() noexcept :
		m_hook()
	{
		/* Empty body. */
	}

	ll_base_hook(const ll_base_hook&) noexcept :
		m_hook()
	{
		/* Empty body. */
	}

	ll_base_hook&
	operator=(const ll_base_hook&) noexcept
	{
		return *this;
	}
};

template<typename Type, ll_member_hook Type::*MemberPtr>
class ll_member
{
private:
	typedef ll_member_hook hook_type;

public:
	typedef Type value_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;

private:
	static const std::ptrdiff_t offset = reinterpret_cast<std::ptrdiff_t>(&(reinterpret_cast<pointer>(0U)->*MemberPtr));

public:
	static ll_detail::hook*
	hook(pointer p) noexcept
	{
		return (p ? &(p->*MemberPtr).m_hook : nullptr);
	}

	static const ll_detail::hook*
	hook(const_pointer p) noexcept
	{
		return (p ? &(p->*MemberPtr).m_hook : nullptr);
	}

	static pointer
	node(ll_detail::hook *h) noexcept
	{
		hook_type* mh = ll_detail::hook_resolve<hook_type>(h);
		return (mh ? reinterpret_cast<pointer>(reinterpret_cast<std::uintptr_t>(mh) - offset) : nullptr);
	}

	static const_pointer
	node(const ll_detail::hook* h) noexcept
	{
		const hook_type* mh = ll_detail::hook_resolve<hook_type>(h);
		return (mh ? reinterpret_cast<const_pointer>(reinterpret_cast<std::uintptr_t>(mh) - offset) : nullptr);
	}
};

template<typename Type, typename Tag>
class ll_base
{
private:
	typedef ll_base_hook<Tag> hook_type;

public:
	typedef Type value_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;

	static ll_detail::hook*
	hook(pointer p) noexcept
	{
		return &p->hook_type::m_hook;
	}

	static const ll_detail::hook*
	hook(const_pointer p) noexcept
	{
		return &p->hook_type::m_hook;
	}

	static pointer
	node(ll_detail::hook* h) noexcept
	{
		return (h ? static_cast<pointer>(ll_detail::hook_resolve<hook_type>(h)) : nullptr);
	}

	static const_pointer
	node(const ll_detail::hook* h) noexcept
	{
		return (h ? static_cast<const_pointer>(ll_detail::hook_resolve<hook_type>(h)) : nullptr);
	}
};

template<typename Defn>
class ll_list :
	public ll_detail::list
{
private:
	typedef Defn definition_type;

public:
	typedef typename definition_type::value_type value_type;
	typedef typename definition_type::reference reference;
	typedef typename definition_type::const_reference const_reference;
	typedef typename definition_type::pointer pointer;
	typedef typename definition_type::const_pointer const_pointer;

	/* Iterator types. */
	class iterator;
	class const_iterator;
	class reverse_iterator;
	class const_reverse_iterator;
	class unlink_wait;

private:
	/* Aspects of iterators, combined to create actual iterators. */
	template<typename Type, typename Derived> class iterator_resolver;
	template<typename Derived> class iterator_forward_traverse;
	template<typename Derived> class iterator_backward_traverse;

public:
	constexpr ll_list() noexcept { /* Empty body. */ }

	pointer
	pop_front()
	{
		return definition_type::node(this->list::pop_front());
	}

	pointer
	pop_back()
	{
		return definition_type::node(this->list::pop_back());
	}

	iterator
	begin() noexcept
	{
		iterator rv;
		this->ll_detail::list::first(rv);
		return rv;
	}

	iterator
	end() noexcept
	{
		iterator rv;
		this->ll_detail::list::listhead(rv);
		return rv;
	}

	const_iterator
	cbegin() const noexcept
	{
		const_iterator rv;
		this->ll_detail::list::first(rv);
		return rv;
	}

	const_iterator
	begin() const noexcept
	{
		return this->cbegin();
	}

	const_iterator
	cend() const noexcept
	{
		const_iterator rv;
		this->ll_detail::list::listhead(rv);
		return rv;
	}

	const_iterator
	end() const noexcept
	{
		return this->cend();
	}

	reverse_iterator
	rbegin() noexcept
	{
		reverse_iterator rv;
		this->ll_detail::list::last(rv);
		return rv;
	}

	reverse_iterator
	rend() noexcept
	{
		reverse_iterator rv;
		this->ll_detail::list::listhead(rv);
		return rv;
	}

	const_reverse_iterator
	crbegin() const noexcept
	{
		const_reverse_iterator rv;
		this->ll_detail::list::last(rv);
		return rv;
	}

	const_reverse_iterator
	rbegin() const noexcept
	{
		return this->crbegin();
	}

	const_reverse_iterator
	crend() const noexcept
	{
		const_reverse_iterator rv;
		this->ll_detail::list::listhead(rv);
		return rv;
	}

	const_reverse_iterator
	rend() const noexcept
	{
		return  this->crend();
	}

	bool
	erase_element(const iterator& i) noexcept
	{
		return i.simple_iterator::unlink(*this);
	}

	bool
	erase_element(const reverse_iterator& i) noexcept
	{
		return i.simple_iterator::unlink(*this);
	}

	/*
	 * Guarantee the element pointed at by iterator will not be on the list.
	 * Guarantee it will not be inserted (not ever).
	 *
	 * Intended use: call on element prior to running its destructor.
	 *
	 * Returns true if, as part of this call, the element was erased from the list.
	 */
	bool
	unlink_robust(const iterator& i) noexcept
	{
		return i.simple_iterator::unlink_robust(*this);
	}

	iterator
	erase(const iterator& i) noexcept
	{
		this->erase_element(i);
		iterator rv = i;
		++rv;
		return rv;
	}

	iterator
	erase(const reverse_iterator& i) noexcept
	{
		this->erase_element(i);
		iterator rv = i;
		++rv;
		return rv;
	}

	template<typename Dispose>
	iterator
	erase_and_dispose(iterator& i, Dispose dispose)
		/* XXX needs noexcept specification. */
	{
		pointer disposable = nullptr;
		if (this->erase_element(i))
			disposable = i.get();
		iterator rv = i;
		++rv;
		if (disposable) {
			i = iterator();
			dispose(disposable);
		}
		return rv;
	}

	template<typename Dispose>
	reverse_iterator
	erase_and_dispose(reverse_iterator& i, Dispose dispose)
		/* XXX needs noexcept specification. */
	{
		pointer disposable = nullptr;
		if (this->erase_element(i))
			disposable = i.get();
		reverse_iterator rv = i;
		++rv;
		if (disposable) {
			i = reverse_iterator();
			dispose(disposable);
		}
		return rv;
	}

	bool
	push_back_element(value_type* v) noexcept
	{
		return this->list::push_back(ll_detail::hook_ptr(definition_type::hook(v)));
	}

	bool
	push_front_element(value_type* v) noexcept
	{
		return this->list::push_front(ll_detail::hook_ptr(definition_type::hook(v)));
	}

	bool
	push_back(reference v) noexcept
	{
		return this->push_back_element(&v);
	}

	bool
	push_front(reference v) noexcept
	{
		return this->push_front_element(&v);
	}

	unlink_wait
	pop_front_nowait()
	{
		unlink_wait w;
		this->list::pop_front_nowait(w.m_ptr);
		return w;
	}

	unlink_wait
	pop_back_nowait()
	{
		unlink_wait w;
		this->list::pop_back_nowait(w.m_ptr);
		return w;
	}

	void
	push_front(unlink_wait&& w)
	{
		if (!w)
			throw std::invalid_argument("cannot insert empty unlink_wait");
		this->list::push_front_nowait(w.m_ptr);
		w.m_ptr = simple_iterator();
	}

	void
	push_back(unlink_wait&& w)
	{
		if (!w)
			throw std::invalid_argument("cannot insert empty unlink_wait");
		this->list::push_back_nowait(w.m_ptr);
		w.m_ptr = simple_iterator();
	}

	void
	remove(const value_type& v) noexcept
	{
		const iterator e = this->end();
		for (iterator i = this->begin(); i != e; ++i) {
			if (*i == v)
				this->erase_element(i);
		}
	}

	template<typename Disposer>
	void
	remove_and_dispose(const value_type& v, Disposer disposer)
		/* XXX needs dynamic exception specification. */
	{
		const iterator e = this->end();
		iterator i = this->begin();
		while (i != e) {
			value_type* to_dispose = nullptr;
			if (*i == v && this->erase_element(i))
				to_dispose = i.get();
			/* Note that we move iterator out of disposed element prior to dispose operation. */
			++i;
			if (to_dispose)
				disposer(to_dispose);
		}
	}

	template<typename Predicate>
	void
	remove_if(Predicate p)
		/* XXX needs dynamic exception specification. */
	{
		const iterator e = this->end();
		for (iterator i = this->begin(); i != e; ++i) {
			if (p(*i))
				this->erase_element(i);
		}
	}

	template<typename Predicate, typename Disposer>
	void
	remove_and_dispose_if(Predicate p, Disposer disposer)
		/* XXX needs dynamic exception specification. */
	{
		const iterator e = this->end();
		iterator i = this->begin();
		while (i != e) {
			value_type* to_dispose = nullptr;
			if (p(*i) && this->erase_element(i))
				to_dispose = i.get();
			/* Note that we move iterator out of disposed element prior to dispose operation. */
			++i;
			if (to_dispose)
				disposer(to_dispose);
		}
	}

	iterator
	iterator_to(reference v) noexcept
	{
		iterator iter;
		this->list::iter_to(iter, *definition_type::hook(&v));
		return iter;
	}

	const_iterator
	iterator_to(const_reference v) noexcept
	{
		const_iterator iter;
		this->list::iter_to(iter, const_cast<typename ll_detail::hook&>(*definition_type::hook(&v)));
		return iter;
	}

	void
	clear() noexcept
	{
		while (this->pop_front());
	}

	template<typename Disposer>
	void
	clear_and_dispose(Disposer dispose)
		/* XXX needs exception specification. */
	{
		pointer p;
		while ((p = this->pop_front()))
			dispose(p);
	}
};

template<typename Defn>
template<typename Type, typename Derived>
class ll_list<Defn>::iterator_resolver
{
public:
	typedef Type value_type;
	typedef value_type* pointer;
	typedef value_type& reference;

protected:
	iterator_resolver() noexcept
	{
		return;
	}

	iterator_resolver(const iterator_resolver&) noexcept
	{
		return;
	}

	~iterator_resolver() noexcept
	{
		return;
	}

public:
	pointer
	get() const noexcept
	{
		const Derived& self = static_cast<const Derived&>(*this);
		return Defn::node(self.simple_iterator::get_internal().get());
	}

	reference
	operator*() const noexcept
	{
		return *this->get();
	}

	pointer
	operator->() const noexcept
	{
		return this->get();
	}

	bool
	operator==(const typename ll_list<Defn>::iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self == o;
	}

	bool
	operator==(const typename ll_list<Defn>::const_iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self == o;
	}

	bool
	operator==(const typename ll_list<Defn>::reverse_iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self == o;
	}

	bool
	operator==(const typename ll_list<Defn>::const_reverse_iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self == o;
	}

	bool
	operator!=(const typename ll_list<Defn>::iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self != o;
	}

	bool
	operator!=(const typename ll_list<Defn>::const_iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self != o;
	}

	bool
	operator!=(const typename ll_list<Defn>::reverse_iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self != o;
	}

	bool
	operator!=(const typename ll_list<Defn>::const_reverse_iterator& o) const noexcept
	{
		const simple_iterator& self = static_cast<const Derived&>(*this);
		return self != o;
	}
};

template<typename Defn>
template<typename Derived>
class ll_list<Defn>::iterator_forward_traverse
{
public:
	typedef std::ptrdiff_t difference_type;
	typedef std::bidirectional_iterator_tag iterator_category;

protected:
	iterator_forward_traverse() noexcept
	{
		return;
	}

	iterator_forward_traverse(const iterator_forward_traverse&) noexcept
	{
		return;
	}

	~iterator_forward_traverse() noexcept
	{
		return;
	}

public:
	Derived&
	operator++() noexcept
	{
		Derived& self = static_cast<Derived&>(*this);
		self.simple_iterator::step_forward();
		return self;
	}

	Derived
	operator++(int) noexcept
	{
		Derived copy = static_cast<Derived&>(*this);
		++copy;
		return copy;
	}

	Derived&
	operator--() noexcept
	{
		Derived& self = static_cast<Derived&>(*this);
		self.simple_iterator::step_backward();
		return self;
	}

	Derived
	operator--(int) noexcept
	{
		Derived copy = static_cast<Derived&>(*this);
		--copy;
		return copy;
	}
};

template<typename Defn>
template<typename Derived>
class ll_list<Defn>::iterator_backward_traverse
{
public:
	typedef std::ptrdiff_t difference_type;
	typedef std::bidirectional_iterator_tag iterator_category;

protected:
	iterator_backward_traverse() noexcept
	{
		return;
	}

	iterator_backward_traverse(const iterator_backward_traverse&) noexcept
	{
		return;
	}

	~iterator_backward_traverse() noexcept
	{
		return;
	}

public:
	Derived&
	operator--() noexcept
	{
		Derived& self = static_cast<Derived&>(*this);
		self.simple_iterator::step_forward();
		return self;
	}

	Derived
	operator--(int) noexcept
	{
		Derived copy = static_cast<Derived&>(*this);
		--copy;
		return copy;
	}

	Derived&
	operator++() noexcept
	{
		Derived& self = static_cast<Derived&>(*this);
		self.simple_iterator::step_backward();
		return self;
	}

	Derived
	operator++(int) noexcept
	{
		Derived copy = static_cast<Derived&>(*this);
		++copy;
		return copy;
	}
};

template<typename Defn>
class ll_list<Defn>::iterator :
	protected simple_iterator,
	public iterator_resolver<value_type, iterator>,
	public iterator_forward_traverse<iterator>
{
friend class ll_list<Defn>;

public:
	using simple_iterator::difference_type;

	iterator() noexcept :
		simple_iterator()
	{
		/* Empty body. */
	}

	iterator(const iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	iterator(const reverse_iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	iterator(iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	iterator(reverse_iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	iterator&
	operator=(const iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	iterator&
	operator=(const reverse_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	iterator&
	operator=(iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	iterator&
	operator=(reverse_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	using iterator_resolver<value_type, iterator>::operator==;
	using iterator_resolver<value_type, iterator>::operator!=;
};

template<typename Defn>
class ll_list<Defn>::reverse_iterator :
	protected simple_iterator,
	public iterator_resolver<value_type, reverse_iterator>,
	public iterator_backward_traverse<reverse_iterator>
{
friend class ll_list<Defn>;

public:
	using simple_iterator::difference_type;

	reverse_iterator() noexcept :
		simple_iterator()
	{
		/* Empty body. */
	}

	reverse_iterator(const iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	reverse_iterator(const reverse_iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	reverse_iterator(iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	reverse_iterator(reverse_iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	iterator
	base() const noexcept
	{
		return iterator(*this);
	}

	reverse_iterator&
	operator=(const iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	reverse_iterator&
	operator=(const reverse_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	reverse_iterator&
	operator=(iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	reverse_iterator&
	operator=(reverse_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	using iterator_resolver<value_type, reverse_iterator>::operator==;
	using iterator_resolver<value_type, reverse_iterator>::operator!=;
};

template<typename Defn>
class ll_list<Defn>::const_iterator :
	protected simple_iterator,
	public iterator_resolver<const value_type, const_iterator>,
	public iterator_forward_traverse<const_iterator>
{
friend class ll_list<Defn>;

public:
	using simple_iterator::difference_type;

	const_iterator() noexcept :
		simple_iterator()
	{
		/* Empty body. */
	}

	const_iterator(const const_iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_iterator(const const_reverse_iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_iterator(const_iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_iterator(const_reverse_iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_iterator&
	operator=(const iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(const reverse_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(reverse_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(const const_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(const const_reverse_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(const_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_iterator&
	operator=(const_reverse_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	using iterator_resolver<const value_type, const_iterator>::operator==;
	using iterator_resolver<const value_type, const_iterator>::operator!=;
};

template<typename Defn>
class ll_list<Defn>::const_reverse_iterator :
	protected simple_iterator,
	public iterator_resolver<const value_type, const_reverse_iterator>,
	public iterator_backward_traverse<const_reverse_iterator>
{
friend class ll_list<Defn>;

public:
	using simple_iterator::difference_type;

	const_reverse_iterator() noexcept :
		simple_iterator()
	{
		/* Empty body. */
	}

	const_reverse_iterator(const const_iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_reverse_iterator(const const_reverse_iterator& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_reverse_iterator(const_iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_reverse_iterator(const_reverse_iterator&& i) noexcept :
		simple_iterator(i)
	{
		/* Empty body. */
	}

	const_iterator
	base() const noexcept
	{
		return const_iterator(*this);
	}

	const_reverse_iterator&
	operator=(const iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(const reverse_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(reverse_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(const const_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(const const_reverse_iterator& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(const_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	const_reverse_iterator&
	operator=(const_reverse_iterator&& i) noexcept
	{
		this->simple_iterator::operator=(i);
		return *this;
	}

	using iterator_resolver<const value_type, const_reverse_iterator>::operator==;
	using iterator_resolver<const value_type, const_reverse_iterator>::operator!=;
};

template<typename Defn>
class ll_list<Defn>::unlink_wait
{
friend class ll_list<Defn>;

private:
	ll_detail::list::simple_iterator m_ptr;

	unlink_wait(const ll_detail::list::simple_iterator& p) noexcept :
		m_ptr(p)
	{
		/* Empty body. */
	}

	unlink_wait(ll_detail::list::simple_iterator&& p) noexcept :
		m_ptr(p)
	{
		/* Empty body. */
	}

public:
	unlink_wait() noexcept :
		m_ptr()
	{
		/* Empty body. */
	}

	unlink_wait(unlink_wait&& o) noexcept :
		m_ptr(std::move(o.m_ptr))
	{
		/* Empty body. */
	}

	~unlink_wait() noexcept
	{
		this->release();
	}

	unlink_wait&
	operator=(unlink_wait&& o) noexcept
	{
		this->release();
		this->m_ptr = std::move(o.m_ptr);
		return *this;
	}

	pointer
	get() const noexcept
	{
		return Defn::node(this->m_ptr.get_internal().get());
	}

	reference
	operator*() const noexcept
	{
		return *this->get();
	}

	pointer
	operator->() const noexcept
	{
		return this->get();
	}

	pointer
	release() noexcept
	{
		if (this->get())
			this->m_ptr.unlink_wait();
		pointer p = this->get();
		this->m_ptr = simple_iterator();
		return p;
	}

	explicit operator bool() const noexcept
	{
		return this->get();
	}


	unlink_wait(const unlink_wait&) = delete;
	unlink_wait& operator=(const unlink_wait&) = delete;
};


/*
 * Smart pointer list.
 *
 * Elements on the list will be referenced using 'value_type* p = Release(elem)',
 * elements removed from the list will be released using 'SmartPtr p = Acquire(elem)'.
 * I.e. the smart pointer will give up its ownership using Release, while it will
 * steal ownership using Acquire.
 */
template<typename SmartPtr, typename Defn, typename Acquire, typename Release>
class ll_smartptr_list
{
public:
	typedef SmartPtr pointer;

private:
	typedef ll_list<Defn> list_type;

	static pointer
	acquire(typename list_type::pointer p) noexcept
	{
		Acquire impl;
		SmartPtr rv = impl(p);
		return rv;
	}

	static typename list_type::pointer
	release(const pointer& p) noexcept
	{
		Release impl;
		typename list_type::pointer rv = impl(p);
		return rv;
	}

	static typename list_type::pointer
	release(pointer&& p) noexcept
	{
		Release impl;
		typename list_type::pointer rv = impl(std::move(p));
		return rv;
	}

public:
	typedef typename list_type::value_type value_type;
	typedef typename list_type::reference reference;
	typedef typename list_type::const_reference const_reference;

private:
	template<typename IterImpl>
	class iter_adapter :
		public IterImpl
	{
	public:
		typedef typename ll_smartptr_list::pointer pointer;

		iter_adapter() noexcept {};

		iter_adapter(const iter_adapter& i) noexcept :
			IterImpl(i)
		{
			/* Empty body. */
		}

		iter_adapter(iter_adapter&& i) noexcept :
			IterImpl(std::move(i))
		{
			/* Empty body. */
		}

		iter_adapter(const IterImpl& i) noexcept :
			IterImpl(i)
		{
			/* Empty body. */
		}

		iter_adapter(IterImpl&& i) noexcept :
			IterImpl(std::move(i))
		{
			/* Empty body. */
		}

		iter_adapter&
		operator=(const iter_adapter& i) noexcept
		{
			this->IterImpl::operator=(i);
			return *this;
		}

		iter_adapter&
		operator=(iter_adapter&& i) noexcept
		{
			this->IterImpl::operator=(std::move(i));
			return *this;
		}

		template<typename OImpl>
		iter_adapter&
		operator=(const iter_adapter<OImpl>& i) noexcept
		{
			this->IterImpl::operator=(i);
			return *this;
		}

		template<typename OImpl>
		iter_adapter&
		operator=(iter_adapter<OImpl>&& i) noexcept
		{
			this->IterImpl::operator=(std::move(i));
			return *this;
		}

		pointer
		get() const noexcept
		{
			return this->IterImpl::get();
		}
	};

public:
	typedef iter_adapter<typename list_type::iterator> iterator;
	typedef iter_adapter<typename list_type::const_iterator> const_iterator;
	typedef iter_adapter<typename list_type::reverse_iterator> reverse_iterator;
	typedef iter_adapter<typename list_type::const_reverse_iterator> const_reverse_iterator;

	class unlink_wait
	{
	friend void ll_smartptr_list::push_front(unlink_wait&&);
	friend void ll_smartptr_list::push_back(unlink_wait&&);

	public:
		typedef typename ll_smartptr_list::pointer pointer;
		typedef typename ll_smartptr_list::reference reference;

	private:
		typedef typename list_type::unlink_wait impl_type;

		impl_type m_impl;

	public:
		unlink_wait() noexcept
		{
			/* Empty body. */
		}

		unlink_wait(unlink_wait&& o) noexcept :
			m_impl(std::move(o.m_impl))
		{
			/* Empty body. */
		}

		unlink_wait(impl_type&& impl) noexcept :
			m_impl(std::move(impl))
		{
			/* Empty body. */
		}

		~unlink_wait() noexcept
		{
			this->release();
		}

		unlink_wait&
		operator=(unlink_wait&& w) noexcept
		{
			this->m_impl = std::move(w.m_impl);
			return *this;
		}

		pointer
		get() const noexcept
		{
			return this->m_impl.get();
		}

		reference
		operator*() const noexcept
		{
			return this->m_impl.operator*();
		}

		value_type*
		operator->() const noexcept
		{
			return this->m_impl.operator->();
		}

		pointer
		release() noexcept
		{
			value_type* p = this->m_impl.release();
			return (p ? acquire(p) : nullptr);
		}

		explicit operator bool() const noexcept
		{
			return bool(this->m_impl);
		}


		unlink_wait(const unlink_wait&) = delete;
		unlink_wait& operator=(const unlink_wait&) = delete;
	};

private:
	list_type m_list;

public:
	ll_smartptr_list() noexcept { /* Empty body. */ }

	~ll_smartptr_list() noexcept
	{
		this->clear();
	}

	pointer
	pop_front() noexcept
	{
		return acquire(this->m_list.pop_front());
	}

	pointer
	pop_back() noexcept
	{
		return acquire(this->m_list.pop_back());
	}

	bool
	push_front(const pointer& p)
	{
		typename list_type::pointer lp = release(p);
		bool rv = this->m_list.push_front_element(lp);
		if (!rv)
			acquire(lp);
		return rv;
	}

	bool
	push_front(pointer&& p)
	{
		typename list_type::pointer lp = release(std::move(p));
		bool rv = this->m_list.push_front_element(lp);
		if (!rv)
			p = acquire(lp);
		return rv;
	}

	bool
	push_back(const pointer& p)
	{
		typename list_type::pointer lp = release(p);
		bool rv = this->m_list.push_back_element(lp);
		if (!rv)
			acquire(lp);
		return rv;
	}

	bool
	push_back(pointer&& p)
	{
		typename list_type::pointer lp = release(std::move(p));
		bool rv = this->m_list.push_back_element(lp);
		if (!rv)
			p = acquire(lp);
		return rv;
	}

	unlink_wait
	pop_front_nowait()
	{
		return unlink_wait(this->m_list.pop_front_nowait());
	}

	unlink_wait
	pop_back_nowait()
	{
		return unlink_wait(this->m_list.pop_back_nowait());
	}

	void
	push_front(unlink_wait&& w)
	{
		this->m_list.push_front(std::move(w.m_impl));
	}

	void
	push_back(unlink_wait&& w)
	{
		this->m_list.push_back(std::move(w.m_impl));
	}

	iterator
	erase(iterator& i) noexcept
	{
		return this->erase_and_dispose(i, [](const pointer&) {});
	}

	iterator
	erase(iterator&& i) noexcept
	{
		return this->erase_and_dispose(i, [](const pointer&) {});
	}

	template<typename Disposer>
	iterator
	erase_and_dispose(iterator& i, Disposer disposer)
	{
		iterator rv = this->m_list.erase_and_dispose(i, [this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
		return rv;
	}

	template<typename Disposer>
	iterator
	erase_and_dispose(iterator&& i, Disposer disposer)
	{
		iterator rv = this->m_list.erase_and_dispose(i, [this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
		return rv;
	}

	reverse_iterator
	erase(reverse_iterator& i) noexcept
	{
		return this->erase_and_dispose(i, [](const pointer&) {});
	}

	reverse_iterator
	erase(reverse_iterator&& i) noexcept
	{
		return this->erase_and_dispose(i, [](const pointer&) {});
	}

	template<typename Disposer>
	reverse_iterator
	erase_and_dispose(reverse_iterator& i, Disposer disposer)
	{
		reverse_iterator rv = this->m_list.erase_and_dispose(i, [this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
		return rv;
	}

	template<typename Disposer>
	reverse_iterator
	erase_and_dispose(reverse_iterator&& i, Disposer disposer)
	{
		reverse_iterator rv = this->m_list.erase_and_dispose(i, [this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
		return rv;
	}

	/*
	 * Guarantee the element pointed at by iterator will not be on the list.
	 * Guarantee it will not be inserted (not ever).
	 *
	 * Intended use: call on element prior to running its destructor.
	 *
	 * Returns smart pointer if, as part of this call, the element was erased from the list.
	 */
	pointer
	unlink_robust(iterator&& i) noexcept
	{
		pointer p;
		if (this->m_list.unlink_robust(i))
			p = acquire(&*i);	/* Move reference from list to external smart pointer. */
		i = iterator();
		return p;
	}

	void
	remove(const value_type& v) noexcept
	{
		this->remove_and_dispose(v, [](const pointer&) {});
	}

	template<typename Disposer>
	void
	remove_and_dispose(const value_type& v, Disposer disposer)
	{
		this->m_list.remove_and_dispose(v, [this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
	}

	template<typename Predicate>
	void
	remove_if(Predicate p)
	{
		this->remove_and_dispose_if(p, [](const pointer&) {});
	}

	template<typename Predicate, typename Disposer>
	void
	remove_and_dispose_if(Predicate p, Disposer disposer)
	{
		this->m_list.remove_and_dispose_if(p, [this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
	}

	void
	clear() noexcept
	{
		this->clear_and_dispose([](const pointer&) {});
	}

	template<typename Disposer>
	void
	clear_and_dispose(Disposer disposer)
	{
		this->m_list.clear_and_dispose([this, &disposer](typename list_type::pointer r) {
			disposer(ll_smartptr_list::acquire(r));
		});
	}

	iterator
	begin() noexcept
	{
		return iterator(this->m_list.begin());
	}
	iterator
	end() noexcept
	{
		return iterator(this->m_list.end());
	}

	const_iterator
	cbegin() const noexcept
	{
		return const_iterator(this->m_list.begin());
	}
	const_iterator
	begin() const noexcept
	{
		return this->cbegin();
	}
	const_iterator
	cend() const noexcept
	{
		return const_iterator(this->m_list.end());
	}
	const_iterator
	end() const noexcept
	{
		return this->cend();
	}

	reverse_iterator
	rbegin() noexcept
	{
		return reverse_iterator(this->m_list.rbegin());
	}
	reverse_iterator
	rend() noexcept
	{
		return reverse_iterator(this->m_list.rend());
	}

	const_reverse_iterator
	crbegin() const noexcept
	{
		return const_reverse_iterator(this->m_list.rbegin());
	}
	const_reverse_iterator
	rbegin() const noexcept
	{
		return this->crbegin();
	}
	const_reverse_iterator
	crend() const noexcept
	{
		return const_reverse_iterator(this->m_list.rend());
	}
	const_reverse_iterator
	rend() const noexcept
	{
		return this->crend();
	}

	iterator
	iterator_to(reference v)
	{
		return iterator(this->m_list.iterator_to(v));
	}

	const_iterator
	iterator_to(const_reference v)
	{
		return const_iterator(this->m_list.iterator_to(v));
	}

	bool
	empty() const noexcept
	{
		return this->m_list.empty();
	}
};


}


#ifdef _MSC_VER
#pragma warning( pop )
#endif


#endif /* LL_H */
