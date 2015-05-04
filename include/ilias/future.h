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
#ifndef _ILIAS_PROMISE_H_
#define _ILIAS_PROMISE_H_

#include <ilias/ilias_async_export.h>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
#include <ilias/detail/invoke.h>
#include <ilias/workq.h>

namespace ilias {


template<typename> class cb_promise;
template<typename> class cb_future;
template<typename> class shared_cb_future;
template<typename, typename> class pass_promise_t;


namespace impl {

ILIAS_ASYNC_EXPORT void __throw(std::future_errc)
    __attribute__((__noreturn__));

template<typename>
struct _is_pass_promise : std::false_type {};
template<typename T, typename U>
struct _is_pass_promise<pass_promise_t<T, U>> : std::true_type {};
template<typename T> using is_pass_promise =
    typename _is_pass_promise<std::remove_cv_t<T>>::type;

template<typename> struct _is_future : std::false_type {};
template<typename T> struct _is_future<cb_future<T>> : std::true_type {};
template<typename T> struct _is_future<std::future<T>> : std::true_type {};
template<typename T> struct _is_future<shared_cb_future<T>>
: std::true_type {};
template<typename T> struct _is_future<std::shared_future<T>>
: std::true_type {};

template<typename> struct _is_promise : std::false_type {};
template<typename T> struct _is_promise<cb_promise<T>> : std::true_type {};
template<typename T> struct _is_promise<std::promise<T>> : std::true_type {};

template<typename> struct _is_startable_future : std::false_type {};
template<typename T> struct _is_startable_future<cb_future<T>>
: std::true_type {};
template<typename T> struct _is_startable_future<shared_cb_future<T>>
: std::true_type {};


template<typename> class shared_state;
template<typename, typename> class shared_state_nofn;
template<typename, typename, typename, typename...> class shared_state_fn;
template<typename> class shared_state_task;
template<typename, typename, typename> class shared_state_task_impl;
template<typename T, typename... Args, typename Alloc, typename Fn>
class shared_state_task_impl<T(Args...), Alloc, Fn>;

template<typename T, typename Alloc>
std::shared_ptr<shared_state<T>> allocate_shared_state(const Alloc&);

template<typename T, typename Alloc, typename Fn, typename... Args>
std::shared_ptr<shared_state<T>> allocate_shared_state(const Alloc&,
                                                       Fn&&, Args&&...);

template<typename TArgs, typename Alloc, typename Fn>
std::shared_ptr<shared_state_task<TArgs>> allocate_future_state_task(
    const Alloc&, Fn&&);

} /* namespace ilias::impl */


template<typename T> using is_future =
    typename impl::_is_future<std::remove_cv_t<T>>::type;
template<typename T> using is_promise =
    typename impl::_is_promise<std::remove_cv_t<T>>::type;
template<typename T> using is_startable_future =
    typename impl::_is_startable_future<std::remove_cv_t<T>>::type;

using std::future_errc;
using std::future_status;
using std::future_error;

enum class launch {
  dfl = 0x0,
  defer = 0x1,
  aid = 0x2,
  parallel = 0x4
};

constexpr launch operator&(launch, launch) noexcept;
constexpr launch operator|(launch, launch) noexcept;
constexpr launch operator^(launch, launch) noexcept;
constexpr launch operator~(launch) noexcept;

enum class promise_start {
  start,
  defer
};

namespace impl {

template<typename T> using is_launch =
    std::is_same<std::remove_cv_t<std::remove_reference_t<T>>, launch>;

template<typename T>
constexpr auto resolve_future(T&&) noexcept ->
    typename std::enable_if_t<!is_future<std::remove_reference_t<T>>::value,
                              T&&>;

template<typename T>
auto resolve_future(T&&) ->
    typename std::enable_if_t<is_future<std::remove_reference_t<T>>::value,
                              decltype(std::declval<T>().get())
                             >;

template<typename T>
auto start_future(const T&) noexcept ->
    std::enable_if_t<!is_startable_future<T>::value, void>;
template<typename T>
auto start_future(T&) ->
    std::enable_if_t<is_startable_future<T>::value, void>;


template<typename F>
struct _calculated_result_type {
  template<typename... Args> using type =
      decltype(detail::invoke(resolve_future(
                                  std::declval<F>()),
                              resolve_future(
                                  std::declval<Args>())...));
};

template<typename T, typename F>
struct _calculated_result_type<pass_promise_t<T, F>> {
  template<typename...> using type =
      typename pass_promise_t<T, F>::result_type;
};

template<typename F, typename... Args> using future_result_type =
    typename std::enable_if_t<!is_launch<F>::value,
                              _calculated_result_type<std::remove_cv_t<
                                  std::remove_reference_t<F>>>
                             >::template type<Args...>;

} /* namespace ilias::impl */

template<typename F, typename... Args>
auto async_lazy(F&&, Args&&...) ->
    cb_future<impl::future_result_type<F, Args...>>;

template<typename F, typename... Args>
auto async(workq_ptr, F&&, Args&&...) ->
    cb_future<typename std::enable_if<!impl::is_launch<F>::value,
                                   impl::future_result_type<F, Args...>
                                  >::type>;

template<typename F, typename... Args>
auto async(workq_service_ptr, F&&, Args&&...) ->
    cb_future<typename std::enable_if<!impl::is_launch<F>::value,
                                   impl::future_result_type<F, Args...>
                                  >::type>;

template<typename F, typename... Args>
auto async(workq_ptr, launch, F&&, Args&&...) ->
    cb_future<impl::future_result_type<F, Args...>>;

template<typename F, typename... Args>
auto async(workq_service_ptr, launch, F&&, Args&&...) ->
    cb_future<impl::future_result_type<F, Args...>>;


template<typename> class packaged_task;  // Not implemented.


template<typename R>
class cb_promise {
  template<typename> friend class impl::shared_state;

