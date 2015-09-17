/*
 * Copyright (c) 2014 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef _ILIAS_DETAIL_INVOKE_H_
#define _ILIAS_DETAIL_INVOKE_H_

#include <type_traits>
#include <utility>

namespace ilias {
namespace detail {


using std::enable_if_t;
using std::forward;
using std::is_same;
using std::is_base_of;
using std::remove_cv_t;
using std::remove_reference_t;


/*
 * INVOKE:  (t1.*f)(t2, ..., tN)
 * when f is a pointer to member function of a class T and
 * - t1 is an object of type T or
 * - t1 is a reference to an object of type T or
 * - t1 is a reference to an object derived from T.
 */
template<typename F, typename T, typename... Args, typename T1, typename... TT>
auto invoke(F (T::*f)(Args...), T1&& t1, TT&&... tt)
    noexcept(noexcept((forward<T1>(t1).*f)(forward<TT>(tt)...))) ->
    enable_if_t<(is_same<T, remove_cv_t<remove_reference_t<T1>>>::value ||
                 is_base_of<T, remove_cv_t<remove_reference_t<T1>>>::value),
                decltype((forward<T1>(t1).*f)(forward<TT>(tt)...))> {
  return (forward<T1>(t1).*f)(forward<TT>(tt)...);
}

/*
 * INVOKE:  ((*t1).*f)(t2, ..., tN)
 * when f is a pointer to member function of a class T and
 * t1 is not one of the types described in the previous item.
 */
template<typename F, typename T, typename... Args, typename T1, typename... TT>
auto invoke(F (T::*f)(Args...), T1&& t1, TT&&... tt)
    noexcept(noexcept(((*forward<T1>(t1)).*f)(forward<TT>(tt)...))) ->
    enable_if_t<!(is_same<T, remove_cv_t<remove_reference_t<T1>>>::value ||
                  is_base_of<T, remove_cv_t<remove_reference_t<T1>>>::value),
                decltype(((*forward<T1>(t1)).*f)(forward<TT>(tt)...))> {
  return ((*forward<T1>(t1)).*f)(forward<TT>(tt)...);
}

/*
 * INVOKE:  t1.*f
 * when N == 1 and f is a pointer to member data of a class T and
 * - t1 is an object of type T or
 * - t1 is a reference to an object of type T or
 * - t1 is a reference to an object derived from T.
 */
template<typename F, typename T, typename T1>
auto invoke(F T::*f, T1&& t1)
    noexcept(noexcept(forward<T1>(t1).*f)) ->
    enable_if_t<(is_same<T, remove_cv_t<remove_reference_t<T1>>>::value ||
                 is_base_of<T, remove_cv_t<remove_reference_t<T1>>>::value),
                decltype(forward<T1>(t1).*f)> {
  return forward<T1>(t1).*f;
}

/*
 * INVOKE:  (*t1).*f
 * when N == 1 and f is a pointer to member data of a class T and
 * t1 is not one of the types described in the previous item.
 */
template<typename F, typename T, typename T1>
auto invoke(F T::*f, T1&& t1)
    noexcept(noexcept((*forward<T1>(t1)).*f)) ->
    enable_if_t<!(is_same<T, remove_cv_t<remove_reference_t<T1>>>::value ||
                  is_base_of<T, remove_cv_t<remove_reference_t<T1>>>::value),
                decltype((*forward<T1>(t1)).*f)> {
  return (*forward<T1>(t1)).*f;
}

/*
 * INVOKE:  f(t1, t2, ..., tN)
 * in all other cases.
 */
template<typename F, typename... Args>
auto invoke(F&& f, Args&&... args)
    noexcept(noexcept(f(forward<Args>(args)...))) ->
    decltype(f(forward<Args>(args)...)) {
  return f(forward<Args>(args)...);
}


}} /* namespace ilias::detail */

#endif /* _ILIAS_DETAIL_INVOKE_H_ */
