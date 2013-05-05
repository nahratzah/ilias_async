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

#include <utility>


namespace ilias {


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
