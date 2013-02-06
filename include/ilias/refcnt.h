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
	refcnt_acquire(const Derived& o) noexcept
	{
		const refcount_base& self = o;
		self.m_refcount.fetch_add(1, std::memory_order_acquire);
	}

	friend void
	refcnt_release(const Derived& o)
		noexcept(
		    noexcept(m_deleter(&o)) &&
		    (std::is_nothrow_move_constructible<Deleter>::value ||
		     std::is_nothrow_copy_constructible<Deleter>::value) &&
		    std::is_nothrow_destructible<Deleter>::value)
	{
		const refcount_base& self = o;
		if (self.m_refcount.fetch_sub(1,
		    std::memory_order_release) == 1) {
			Deleter deleter = std::move_if_noexcept(self.m_deleter);
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
	void
	acquire(const Type& v) noexcept
	{
		refcnt_acquire(v);
	}

	void
	release(const Type& v) noexcept
	{
		refcnt_release(v);
	}
};

template<typename Type, typename AcqRel = default_refcount_mgr<Type> >
class refpointer :
	private AcqRel
{
public:
	typedef Type element_type;
	typedef element_type* pointer;
	typedef element_type& reference;

private:
	pointer m_ptr;

	/* Shortcuts to avoid lengthy no-except specifiers. */
	static constexpr bool noexcept_acquire = noexcept(AcqRel::acquire);
	static constexpr bool noexcept_release = noexcept(AcqRel::release);
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
			this->AcqRel::release(*tmp);
	}

	void
	reset(const refpointer& o) noexcept(
	    noexcept_acqrel)
	{
		using std::swap;

		pointer tmp = o.m_ptr;
		if (tmp)
			this->AcqRel::acquire(*tmp);
		swap(tmp, this->m_ptr);
		if (tmp)
			this->AcqRel::release(*tmp);
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
			this->AcqRel::release(*old);
	}

	void
	reset(pointer p, bool do_acquire = true) noexcept(
	    noexcept_acqrel)
	{
		using std::swap;

		if (do_acquire && p)
			this->AcqRel::acquire(*p);
		pointer tmp = p;
		swap(tmp, this->m_ptr);
		if (tmp)
			this->AcqRel::release(*tmp);
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
	    noexcept(this->reset()))
	{
		this->reset();
		return *this;
	}

	refpointer&
	operator=(const refpointer& o) noexcept(
	    noexcept(this->reset(o)))
	{
		this->reset(o);
		return *this;
	}

	refpointer&
	operator=(refpointer&& o) noexcept(
	    noexcept(this->reset(o)))
	{
		this->reset(o);
		return *this;
	}

	refpointer&
	operator=(pointer p) noexcept(
	    noexcept(this->reset(p)))
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

	template<typename U>
	bool
	operator!=(const U& o) const noexcept
	{
		return !(*this == o);
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
};


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


} /* namespace ilias */


#ifdef _MSC_VER
#pragma warning( pop )
#endif


#endif /* ILIAS_REFCNT_H */
