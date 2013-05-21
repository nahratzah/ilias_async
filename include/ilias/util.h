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
#ifndef ILIAS_ASYNC_UTIL_H
#define ILIAS_ASYNC_UTIL_H

#include <thread>
#include <type_traits>
#include <utility>


namespace ilias {
namespace util_detail {
namespace swap_helper{


using std::swap;


namespace {


template<typename Type>
void
do_(Type& a, Type& b)
noexcept(noexcept(swap(a, b)))
{
	swap(a, b);
}


}} /* namespace ilias::util_detail::::swap_helper::<unnamed> */


/*
 * Optional_data may hold data, or it may not.
 */
template<typename Type,
    bool NeedsDestructor = std::is_trivially_destructible<Type>::value>
class optional_data
{
private:
	union data_type {
		Type val;

		~data_type() noexcept {};
	};

	bool m_has_data{ false };
	data_type m_data{};

public:
	optional_data() = default;

	optional_data(const optional_data& o)
	noexcept(std::is_nothrow_copy_constructible<Type>::value)
	:	optional_data()
	{
		if (o.m_has_data) {
			new (&this->m_data.val) Type{
				o.m_data.val
			};
			this->m_has_data = true;
		}
	}

	optional_data(optional_data&& o)
	noexcept(std::is_nothrow_move_constructible<Type>::value)
	:	optional_data()
	{
		if (o.m_has_data) {
			new (&this->m_data.val) Type{
				std::move(o.m_data.val)
			};
			this->m_has_data = true;
			o.reset();
		}
	}

	optional_data(const Type& v)
	noexcept(std::is_nothrow_copy_constructible<Type>::value)
	:	optional_data()
	{
		new (&this->m_data.val) Type{ v };
		this->m_has_data = true;
	}

	optional_data(Type&& v)
	noexcept(std::is_nothrow_move_constructible<Type>::value)
	:	optional_data()
	{
		new (&this->m_data.val) Type{ std::move(v) };
		this->m_has_data = true;
	}

	~optional_data()
	noexcept(std::is_nothrow_destructible<Type>::value)
	{
		this->reset();
	}

	void
	reset()
	noexcept(std::is_nothrow_destructible<Type>::value)
	{
		if (this->m_has_data) {
			this->m_data.val.~Type();
			this->m_has_data = false;
		}
	}

	void
	reset(const Type& v)
	noexcept(
		std::is_nothrow_copy_constructible<Type>::value &&
		std::is_nothrow_assignable<Type, const Type&>::value)
	{
		if (this->m_has_data)
			this->m_data.val = v;
		else {
			new (&this->m_data.val) Type{ v };
			this->m_has_data = true;
		}
	}

	void
	reset(Type&& v)
	noexcept(
		std::is_nothrow_move_constructible<Type>::value &&
		std::is_nothrow_assignable<Type, Type&&>::value)
	{
		if (this->m_has_data)
			this->m_data.val = std::move(v);
		else {
			new (&this->m_data.val) Type{ v };
			this->m_has_data = true;
		}
	}

	optional_data&
	operator=(const optional_data& o)
	noexcept(
		noexcept(std::declval<optional_data>().reset()) &&
		noexcept(std::declval<optional_data>().reset(
		    std::declval<const Type&>())))
	{
		if (!o.m_has_data)
			this->reset();
		else
			this->reset(o.m_data.val);
		return *this;
	}

	optional_data&
	operator=(optional_data&& o)
	noexcept(
		noexcept(std::declval<optional_data>().reset()) &&
		noexcept(std::declval<optional_data>().reset(
		    std::declval<Type&&>())))
	{
		if (!o.m_has_data)
			this->reset();
		else {
			this->reset(std::move(o.m_data.val));
			o.reset();
		}
		return *this;
	}

	bool
	operator==(const optional_data& o) const
	noexcept(
		noexcept(std::declval<const Type&>() ==
		    std::declval<const Type&>()))
	{
		if (this->m_has_data && o.m_has_data)
			return this->m_data.val == o.m_data.val;
		return this->m_has_data == o.m_has_data;
	}

	explicit operator bool() const noexcept
	{
		return this->m_has_data;
	}

	friend void
	swap(optional_data& a, optional_data& b)
	noexcept(
		std::is_nothrow_assignable<optional_data,
		    optional_data&&>::value &&
		noexcept(swap_helper::do_(std::declval<Type&>(),
		    std::declval<Type&>())))
	{
		if (a.m_has_data && b.m_has_data)
			swap_helper::do_(a.m_data.val, b.m_data.val);
		else if (a.m_has_data)
			b = std::move(a);
		else if (b.m_has_data)
			a = std::move(b);
	}

