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
#ifndef ILIAS_REFCNT_H
#define ILIAS_REFCNT_H

#include <ilias/ilias_async_export.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <utility>
#include <type_traits>


#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable: 4800 )
#endif


namespace ilias {


namespace refpointer_detail {

struct atom_lck
{
public:
	struct impl;

private:
	impl& m_impl;

public:
	ILIAS_ASYNC_EXPORT atom_lck(const void*) noexcept;
	ILIAS_ASYNC_EXPORT ~atom_lck() noexcept;

	atom_lck(const atom_lck&) = delete;
	atom_lck(atom_lck&&) = delete;
	atom_lck& operator=(const atom_lck&) = delete;
};

} /* namespace ilias::refpointer_detail */


/*
 * Reference counted base class.
 *
 * Derived: derived type of the class.
 * Deleter: deletion invocation on release of last reference.
 */
template<typename Derived,
    typename Deleter = std::default_delete<const Derived> >
class refcount_base
{
private:
	mutable std::atomic<unsigned int> m_refcount;
	Deleter m_deleter;

protected:
	refcount_base() noexcept
	:	m_refcount(0),
		m_deleter()
	{
		/* Empty body. */
	}

	refcount_base(const Deleter& m_deleter)
		noexcept(std::is_nothrow_copy_constructible<Deleter>::value)
	:	m_refcount(0),
		m_deleter(m_deleter)
	{
		/* Empty body. */
	}

	refcount_base(Deleter&& m_deleter)
		noexcept(std::is_nothrow_move_constructible<Deleter>::value)
	:	m_refcount(0),
		m_deleter(std::move(m_deleter))
	{
		/* Empty body. */
	}

	refcount_base(const refcount_base&) noexcept :
		refcount_base()
	{
		/* Empty body. */
	}

	~refcount_base() noexcept
	{
		assert(this->m_refcount.load(std::memory_order_seq_cst) == 0);
	}

	refcount_base&
	operator=(const refcount_base&) noexcept
	{
		return *this;
	}

	friend void
	refcnt_acquire(const Derived& o, unsigned int nrefs = 1U) noexcept
	{
		if (nrefs == 0)
			return;

		const refcount_base& self = o;
		self.m_refcount.fetch_add(nrefs, std::memory_order_acquire);
	}

	friend void
	refcnt_release(const Derived& o, unsigned int nrefs = 1U)
		noexcept(
		    noexcept((*(Deleter*)nullptr)(&o)) &&
		    (std::is_nothrow_move_constructible<Deleter>::value ||
		     std::is_nothrow_copy_constructible<Deleter>::value) &&
		    std::is_nothrow_destructible<Deleter>::value)
	{
		if (nrefs == 0)
			return;

		const refcount_base& self = o;
		if (self.m_refcount.fetch_sub(nrefs,
		    std::memory_order_release) == nrefs) {
			std::atomic_thread_fence(std::memory_order_acq_rel);
			Deleter deleter =
			    std::move_if_noexcept(self.m_deleter);
			deleter(&o);
		}
	}

	/* Returns true if only one active reference exists to o. */
	friend bool
	refcnt_is_solo(const Derived& o) noexcept
	{
		const refcount_base& self = o;
		return (self.m_refcount.load(std::memory_order_relaxed) == 1);
	}

	friend bool
	refcnt_is_zero(const Derived& o) noexcept
	{
		const refcount_base& self = o;
		return (self.m_refcount.load(std::memory_order_relaxed) == 0);
	}
};

template<typename Type>
struct default_refcount_mgr
{
	static void
	acquire(const Type& v, unsigned int nrefs) noexcept
	{
		refcnt_acquire(v, nrefs);
	}

	static void
	release(const Type& v, unsigned int nrefs) noexcept
	{
		refcnt_release(v, nrefs);
	}
};

template<typename Type, typename AcqRel = default_refcount_mgr<Type> >
class refpointer
{
public:
	using element_type = Type;
	using pointer = element_type*;
	using reference = element_type&;

private:
	using const_reference = const element_type&;

	pointer m_ptr;

	/* Shortcuts to avoid lengthy no-except specifiers. */
	static constexpr bool noexcept_acquire = noexcept(
		AcqRel::acquire(std::declval<const_reference>(), 1U)
	    );
	static constexpr bool noexcept_release = noexcept(
		AcqRel::release(std::declval<const_reference>(), 1U)
	    );
	static constexpr bool noexcept_acqrel =
	    noexcept_acquire && noexcept_release;

public:
	constexpr refpointer() noexcept
	:	m_ptr(nullptr)
	{
		/* Empty body. */
	}

	constexpr refpointer(std::nullptr_t, bool = true) noexcept
	:	m_ptr(nullptr)
	{
		/* Empty body. */
	}