 public:
  cb_promise();
  cb_promise(const cb_promise&) = delete;
  cb_promise(cb_promise&&) noexcept;
  template<typename Alloc> cb_promise(std::allocator_arg_t, const Alloc&);

  ~cb_promise() noexcept = default;

  cb_promise& operator=(const cb_promise&) = delete;
  cb_promise& operator=(cb_promise&&) noexcept;
  void swap(cb_promise&) noexcept;

  cb_future<R> get_future();

  void set_value(const R&);
  void set_value(R&&);
  void set_exception(std::exception_ptr);

 private:
  cb_promise(std::shared_ptr<impl::shared_state<R>>) noexcept;

  std::shared_ptr<impl::shared_state<R>> state_;
};

template<typename R>
class cb_promise<R&> {
  template<typename> friend class impl::shared_state;

 public:
  cb_promise();
  cb_promise(const cb_promise&) = delete;
  cb_promise(cb_promise&&) noexcept;
  template<typename Alloc> cb_promise(std::allocator_arg_t, const Alloc&);

  ~cb_promise() noexcept = default;

  cb_promise& operator=(const cb_promise&) = delete;
  cb_promise& operator=(cb_promise&&) noexcept;
  void swap(cb_promise&) noexcept;

  cb_future<R&> get_future();

  void set_value(R&);
  void set_exception(std::exception_ptr);

 private:
  cb_promise(std::shared_ptr<impl::shared_state<R&>>) noexcept;

  std::shared_ptr<impl::shared_state<R&>> state_;
};

template<>
class cb_promise<void> {
  template<typename> friend class impl::shared_state;

 public:
  ILIAS_ASYNC_EXPORT cb_promise();
  cb_promise(const cb_promise&) = delete;
  cb_promise(cb_promise&&) noexcept;
  template<typename Alloc> cb_promise(std::allocator_arg_t, const Alloc&);

  ~cb_promise() noexcept = default;

  cb_promise& operator=(const cb_promise&) = delete;
  cb_promise& operator=(cb_promise&&) noexcept;
  void swap(cb_promise&) noexcept;

  ILIAS_ASYNC_EXPORT cb_future<void> get_future();

  ILIAS_ASYNC_EXPORT void set_value();
  ILIAS_ASYNC_EXPORT void set_exception(std::exception_ptr);

 private:
  cb_promise(std::shared_ptr<impl::shared_state<void>>) noexcept;

  std::shared_ptr<impl::shared_state<void>> state_;
};


template<typename R>
class cb_future {
  friend cb_promise<R>;
  template<typename> friend class impl::shared_state;
  template<typename, typename, typename, typename...>
      friend class impl::shared_state_fn;
  template<typename> friend class packaged_task;  // Not implemented.

  template<typename F, typename... Args>
  friend auto async_lazy(F&&, Args&&...) ->
      cb_future<impl::future_result_type<F, Args...>>;

  template<typename F, typename... Args>
  friend auto async(workq_ptr, launch, F&&, Args&&...) ->
      cb_future<impl::future_result_type<F, Args...>>;

 public:
  using callback_arg_type = cb_future;

  cb_future() noexcept = default;
  cb_future(const cb_future&) = delete;
  cb_future(cb_future&&) noexcept;

  ~cb_future() noexcept = default;