	Type*
	get() const noexcept
	{
		return (this->m_has_data ?
		    &const_cast<Type&>(this->m_data.val) :
		    nullptr);
	}

	Type*
	operator->() const noexcept
	{
		return this->get();
	}

	Type&
	operator*() const noexcept
	{
		return *this->get();
	}
};

/*
 * Specialization for trivially destructible Type:
 * ensures optional_data is trivially destructible as well.
 */
template<typename Type>
class optional_data<Type, true>
{
private:
	union data_type {
		Type val;
	};

	bool m_has_data{ false };
	data_type m_data{};

public:
	optional_data() = default;

	optional_data(const optional_data& o)
	noexcept(std::is_nothrow_copy_constructible<Type>::value)
	:	optional_data()
	{
		if (o.m_has_data) {
			new (&this->m_data.val) Type{
				o.m_data.val
			};
			this->m_has_data = true;
		}
	}

	optional_data(optional_data&& o)
	noexcept(std::is_nothrow_move_constructible<Type>::value)
	:	optional_data()
	{
		if (o.m_has_data) {
			new (&this->m_data.val) Type{
				std::move(o.m_data.val)
			};
			this->m_has_data = true;
			o.reset();
		}
	}

	optional_data(const Type& v)
	noexcept(std::is_nothrow_copy_constructible<Type>::value)
	:	optional_data()
	{
		new (&this->m_data.val) Type{ v };
		this->m_has_data = true;
	}

	optional_data(Type&& v)
	noexcept(std::is_nothrow_move_constructible<Type>::value)
	:	optional_data()
	{
		new (&this->m_data.val) Type{ std::move(v) };
		this->m_has_data = true;
	}

	void
	reset()
	noexcept(std::is_nothrow_destructible<Type>::value)
	{
		if (this->m_has_data) {
			this->m_data.val.~Type();
			this->m_has_data = false;
		}
	}

	void
	reset(const Type& v)
	noexcept(
		std::is_nothrow_copy_constructible<Type>::value &&
		std::is_nothrow_assignable<Type, const Type&>::value)
	{
		if (this->m_has_data)
			this->m_data.val = v;
		else {
			new (&this->m_data.val) Type{ v };
			this->m_has_data = true;
		}
	}

	void
	reset(Type&& v)
	noexcept(
		std::is_nothrow_move_constructible<Type>::value &&
		std::is_nothrow_assignable<Type, Type&&>::value)
	{
		if (this->m_has_data)
			this->m_data.val = std::move(v);
		else {
			new (&this->m_data.val) Type{ v };
			this->m_has_data = true;
		}
	}

	optional_data&
	operator=(const optional_data& o)
	noexcept(
		noexcept(std::declval<optional_data>().reset()) &&
		noexcept(std::declval<optional_data>().reset(
		    std::declval<const Type&>())))
	{
		if (!o.m_has_data)
			this->reset();
		else
			this->reset(o.m_data.val);
		return *this;
	}

	optional_data&
	operator=(optional_data&& o)
	noexcept(
		noexcept(std::declval<optional_data>().reset()) &&
		noexcept(std::declval<optional_data>().reset(
		    std::declval<Type&&>())))
	{
		if (!o.m_has_data)
			this->reset();
		else {
			this->reset(std::move(o.m_data.val));
			o.reset();
		}
		return *this;
	}

	bool
	operator==(const optional_data& o) const
	noexcept(
		noexcept(std::declval<const Type&>() ==
		    std::declval<const Type&>()))
	{
		if (this->m_has_data && o.m_has_data)
			return this->m_data.val == o.m_data.val;
		return this->m_has_data == o.m_has_data;
	}

	explicit operator bool() const noexcept
	{
		return this->m_has_data;
	}

	friend void
	swap(optional_data& a, optional_data& b)
	noexcept(
		std::is_nothrow_assignable<optional_data,
		    optional_data&&>::value &&
		noexcept(swap_helper::do_(std::declval<Type&>(),
		    std::declval<Type&>())))
	{
		if (a.m_has_data && b.m_has_data)
			swap_helper::do_(a.m_data.val, b.m_data.val);
		else if (a.m_has_data)
			b = std::move(a);
		else if (b.m_has_data)
			a = std::move(b);
	}

	Type*
	get() const noexcept
	{
		return (this->m_has_data ?
		    &const_cast<Type&>(this->m_data.val) :
		    nullptr);
	}

