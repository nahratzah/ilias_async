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


} /* namespace ilias */


#endif /* ILIAS_ASYNC_UTIL_H */