	refpointer(const refpointer& o) noexcept(
	    noexcept_acquire)
	:	m_ptr(nullptr)
	{
		this->reset(o);
	}

	refpointer(refpointer&& o) noexcept
	:	m_ptr(nullptr)
	{
		std::swap(this->m_ptr, o.m_ptr);
	}

	template<typename U, typename U_AcqRel>
	refpointer(const refpointer<U, U_AcqRel>& o) noexcept(
	    noexcept_acquire)
	:	m_ptr(nullptr)
	{
		this->reset(o.get());
	}

	refpointer(pointer p, bool do_acquire = true) noexcept(
	    noexcept_acquire)
	:	m_ptr(nullptr)
	{
		this->reset(p, do_acquire);
	}

	~refpointer() noexcept
	{
		this->reset();
	}

	void
	reset() noexcept(
	    noexcept_release)
	{
		using std::swap;

		pointer tmp = nullptr;
		swap(tmp, this->m_ptr);
		if (tmp)
			AcqRel::release(*tmp, 1U);
	}

	void
	reset(const refpointer& o) noexcept(
	    noexcept_acqrel)
	{
		using std::swap;

		pointer tmp = o.m_ptr;
		if (tmp)
			AcqRel::acquire(*tmp, 1U);
		swap(tmp, this->m_ptr);
		if (tmp)
			AcqRel::release(*tmp, 1U);
	}

	void
	reset(refpointer&& o) noexcept(
	    noexcept_release)
	{
		using std::swap;

		pointer old = nullptr;
		swap(old, this->m_ptr);
		swap(this->m_ptr, o.m_ptr);

		if (old)
			AcqRel::release(*old, 1U);
	}

	void
	reset(pointer p, bool do_acquire = true) noexcept(
	    noexcept_acqrel)
	{
		using std::swap;

		if (do_acquire && p)
			AcqRel::acquire(*p, 1U);
		pointer tmp = p;
		swap(tmp, this->m_ptr);
		if (tmp)
			AcqRel::release(*tmp, 1U);
	}

	template<typename U, typename U_AcqRel>
	void
	reset(const refpointer<U, U_AcqRel>& o) noexcept(
	    noexcept_acqrel)
	{
		this->reset(o.get(), true);
	}

	refpointer&
	operator=(std::nullptr_t) noexcept(
	    noexcept_release)
	{
		this->reset();
		return *this;
	}

	refpointer&
	operator=(const refpointer& o) noexcept(
	    noexcept_acqrel)
	{
		this->reset(o);
		return *this;
	}

	refpointer&
	operator=(refpointer&& o) noexcept(
	    noexcept_release)
	{
		this->reset(std::move(o));
		return *this;
	}

	refpointer&
	operator=(pointer p) noexcept(
	    noexcept_acqrel)
	{
		this->reset(p);
		return *this;
	}

	bool
	operator==(const refpointer& o) const noexcept
	{
		return (this->get() == o.get());
	}

	template<typename U>
	bool
	operator==(const refpointer<U>& o) const noexcept
	{
		return (this->get() == o.get());
	}

	template<typename Ptr>
	bool
	operator==(Ptr* p) const noexcept
	{
		return (this->get() == p);
	}

	friend bool
	operator==(const pointer& a, const refpointer& b) noexcept
	{
		return b == a;
	}

	template<typename U>
	bool
	operator!=(const U& o) const noexcept
	{
		return !(*this == o);
	}

	friend bool
	operator!=(const pointer& a, const refpointer& b) noexcept
	{
		return b != a;
	}

	explicit operator bool() const noexcept
	{
		return this->get();
	}

	pointer
	get() const noexcept
	{
		return this->m_ptr;
	}