  cb_future& operator=(const cb_future&) = delete;
  cb_future& operator=(cb_future&&) noexcept;
  void swap(cb_future&) noexcept;

  shared_cb_future<R> share();

  R get();

  bool valid() const noexcept;
  void start() const;

  void wait() const;
  template<typename Rep, typename Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>&) const;
  template<typename Clock, typename Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>&)
      const;

 private:
  cb_future(std::shared_ptr<impl::shared_state<R>>) noexcept;

  std::shared_ptr<impl::shared_state<R>> state_;
};

template<typename R>
class cb_future<R&> {
  friend cb_promise<R&>;
  template<typename> friend class impl::shared_state;
  template<typename, typename, typename, typename...>
      friend class impl::shared_state_fn;
  template<typename> friend class packaged_task;  // Not implemented.

  template<typename F, typename... Args>
  friend auto async_lazy(F&&, Args&&...) ->
      cb_future<impl::future_result_type<F, Args...>>;

  template<typename F, typename... Args>
  friend auto async(workq_ptr, launch, F&&, Args&&...) ->
      cb_future<impl::future_result_type<F, Args...>>;

 public:
  using callback_arg_type = cb_future;

  cb_future() noexcept = default;
  cb_future(const cb_future&) = delete;
  cb_future(cb_future&&) noexcept;

  ~cb_future() noexcept = default;

  cb_future& operator=(const cb_future&) = delete;
  cb_future& operator=(cb_future&&) noexcept;
  void swap(cb_future&) noexcept;

  shared_cb_future<R&> share();

  R& get();

  bool valid() const noexcept;
  void start() const;

  void wait() const;
  template<typename Rep, typename Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>&) const;
  template<typename Clock, typename Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>&)
      const;

 private:
  cb_future(std::shared_ptr<impl::shared_state<R&>>) noexcept;

  std::shared_ptr<impl::shared_state<R&>> state_;
};

template<>
class cb_future<void> {
  friend cb_promise<void>;
  template<typename> friend class impl::shared_state;
  template<typename, typename, typename, typename...>
      friend class impl::shared_state_fn;
  template<typename> friend class packaged_task;  // Not implemented.

  template<typename F, typename... Args>
  friend auto async_lazy(F&&, Args&&...) ->
      cb_future<impl::future_result_type<F, Args...>>;

  template<typename F, typename... Args>
  friend auto async(workq_ptr, launch, F&&, Args&&...) ->
      cb_future<impl::future_result_type<F, Args...>>;

 public:
  using callback_arg_type = cb_future;

  cb_future() noexcept = default;
  cb_future(const cb_future&) = delete;
  cb_future(cb_future&&) noexcept;

  ~cb_future() noexcept = default;

  cb_future& operator=(const cb_future&) = delete;
  cb_future& operator=(cb_future&&) noexcept;
  void swap(cb_future&) noexcept;

  shared_cb_future<void> share();

  ILIAS_ASYNC_EXPORT void get();

  bool valid() const noexcept;
  ILIAS_ASYNC_EXPORT void start() const;

  ILIAS_ASYNC_EXPORT void wait() const;
  template<typename Rep, typename Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>&) const;
  template<typename Clock, typename Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>&)
      const;

 private:
  cb_future(std::shared_ptr<impl::shared_state<void>>) noexcept;

  std::shared_ptr<impl::shared_state<void>> state_;
};


template<typename R>
class shared_cb_future {
  friend cb_future<R>;
  template<typename> friend class impl::shared_state;
  template<typename, typename, typename, typename...>
      friend class impl::shared_state_fn;

 public:
  using callback_arg_type = shared_cb_future;

  shared_cb_future() noexcept = default;
  shared_cb_future(const shared_cb_future&);
  shared_cb_future(shared_cb_future&&) noexcept;
  shared_cb_future(cb_future<R>&&) noexcept;

  ~shared_cb_future() noexcept = default;

  shared_cb_future& operator=(const shared_cb_future&);
  shared_cb_future& operator=(shared_cb_future&&) noexcept;
  void swap(shared_cb_future&) noexcept;

  const R& get() const;

  bool valid() const noexcept;
  void start() const;

  void wait() const;
  template<typename Rep, typename Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>&) const;
  template<typename Clock, typename Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>&)
      const;

 private:
  shared_cb_future(std::shared_ptr<impl::shared_state<R>>) noexcept;

  std::shared_ptr<impl::shared_state<R>> state_;
};

template<typename R>
class shared_cb_future<R&> {
  friend cb_future<R&>;
  template<typename> friend class impl::shared_state;
  template<typename, typename, typename, typename...>
      friend class impl::shared_state_fn;