	Type*
	operator->() const noexcept
	{
		return this->get();
	}

	Type&
	operator*() const noexcept
	{
		return *this->get();
	}
};


namespace {


/*
 * Guard, performs temporary unlock on lockable.
 *
 * Note: this only unlocks once, so recursive lockables may
 * still be locked during the lifetime of this object.
 */
template<typename Lockable>
class unlock_guard
{
private:
	Lockable& l;

public:
	unlock_guard(Lockable& l)
	noexcept(
		noexcept(l.unlock()))
	:	l(l)
	{
		this->l.unlock();
	}

	unlock_guard(const unlock_guard&) = delete;
	unlock_guard(unlock_guard&&) = delete;
	unlock_guard& operator=(const unlock_guard&) = delete;

	~unlock_guard()
	noexcept(
		noexcept(l.lock()))
	{
		this->l.lock();
	}
};


}} /* namespace ilias::util_detail::<unnamed> */


/*
 * Perform function with the lockable locked.
 */
template<typename Lockable, typename Fn, typename... Args>
auto
do_locked(Lockable& lockable, Fn& fn, Args&&... args)
noexcept(
	noexcept(fn(std::forward<Args>(args)...)) &&
	std::is_nothrow_constructible<
	    std::lock_guard<Lockable>, Lockable&>::value &&
	std::is_nothrow_destructible<std::lock_guard<Lockable>>::value)
->	decltype(fn(std::forward<Args>(args)...))
{
	std::lock_guard<Lockable> guard{ lockable };
	return fn(std::forward<Args>(args)...);
}

template<typename Lockable, typename Fn, typename... Args>
auto
do_locked(Lockable& lockable, Fn&& fn, Args&&... args)
noexcept(
	noexcept(fn(std::forward<Args>(args)...)) &&
	std::is_nothrow_constructible<
	    std::lock_guard<Lockable>, Lockable&>::value &&
	std::is_nothrow_destructible<std::lock_guard<Lockable>>::value)
->	decltype(fn(std::forward<Args>(args)...))
{
	std::lock_guard<Lockable> guard{ lockable };
	return fn(std::forward<Args>(args)...);
}


/*
 * Perform function with the lockable unlocked.
 *
 * Note: this function only unlocks once, so the lockable may still be
 * locked if it is a recursive lockable.
 */
template<typename Lockable, typename Fn, typename... Args>
auto
do_unlocked(Lockable& lockable, Fn& fn, Args&&... args)
noexcept(
	noexcept(fn(std::forward<Args>(args)...)) &&
	std::is_nothrow_constructible<
	    util_detail::unlock_guard<Lockable>, Lockable&>::value &&
	std::is_nothrow_destructible<
	    util_detail::unlock_guard<Lockable>>::value)
->	decltype(fn(std::forward<Args>(args)...))
{
	using namespace util_detail;

	unlock_guard<Lockable> guard{ lockable };
	return fn(std::forward<Args>(args)...);
}

template<typename Lockable, typename Fn, typename... Args>
auto
do_unlocked(Lockable& lockable, Fn&& fn, Args&&... args)
noexcept(
	noexcept(fn(std::forward<Args>(args)...)) &&
	std::is_nothrow_constructible<
	    util_detail::unlock_guard<Lockable>, Lockable&>::value &&
	std::is_nothrow_destructible<
	    util_detail::unlock_guard<Lockable>>::value)
->	decltype(fn(std::forward<Args>(args)...))
{
	using namespace util_detail;

	unlock_guard<Lockable> guard{ lockable };
	return fn(std::forward<Args>(args)...);
}


/*
 * Wrap a function in a noexcept specification.
 *
 * Mostly documentary, but allows constructs such as:
 *
 * do_noexcept([&]() {
 *         ...
 *         ...
 *     });
 *
 * Which will ensure the statements inside the scope will not throw.
 */
template<typename Fn, typename... Args>
auto
do_noexcept(Fn& fn, Args&&... args) noexcept
-> decltype(fn(std::forward<Args>(args)...))
{
	return fn(std::forward<Args>(args)...);
}

template<typename Fn, typename... Args>
auto
do_noexcept(Fn&& fn, Args&&... args) noexcept
-> decltype(fn(std::forward<Args>(args)...))
{
	return fn(std::forward<Args>(args)...);
}


/* Object holding optional data. */
template<typename Type> using opt_data = util_detail::optional_data<Type>;


} /* namespace ilias */


#endif /* ILIAS_ASYNC_UTIL_H */