	pointer
	release() noexcept
	{
		using std::swap;

		pointer rv = nullptr;
		swap(rv, this->m_ptr);
		return rv;
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

	void
	swap(refpointer& o) noexcept
	{
		using std::swap;

		swap(this->m_ptr, o.m_ptr);
	}

	friend void
	swap(refpointer& lhs, refpointer& rhs) noexcept
	{
		lhs.swap(rhs);
	}

	/*
	 * Atomic primitives.
	 */
	friend bool
	atomic_is_lock_free(const refpointer*) noexcept
	{
		return false;
	}

	friend refpointer
	atomic_load(const refpointer* p) noexcept(noexcept_acquire)
	{
		refpointer_detail::atom_lck lck{ p };
		return *p;
	}

	friend void
	atomic_store(refpointer* p, refpointer v) noexcept(noexcept_release)
	{
		refpointer_detail::atom_lck lck{ p };
		*p = std::move(v);
	}

	friend refpointer
	atomic_exchange(refpointer* p, refpointer v) noexcept
	{
		{
			refpointer_detail::atom_lck lck{ p };
			p->swap(v);
		}
		return v;
	}

	friend bool
	atomic_compare_exchange_strong(refpointer* p, refpointer* expect,
	    refpointer v) noexcept(noexcept_acqrel)
	{
		refpointer_detail::atom_lck lck{ p };
		if (*p == *expect) {
			*p = std::move(v);
			return true;
		}
		*expect = *p;
		return false;
	}

	friend bool
	atomic_compare_exchange_weak(refpointer* p, refpointer* expect,
	    refpointer v) noexcept(noexcept_acqrel)
	{
		return atomic_compare_exchange_strong(p, expect, std::move(v));
	}
};


/*
 * Create a new refpointer holding type.
 */
template<typename Type, typename AcqRel = default_refcount_mgr<Type>,
    typename... Args>
refpointer<Type, AcqRel>
make_refpointer(Args&&... args)
{
	return new Type(std::forward<Args>(args)...);
}


template<typename U, typename T, typename AcqRel>
refpointer<U, AcqRel>
static_pointer_cast(const refpointer<T, AcqRel>& p) noexcept
{
	return refpointer<U, AcqRel>(
	    static_cast<typename refpointer<U, AcqRel>::pointer>(p.get()));
}

template<typename U, typename T, typename AcqRel>
refpointer<U, AcqRel>
static_pointer_cast(refpointer<T, AcqRel>&& p) noexcept
{
	return refpointer<U, AcqRel>(
	    static_cast<typename refpointer<U, AcqRel>::pointer>(p.release()),
	    false);
}


template<typename U, typename T, typename AcqRel>
refpointer<U, AcqRel>
dynamic_pointer_cast(const refpointer<T, AcqRel>& p) noexcept
{
	return refpointer<U, AcqRel>(
	    dynamic_cast<typename refpointer<U, AcqRel>::pointer>(p.get()));
}


template<typename U, typename T, typename AcqRel>
refpointer<U, AcqRel>
const_pointer_cast(const refpointer<T, AcqRel>& p) noexcept
{
	return refpointer<U, AcqRel>(
	    const_cast<typename refpointer<U, AcqRel>::pointer>(p.get()));
}

template<typename U, typename T, typename AcqRel>
refpointer<U, AcqRel>
const_pointer_cast(refpointer<T, AcqRel>&& p) noexcept
{
	return refpointer<U, AcqRel>(
	    const_cast<typename refpointer<U, AcqRel>::pointer>(p.release()),
	    false);
}


template<typename Type, typename AcqRel = default_refcount_mgr<Type> >
struct refpointer_acquire
{
	refpointer<Type, AcqRel>
	operator()(Type* p) const noexcept
	{
		return refpointer<Type, AcqRel>(p, false);
	}
};

template<typename Type, typename AcqRel = default_refcount_mgr<Type> >
struct refpointer_release
{
	Type*
	operator()(refpointer<Type, AcqRel> p) const noexcept
	{
		return p.release();
	}
};


template<typename T, typename A>
refpointer<T, A>
atomic_load_explicit(const refpointer<T, A>* p, std::memory_order)
noexcept(noexcept(atomic_load(p)))
{
	return atomic_load(p);
}

template<typename T, typename A>
void
atomic_store_explicit(const refpointer<T, A>* p, refpointer<T, A> v,
    std::memory_order)
noexcept(noexcept(atomic_store(p, std::move(v))))
{
	atomic_store(p, std::move(v));
}

template<typename T, typename A>
refpointer<T, A>
atomic_exchange_explicit(const refpointer<T, A>* p, refpointer<T, A> v,
    std::memory_order)
noexcept(noexcept(atomic_exchange(p, std::move(v))))
{
	return atomic_exchange(p, std::move(v));
}

template<typename T, typename A>
bool
atomic_compare_exchange_strong_explicit(const refpointer<T, A>* p,
    refpointer<T, A>* expect, refpointer<T, A> v,
    std::memory_order, std::memory_order)
noexcept(noexcept(atomic_compare_exchange_strong(p, expect, std::move(v))))
{
	return atomic_compare_exchange_strong(p, expect, std::move(v));
}

template<typename T, typename A>
bool
atomic_compare_exchange_weak_explicit(const refpointer<T, A>* p,
    refpointer<T, A>* expect, refpointer<T, A> v,
    std::memory_order, std::memory_order)
noexcept(noexcept(atomic_compare_exchange_weak(p, expect, std::move(v))))
{
	return atomic_compare_exchange_weak(p, expect, std::move(v));
}


} /* namespace ilias */


#ifdef _MSC_VER
#pragma warning( pop )
#endif


#endif /* ILIAS_REFCNT_H */