 public:
  using callback_arg_type = shared_cb_future;

  shared_cb_future() noexcept = default;
  shared_cb_future(const shared_cb_future&);
  shared_cb_future(shared_cb_future&&) noexcept;
  shared_cb_future(cb_future<R&>&&) noexcept;

  ~shared_cb_future() noexcept = default;

  shared_cb_future& operator=(const shared_cb_future&);
  shared_cb_future& operator=(shared_cb_future&&) noexcept;
  void swap(shared_cb_future&) noexcept;

  R& get() const;

  bool valid() const noexcept;
  void start() const;

  void wait() const;
  template<typename Rep, typename Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>&) const;
  template<typename Clock, typename Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>&)
      const;

 private:
  shared_cb_future(std::shared_ptr<impl::shared_state<R&>>) noexcept;

  std::shared_ptr<impl::shared_state<R&>> state_;
};

template<>
class shared_cb_future<void> {
  friend cb_future<void>;
  template<typename> friend class impl::shared_state;
  template<typename, typename, typename, typename...>
      friend class impl::shared_state_fn;

 public:
  using callback_arg_type = shared_cb_future;

  shared_cb_future() noexcept = default;
  shared_cb_future(const shared_cb_future&);
  shared_cb_future(shared_cb_future&&) noexcept;
  shared_cb_future(cb_future<void>&&) noexcept;

  ~shared_cb_future() noexcept = default;

  shared_cb_future& operator=(const shared_cb_future&);
  shared_cb_future& operator=(shared_cb_future&&) noexcept;
  void swap(shared_cb_future&) noexcept;

  ILIAS_ASYNC_EXPORT void get() const;

  bool valid() const noexcept;
  ILIAS_ASYNC_EXPORT void start() const;

  ILIAS_ASYNC_EXPORT void wait() const;
  template<typename Rep, typename Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>&) const;
  template<typename Clock, typename Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>&)
      const;

 private:
  shared_cb_future(std::shared_ptr<impl::shared_state<void>>) noexcept;

  std::shared_ptr<impl::shared_state<void>> state_;
};


template<typename R, typename... Args>
class packaged_task<R(Args...)> {
 public:
  packaged_task() noexcept = default;
  packaged_task(const packaged_task&) = delete;
  packaged_task(packaged_task&&) noexcept;
  template<typename F> packaged_task(F&&);
  template<typename F, typename Alloc> packaged_task(std::allocator_arg_t,
                                                     const Alloc&,
                                                     F&&);
  ~packaged_task() noexcept = default;

  packaged_task& operator=(const packaged_task&) = delete;
  packaged_task& operator=(packaged_task&&) noexcept;

  bool valid() const noexcept;

  cb_future<R> get_future();

  void operator()(Args...);

  void reset();

 private:
  std::shared_ptr<impl::shared_state_task<R(Args...)>> state_;
};


template<typename ResultType, typename F>
class pass_promise_t {
 public:
  using result_type = ResultType;

  pass_promise_t()
      noexcept(std::is_nothrow_default_constructible<F>::value);
  pass_promise_t(const pass_promise_t&)
      noexcept(std::is_nothrow_copy_constructible<F>::value);
  pass_promise_t(pass_promise_t&&)
      noexcept(std::is_nothrow_move_constructible<F>::value);
  pass_promise_t(F&& f)
      noexcept(std::is_nothrow_move_constructible<F>::value);
  pass_promise_t(const F& f)
      noexcept(std::is_nothrow_copy_constructible<F>::value);

  template<typename... Args>
  auto operator()(Args&&...)
      noexcept(noexcept(detail::invoke(std::declval<F>(),
                                       std::declval<Args>()...))) ->
      void;

 private:
  F f_;
};

template<typename ResultType, typename F>
auto pass_promise(F&&) -> pass_promise_t<ResultType, F>;


template<typename R>
void callback(cb_future<R>,
              std::function<
                  void(typename cb_future<R>::callback_arg_type)>);

template<typename R>
void callback(shared_cb_future<R>,
              std::function<
                  void(typename shared_cb_future<R>::callback_arg_type)>,
              promise_start = promise_start::start);


} /* namespace ilias */

namespace std {

template<typename R, typename Alloc>
struct uses_allocator<::ilias::cb_promise<R>, Alloc> : true_type {};
template<typename R, typename Alloc>
struct uses_allocator<::ilias::packaged_task<R>, Alloc> : true_type {};

} /* namespace std */

#include <ilias/future-inl.h>

#endif /* _ILIAS_PROMISE_H_ */
