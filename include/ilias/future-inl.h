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
#ifndef _ILIAS_FUTURE_INL_H_
#define _ILIAS_FUTURE_INL_H_

#include <ilias/future.h>
#include <ilias/detail/invoke.h>
#include <ilias/workq.h>
#include <memory>
#include <vector>
#include <thread>

namespace ilias {
namespace impl {


template<typename> class shared_state_converter;
template<typename, typename, typename> class shared_state_converter_impl;


template<typename T>
constexpr auto resolve_future(T&& v) noexcept ->
    typename std::enable_if_t<!is_future<std::remove_reference_t<T>>::value,
                              T&&> {
  return std::forward<T>(v);
}

template<typename T>
auto resolve_future(T&& v) ->
    typename std::enable_if_t<is_future<std::remove_reference_t<T>>::value,
                              decltype(std::declval<T>().get())> {
  return v.get();
}

template<typename T>
auto start_future(const T&) noexcept ->
    std::enable_if_t<!is_startable_future<T>::value, void> {
  return;
}

template<typename T>
auto start_future(T& v) ->
    std::enable_if_t<is_startable_future<T>::value, void> {
  v.start();
}


template<typename> struct shared_state_fn_invoke_and_assign;
template<> struct shared_state_fn_invoke_and_assign<void>;


template<typename> class future_callback_functor;

template<typename T>
class future_callback_functor<cb_future<T>> {
 public:
  future_callback_functor() noexcept {}
  virtual ~future_callback_functor() noexcept {}
  virtual void operator()(cb_future<T>&& f) noexcept = 0;
};

template<typename T>
class future_callback_functor<shared_cb_future<T>> {
 private:
  void clear_chain_() noexcept {
    future_callback_functor<shared_cb_future<T>>* p =
        std::exchange(chain, nullptr);
    while (p) {
      auto pp = p;
      p = std::exchange(pp->chain, nullptr);
      delete pp;
    }
  }

 public:
  future_callback_functor() noexcept {}
  virtual ~future_callback_functor() noexcept { clear_chain_(); }
  virtual void operator()(shared_cb_future<T>&& f) noexcept = 0;

  /*
   * This should be a unique_ptr, but the type is incomplete at this
   * point, preventing us from instantiating it.
   */
  future_callback_functor<shared_cb_future<T>>* chain;
};

template<typename Fut, typename Impl>
class future_callback_functor_impl
: public future_callback_functor<Fut>
{
 public:
  future_callback_functor_impl() = delete;
  future_callback_functor_impl(const future_callback_functor_impl&) = delete;
  future_callback_functor_impl& operator=(
      const future_callback_functor_impl&) = delete;
  future_callback_functor_impl(Impl&& impl) : impl_(std::move(impl)) {}
  ~future_callback_functor_impl() noexcept override;
  void operator()(Fut&& f) noexcept override;

 private:
  Impl impl_;
};


template<typename Fut, typename Impl>
future_callback_functor_impl<Fut, Impl>::~future_callback_functor_impl()
    noexcept
{}

template<typename Fut, typename Impl>
void future_callback_functor_impl<Fut, Impl>::operator()(Fut&& f) noexcept {
  ilias::detail::invoke(std::move(impl_), std::move(f));
}


ILIAS_ASYNC_EXPORT void noop_dependant(std::weak_ptr<void>) noexcept;


class ILIAS_ASYNC_EXPORT shared_state_base {
  /*
   * Allow shared_state_converter to invoke clear_convert.
   */
  template<typename> friend class shared_state_converter;

 public:
  enum class state_t {
    uninitialized,
    uninitialized_deferred,
    uninitialized_convert,
    ready_value,
    ready_exc
  };

  class register_dependant_tx;

 protected:
  shared_state_base() = delete;
  explicit shared_state_base(bool = false) noexcept;
  shared_state_base(const shared_state_base&) = delete;
  shared_state_base(shared_state_base&&) = delete;
  shared_state_base& operator=(const shared_state_base&) = delete;
  shared_state_base& operator=(shared_state_base&&) = delete;
  ~shared_state_base() noexcept = default;

 public:
  state_t get_state() const noexcept;
  bool mark_shared() noexcept;  // Mark ssb shared between promise and future.
  state_t wait();
  template<typename Clock, typename Duration>
  state_t wait_until(const std::chrono::time_point<Clock, Duration>&);

  void start_deferred(bool = false) noexcept;
  std::tuple<bool, bool> get_start_deferred() const noexcept;

 protected:
  virtual void do_start_deferred(bool = false) noexcept;

 public:
  void lock() noexcept;
  void unlock() noexcept;
  void ensure_uninitialized() const;

  register_dependant_tx register_dependant_begin();

 private:
  virtual size_t register_dependant_begin_() = 0;
  virtual void register_dependant_commit_(size_t,
                                          void (*)(std::weak_ptr<void>),
                                          std::weak_ptr<void>) noexcept = 0;

 public:
  virtual void register_dependant(void (*)(std::weak_ptr<void>),
                                  std::weak_ptr<void>);

 protected:
  void set_ready_val(std::unique_lock<shared_state_base>) noexcept;
  void set_ready_exc(std::unique_lock<shared_state_base>) noexcept;
  bool clear_deferred() noexcept;
  bool clear_convert() noexcept;
  void mark_convert_present() noexcept;

 private:
  virtual void invoke_ready_cb() noexcept = 0;

  std::atomic<state_t> state_;
  std::atomic<bool> lck_;
  std::atomic<bool> shared_;
  std::atomic<bool> start_deferred_called_;
  std::atomic<bool> start_deferred_value_;
};

template<typename T>
class shared_state
: public shared_state_base,
  public std::enable_shared_from_this<shared_state<T>>
{
  friend shared_state_fn_invoke_and_assign<T>;
  friend shared_state_converter<T>;
  template<typename, typename, typename>
      friend class shared_state_converter_impl;

 public:
  using fut_callback_fn = future_callback_functor<cb_future<T>>;
  using shared_fut_callback_fn = future_callback_functor<shared_cb_future<T>>;

 protected:
  shared_state() = delete;
  explicit shared_state(bool) noexcept;
  shared_state(const shared_state&) = delete;
  shared_state(shared_state&&) = delete;
  shared_state& operator=(const shared_state&) = delete;
  shared_state& operator=(shared_state&&) = delete;
  ~shared_state() noexcept;

 public:
  void set_value(T);
  void set_exc(std::exception_ptr);

  T* get();

  virtual void install_callback(std::unique_ptr<fut_callback_fn>) = 0;
  virtual void install_callback(std::unique_ptr<shared_fut_callback_fn>) = 0;

 protected:
  cb_promise<T> as_promise() noexcept;
  cb_future<T> as_future() noexcept;
  shared_cb_future<T> as_shared_future() noexcept;
  void do_start_deferred(bool) noexcept override;

 private:
  std::aligned_union_t<0, T, std::exception_ptr> storage_;
  std::shared_ptr<shared_state_converter<T>> convert_;  // Converter.
};

template<typename T>
class shared_state<T&>
: public shared_state_base,
  public std::enable_shared_from_this<shared_state<T&>>
{
  friend shared_state_fn_invoke_and_assign<T&>;
  friend shared_state_converter<T&>;
  template<typename, typename, typename>
      friend class shared_state_converter_impl;

 public:
  using fut_callback_fn = future_callback_functor<cb_future<T&>>;
  using shared_fut_callback_fn = future_callback_functor<shared_cb_future<T&>>;

 protected:
  shared_state() = delete;
  explicit shared_state(bool) noexcept;
  shared_state(const shared_state&) = delete;
  shared_state(shared_state&&) = delete;
  shared_state& operator=(const shared_state&) = delete;
  shared_state& operator=(shared_state&&) = delete;
  ~shared_state() noexcept;

 public:
  void set_value(T&);
  void set_exc(std::exception_ptr);

  T* get();

  virtual void install_callback(std::unique_ptr<fut_callback_fn>) = 0;
  virtual void install_callback(std::unique_ptr<shared_fut_callback_fn>) = 0;

 protected:
  cb_promise<T&> as_promise() noexcept;
  cb_future<T&> as_future() noexcept;
  shared_cb_future<T&> as_shared_future() noexcept;
  void do_start_deferred(bool) noexcept override;

 private:
  std::aligned_union_t<0, T*, std::exception_ptr> storage_;
  std::shared_ptr<shared_state_converter<T&>> convert_;  // Converter.
};

template<>
class ILIAS_ASYNC_EXPORT shared_state<void>
: public shared_state_base,
  public std::enable_shared_from_this<shared_state<void>>
{
  friend shared_state_converter<void>;
  friend shared_state_fn_invoke_and_assign<void>;
  template<typename, typename, typename>
      friend class shared_state_converter_impl;

 public:
  using fut_callback_fn = future_callback_functor<cb_future<void>>;
  using shared_fut_callback_fn =
      future_callback_functor<shared_cb_future<void>>;

 protected:
  shared_state() = delete;
  explicit shared_state(bool) noexcept;
  shared_state(const shared_state&) = delete;
  shared_state(shared_state&&) = delete;
  shared_state& operator=(const shared_state&) = delete;
  shared_state& operator=(shared_state&&) = delete;
  ~shared_state() noexcept;

 public:
  void set_value();
  void set_exc(std::exception_ptr);

  void get();

  virtual void install_callback(std::unique_ptr<fut_callback_fn>) = 0;
  virtual void install_callback(std::unique_ptr<shared_fut_callback_fn>) = 0;

 protected:
  cb_promise<void> as_promise() noexcept;
  cb_future<void> as_future() noexcept;
  shared_cb_future<void> as_shared_future() noexcept;
  void do_start_deferred(bool) noexcept override;

 private:
  std::aligned_union_t<0, std::exception_ptr> storage_;
  std::shared_ptr<shared_state_converter<void>> convert_;  // Converter.
};


template<typename T>
class shared_state_converter {
 public:
  shared_state_converter() = delete;
  shared_state_converter(const shared_state_converter&) = delete;
  shared_state_converter(shared_state_converter&&) = delete;
  shared_state_converter& operator=(const shared_state_converter&) = delete;
  shared_state_converter& operator=(shared_state_converter&&) = delete;

  shared_state_converter(const std::shared_ptr<shared_state<T>>&) noexcept;
  virtual ~shared_state_converter() noexcept;

  virtual void start_deferred(bool) noexcept = 0;

 protected:
  void install_value(std::add_rvalue_reference_t<T>);
  void install_exc(std::exception_ptr);

 private:
  std::weak_ptr<shared_state<T>> prom_;
};

template<>
class ILIAS_ASYNC_EXPORT shared_state_converter<void> {
 public:
  shared_state_converter() = delete;
  shared_state_converter(const shared_state_converter&) = delete;
  shared_state_converter(shared_state_converter&&) = delete;
  shared_state_converter& operator=(const shared_state_converter&) = delete;
  shared_state_converter& operator=(shared_state_converter&&) = delete;

  shared_state_converter(const std::shared_ptr<shared_state<void>>&) noexcept;
  virtual ~shared_state_converter() noexcept;

  virtual void start_deferred(bool) noexcept = 0;

 protected:
  void install_value();
  void install_exc(std::exception_ptr);

 private:
  std::weak_ptr<shared_state<void>> prom_;
};


template<typename T, typename U, typename Fn>
class shared_state_converter_impl final
: public shared_state_converter<T>,
  public std::enable_shared_from_this<shared_state_converter_impl<T, U, Fn>>
{
 public:
  shared_state_converter_impl() = delete;
  shared_state_converter_impl(const shared_state_converter_impl&) = delete;
  shared_state_converter_impl(shared_state_converter_impl&&) = delete;
  shared_state_converter_impl& operator=(const shared_state_converter_impl&) =
      delete;
  shared_state_converter_impl& operator=(shared_state_converter_impl&&) =
      delete;

  shared_state_converter_impl(const std::shared_ptr<shared_state<T>>&,
                              std::shared_ptr<shared_state<U>>,
                              Fn);
  ~shared_state_converter_impl() noexcept override;

 private:
  void start_deferred(bool) noexcept override;

 public:
  void init_cb(const std::shared_ptr<shared_state<T>>&, bool);

 private:
  static void dependant_cb_(std::weak_ptr<void>) noexcept;
  static void dependant_cb_shared_(std::weak_ptr<void>) noexcept;

  Fn fn_;
  std::shared_ptr<shared_state<U>> src_;
};


template<typename T, typename Alloc>
class shared_state_nofn
: public shared_state<T>
{
 public:
  using fut_callback_fn = typename shared_state<T>::fut_callback_fn;
  using shared_fut_callback_fn =
      typename shared_state<T>::shared_fut_callback_fn;
  using state_t = typename shared_state<T>::state_t;

  shared_state_nofn() = delete;
  shared_state_nofn(const shared_state_nofn&) = delete;
  shared_state_nofn(shared_state_nofn&&) = delete;
  shared_state_nofn& operator=(const shared_state_nofn&) = delete;
  shared_state_nofn& operator=(shared_state_nofn&&) = delete;

  explicit shared_state_nofn(const Alloc&, bool = false);

 private:
  size_t register_dependant_begin_() override final;
  virtual void register_dependant_commit_(size_t,
                                          void (*)(std::weak_ptr<void>),
                                          std::weak_ptr<void>)
      noexcept override final;

 public:
  void register_dependant(void (*)(std::weak_ptr<void>),
                          std::weak_ptr<void>) override final;

  void install_callback(std::unique_ptr<fut_callback_fn>) override final;
  void install_callback(std::unique_ptr<shared_fut_callback_fn>)
      override final;

 private:
  void invoke_ready_cb() noexcept override final;

  std::mutex ready_cb_mtx_;
  std::unique_ptr<fut_callback_fn> ready_cb_;
  std::unique_ptr<shared_fut_callback_fn> shared_ready_cb_;
  std::vector<std::pair<void (*)(std::weak_ptr<void>), std::weak_ptr<void>>,
              Alloc> dependants_;
};


class shared_state_base::register_dependant_tx {
  friend shared_state_base;

 public:
  register_dependant_tx() noexcept = default;
  register_dependant_tx(const register_dependant_tx&) =
      delete;
  register_dependant_tx& operator=(
      const register_dependant_tx&) = delete;

  register_dependant_tx(register_dependant_tx&&) noexcept;
  register_dependant_tx& operator=(register_dependant_tx&&)
      noexcept;

 private:
  register_dependant_tx(shared_state_base&, size_t) noexcept;

 public:
  void commit(void (*)(std::weak_ptr<void>), std::weak_ptr<void>) noexcept;

 private:
  shared_state_base* self_ = nullptr;
  size_t idx_;
};


template<typename P, typename... Q>
constexpr std::size_t count_true_(std::size_t c, P v0, Q... v) noexcept {
  return count_true_(c + (v0 ? 1U : 0U), v...);
}
constexpr std::size_t count_true_(std::size_t c) noexcept {
  return c;
}
template<typename... P>
constexpr std::size_t count_true(P... v) noexcept {
  return count_true_(0U, v...);
}

template<typename T>
struct shared_state_fn_invoke_and_assign {
  template<typename Fn, typename... Args>
  static auto op(shared_state<T>&, Fn&&, Args&&...) ->
      std::enable_if_t<!is_pass_promise<Fn>::value, void>;
  template<typename Fn, typename... Args>
  static auto op(shared_state<T>&, Fn&&, Args&&...) ->
      std::enable_if_t<is_pass_promise<Fn>::value, void>;
};
template<>
struct shared_state_fn_invoke_and_assign<void> {
  template<typename Fn, typename... Args>
  static auto op(shared_state<void>&, Fn&&, Args&&...) ->
      std::enable_if_t<!is_pass_promise<Fn>::value, void>;
  template<typename Fn, typename... Args>
  static auto op(shared_state<void>&, Fn&&, Args&&...) ->
      std::enable_if_t<is_pass_promise<Fn>::value, void>;
};

template<typename T, typename Alloc, typename Fn, typename... Args>
class shared_state_fn
: public shared_state_nofn<T, Alloc>
{
 private:
  using deferred_type = std::tuple<std::decay_t<Fn>, std::decay_t<Args>...>;

 public:
  using state_t = typename shared_state_nofn<T, Alloc>::state_t;

  shared_state_fn() = delete;
  shared_state_fn(const shared_state_fn&) = delete;
  shared_state_fn(shared_state_fn&&) = delete;
  shared_state_fn& operator=(const shared_state_fn&) = delete;
  shared_state_fn& operator=(shared_state_fn&&) = delete;
  shared_state_fn(const Alloc&, Fn, Args...);

  void init_cb();

 private:
  /* Support for init_cb(). */
  template<std::size_t I, std::size_t... Tail>
  auto init_cb_(std::index_sequence<I, Tail...>) ->
      std::enable_if_t<is_startable_future<std::remove_cv_t<
                           std::remove_reference_t<
                               typename std::tuple_element<I, deferred_type
                              >::type>>
                          >::value,
                       void>;
  template<std::size_t I, std::size_t... Tail>
  auto init_cb_(std::index_sequence<I, Tail...>) ->
      std::enable_if_t<!is_startable_future<std::remove_cv_t<
                            std::remove_reference_t<
                                typename std::tuple_element<I, deferred_type
                               >::type>>
                           >::value,
                       void>;
  void init_cb_(std::index_sequence<>) noexcept { return; }

 protected:
  void do_start_deferred(bool) noexcept override;

 public:
  virtual void invoke_deferred() noexcept;

 private:
  template<std::size_t... I> void invoke_deferred_(std::index_sequence<I...>)
      noexcept;
  template<std::size_t I, std::size_t... Tail> void start_args_(
      std::index_sequence<I, Tail...>) noexcept;
  void start_args_(std::index_sequence<>) noexcept { return; }

  static void dependant_cb(std::weak_ptr<void>) noexcept;

  std::atomic<std::size_t> need_resolution_{ 1U };  // not yet started
  deferred_type deferred_;
};


template<typename T, typename Alloc, typename Fn, typename... Args>
class shared_state_wqjob
: public shared_state_fn<T, Alloc, Fn, Args...>,
  public workq_job
{
 public:
  using state_t = typename shared_state_fn<T, Alloc, Fn, Args...>::state_t;

  shared_state_wqjob() = delete;
  shared_state_wqjob(const shared_state_wqjob&) = delete;
  shared_state_wqjob(shared_state_wqjob&&) = delete;
  shared_state_wqjob& operator=(const shared_state_wqjob&) = delete;
  shared_state_wqjob& operator=(shared_state_wqjob&&) = delete;
  shared_state_wqjob(workq_ptr, unsigned int, const Alloc&, Fn, Args...);

 protected:
  void do_start_deferred(bool) noexcept override;

 public:
  void invoke_deferred() noexcept override;
  void run() noexcept override;

 private:
  std::shared_ptr<void> self_;
};


template<typename T, typename... Args>
class shared_state_task<T(Args...)>
: public shared_state<T>
{
 public:
  using fut_callback_fn = typename shared_state<T>::fut_callback_fn;
  using shared_fut_callback_fn =
      typename shared_state<T>::shared_fut_callback_fn;
  using state_t = typename shared_state<T>::state_t;

 protected:
  shared_state_task();
  shared_state_task(const shared_state_task&) = delete;
  shared_state_task(shared_state_task&&) = delete;
  shared_state_task& operator=(const shared_state_task&) = delete;
  shared_state_task& operator=(shared_state_task&&) = delete;

  ~shared_state_task() noexcept = default;

 public:
  virtual void operator()(Args...) noexcept = 0;
};


template<typename T, typename... Args, typename Alloc, typename Fn>
class shared_state_task_impl<T(Args...), Alloc, Fn> final
: public shared_state_task<T(Args...)>
{
 public:
  using fut_callback_fn =
      typename shared_state_task<T(Args...)>::fut_callback_fn;
  using shared_fut_callback_fn =
      typename shared_state_task<T(Args...)>::shared_fut_callback_fn;
  using state_t = typename shared_state_task<T(Args...)>::state_t;

  shared_state_task_impl() = delete;
  shared_state_task_impl(const shared_state_task_impl&) = delete;
  shared_state_task_impl(shared_state_task_impl&&) = delete;
  shared_state_task_impl& operator=(const shared_state_task_impl&) = delete;
  shared_state_task_impl& operator=(shared_state_task_impl&&) = delete;
  shared_state_task_impl(const Alloc&, std::decay_t<Fn>);

  void operator()(Args...) noexcept override final;

  void register_dependant(void (*)(std::weak_ptr<void>),
                          std::weak_ptr<void>) override final;
  void install_callback(std::unique_ptr<fut_callback_fn>) override final;
  void install_callback(std::unique_ptr<shared_fut_callback_fn>)
      override final;

 private:
  void invoke_ready_cb() noexcept override final;

  std::decay_t<Fn> fn_;

  std::mutex ready_cb_mtx_;
  std::unique_ptr<fut_callback_fn> ready_cb_;
  std::unique_ptr<shared_fut_callback_fn> shared_ready_cb_;
  std::vector<std::pair<void (*)(std::weak_ptr<void>), std::weak_ptr<void>>,
              Alloc> dependants_;
};


inline auto shared_state_base::get_state() const noexcept -> state_t {
  return state_.load(std::memory_order_relaxed);
}

inline auto shared_state_base::mark_shared() noexcept -> bool {
  bool expect = false;
  return shared_.compare_exchange_strong(expect, true,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed);
}

inline auto shared_state_base::wait() -> state_t {
  start_deferred(false);

  state_t s;
  for (s = get_state();
       _predict_false(s != state_t::ready_exc &&
                      s != state_t::ready_value);
       s = get_state()) {
    std::this_thread::yield();
  }
  return s;
}

template<typename Clock, typename Duration>
auto shared_state_base::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) -> state_t {
  using clock = typename std::chrono::time_point<Clock, Duration>::clock;

  start_deferred(true);

  state_t s;
  for (s = get_state();
       _predict_false(s != state_t::ready_exc &&
                      s != state_t::ready_value &&
                      s != state_t::uninitialized_deferred &&
                      clock::now() < tp);
       s = get_state()) {
    std::this_thread::yield();
  }
  return s;
}

inline auto shared_state_base::get_start_deferred() const noexcept ->
    std::tuple<bool, bool> {
  return std::make_tuple(
      start_deferred_called_.load(std::memory_order_relaxed),
      start_deferred_value_.load(std::memory_order_relaxed));
}

inline auto shared_state_base::clear_deferred() noexcept -> bool {
  state_t expect = state_t::uninitialized_deferred;
  return state_.compare_exchange_strong(expect, state_t::uninitialized,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed);
}

inline auto shared_state_base::clear_convert() noexcept -> bool {
  state_t expect = state_t::uninitialized_convert;
  return state_.compare_exchange_strong(expect, state_t::uninitialized,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed);
}

inline auto shared_state_base::mark_convert_present() noexcept -> void {
  state_t expect = state_t::uninitialized;
  bool success = state_.compare_exchange_strong(expect,
                                                state_t::uninitialized_convert,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed);
  assert(success);
}


template<typename T>
shared_state<T>::shared_state(bool deferred) noexcept
: shared_state_base(deferred)
{}

template<typename T>
shared_state<T>::~shared_state() noexcept {
  using exception_ptr = std::exception_ptr;

  void* storage_ptr = &storage_;

  switch (this->get_state()) {
  default:
    break;
  case state_t::ready_value:
    static_cast<T*>(storage_ptr)->~T();
    break;
  case state_t::ready_exc:
    static_cast<exception_ptr*>(storage_ptr)->~exception_ptr();
    break;
  }
}

template<typename T>
auto shared_state<T>::set_value(T v) -> void {
  std::unique_lock<shared_state_base> lck{ *this };
  this->ensure_uninitialized();

  void* storage_ptr = &storage_;
  new (storage_ptr) T(std::move(v));
  this->set_ready_val(std::move(lck));
}

template<typename T>
auto shared_state<T>::set_exc(std::exception_ptr ptr) -> void {
  using exception_ptr = std::exception_ptr;

  std::unique_lock<shared_state_base> lck{ *this };
  this->ensure_uninitialized();

  void* storage_ptr = &storage_;
  new (storage_ptr) exception_ptr(std::move(ptr));
  this->set_ready_exc(std::move(lck));
}

template<typename T>
auto shared_state<T>::get() -> T* {
  void* storage_ptr = &storage_;

  switch (this->wait()) {
  case state_t::ready_value:
    return static_cast<T*>(storage_ptr);
  case state_t::ready_exc:
    std::rethrow_exception(*static_cast<std::exception_ptr*>(storage_ptr));
    break;
  default:
    __builtin_unreachable();
  }

  assert(false);
  for (;;);
}

template<typename T>
auto shared_state<T>::as_promise() noexcept -> cb_promise<T> {
  return cb_promise<T>(this->shared_from_this());
}

template<typename T>
auto shared_state<T>::as_future() noexcept -> cb_future<T> {
  return cb_future<T>(this->shared_from_this());
}

template<typename T>
auto shared_state<T>::as_shared_future() noexcept -> shared_cb_future<T> {
  return shared_cb_future<T>(this->shared_from_this());
}

template<typename T>
auto shared_state<T>::do_start_deferred(bool async) noexcept -> void {
  if (auto converter = atomic_load_explicit(&this->convert_,
                                            std::memory_order_relaxed))
    converter->start_deferred(async);
  else
    this->shared_state_base::do_start_deferred(async);
}


template<typename T>
shared_state<T&>::shared_state(bool deferred) noexcept
: shared_state_base(deferred)
{}

template<typename T>
shared_state<T&>::~shared_state() noexcept {
  using exception_ptr = std::exception_ptr;

  void* storage_ptr = &storage_;

  switch (this->get_state()) {
  default:
    break;
  case state_t::ready_value:
    break;
  case state_t::ready_exc:
    static_cast<exception_ptr*>(storage_ptr)->~exception_ptr();
    break;
  }
}

template<typename T>
auto shared_state<T&>::set_value(T& v) -> void {
  using T_ = T*;

  std::unique_lock<shared_state_base> lck{ *this };
  this->ensure_uninitialized();

  void* storage_ptr = &storage_;
  new (storage_ptr) T_(&v);
  this->set_ready_val(std::move(lck));
}

template<typename T>
auto shared_state<T&>::set_exc(std::exception_ptr ptr) -> void {
  using exception_ptr = std::exception_ptr;

  std::unique_lock<shared_state_base> lck{ *this };
  this->ensure_uninitialized();

  void* storage_ptr = &storage_;
  new (storage_ptr) exception_ptr(std::move(ptr));
  this->set_ready_exc(std::move(lck));
}

template<typename T>
auto shared_state<T&>::get() -> T* {
  void* storage_ptr = &storage_;

  switch (this->wait()) {
  case state_t::ready_value:
    return *static_cast<T**>(storage_ptr);
  case state_t::ready_exc:
    std::rethrow_exception(*static_cast<std::exception_ptr*>(storage_ptr));
    break;
  default:
    __builtin_unreachable();
  }

  assert(false);
  for (;;);
}

template<typename T>
auto shared_state<T&>::as_promise() noexcept -> cb_promise<T&> {
  return cb_promise<T&>(this->shared_from_this());
}

template<typename T>
auto shared_state<T&>::as_future() noexcept -> cb_future<T&> {
  return cb_future<T&>(this->shared_from_this());
}

template<typename T>
auto shared_state<T&>::as_shared_future() noexcept -> shared_cb_future<T&> {
  return shared_cb_future<T&>(this->shared_from_this());
}

template<typename T>
auto shared_state<T&>::do_start_deferred(bool async) noexcept -> void {
  if (auto converter = atomic_load_explicit(&this->convert_,
                                            std::memory_order_relaxed))
    converter->start_deferred(async);
  else
    this->shared_state_base::do_start_deferred(async);
}


inline auto shared_state<void>::as_promise() noexcept -> cb_promise<void> {
  return cb_promise<void>(this->shared_from_this());
}

inline auto shared_state<void>::as_future() noexcept -> cb_future<void> {
  return cb_future<void>(this->shared_from_this());
}

inline auto shared_state<void>::as_shared_future() noexcept ->
    shared_cb_future<void> {
  return shared_cb_future<void>(this->shared_from_this());
}


template<typename T>
shared_state_converter<T>::shared_state_converter(
    const std::shared_ptr<shared_state<T>>& prom) noexcept
: prom_(prom)
{}

template<typename T>
shared_state_converter<T>::~shared_state_converter() noexcept {}

template<typename T>
auto shared_state_converter<T>::install_value(
    std::add_rvalue_reference_t<T> v) -> void {
  std::shared_ptr<shared_state<T>> prom = prom_.lock();
  prom_.reset();
  if (prom) {
    prom->clear_convert();
    prom->set_value(std::forward<T>(v));
  }
}

template<typename T>
auto shared_state_converter<T>::install_exc(std::exception_ptr e) -> void {
  std::shared_ptr<shared_state<T>> prom = prom_.lock();
  prom_.reset();
  if (prom) {
    prom->clear_convert();
    prom->set_exc(move(e));
  }
}


inline shared_state_converter<void>::shared_state_converter(
    const std::shared_ptr<shared_state<void>>& prom) noexcept
: prom_(prom)
{}


template<typename T, typename U, typename Fn>
shared_state_converter_impl<T, U, Fn>::shared_state_converter_impl(
    const std::shared_ptr<shared_state<T>>& prom,
    std::shared_ptr<shared_state<U>> src,
    Fn fn)
: shared_state_converter<T>(prom),
  fn_(std::move(fn)),
  src_(std::move(src))
{}

template<typename T, typename U, typename Fn>
shared_state_converter_impl<T, U, Fn>::~shared_state_converter_impl()
    noexcept {}

template<typename T, typename U, typename Fn>
auto shared_state_converter_impl<T, U, Fn>::start_deferred(
    bool async) noexcept -> void {
  src_->start_deferred(async);
}

template<typename T, typename U, typename Fn>
auto shared_state_converter_impl<T, U, Fn>::init_cb(
    const std::shared_ptr<shared_state<T>>& prom, bool shared) -> void {
  assert(src_ != nullptr);
  assert(prom != nullptr);

  auto tx = src_->register_dependant_begin();  // May throw.

  std::shared_ptr<shared_state_converter<T>> this_ptr =
      this->shared_from_this();
  prom->mark_convert_present();
  auto old_convert = atomic_exchange_explicit(&prom->convert_,
                                              move(this_ptr),
                                              std::memory_order_relaxed);
  assert(old_convert == nullptr);

  tx.commit((shared ? &dependant_cb_shared_ : &dependant_cb_),
            this->shared_from_this());

  bool start_called;
  bool start_async;
  std::tie(start_called, start_async) = prom->get_start_deferred();
  if (start_called) start_deferred(start_async);
}

template<typename T, typename U, typename Fn>
auto shared_state_converter_impl<T, U, Fn>::dependant_cb_(
    std::weak_ptr<void> self) noexcept -> void {
  std::shared_ptr<void> void_ptr = self.lock();
  if (!void_ptr) return;
  const auto self_ptr =
      std::static_pointer_cast<shared_state_converter_impl>(self.lock());
  auto& src_ = self_ptr->src_;
  auto& fn_ = self_ptr->fn_;

  try {
    self_ptr->install_value(detail::invoke(std::move(fn_),
                                           src_->as_future().get()));
  } catch (...) {
    self_ptr->install_exc(std::current_exception());
  }
  src_.reset();
}

template<typename T, typename U, typename Fn>
auto shared_state_converter_impl<T, U, Fn>::dependant_cb_shared_(
    std::weak_ptr<void> self) noexcept -> void {
  std::shared_ptr<void> void_ptr = self.lock();
  if (!void_ptr) return;
  const auto self_ptr =
      std::static_pointer_cast<shared_state_converter_impl>(self.lock());
  auto& src_ = self_ptr->src_;
  auto& fn_ = self_ptr->fn_;

  self_ptr->install_value(detail::invoke(std::move(fn_),
                                         src_->as_shared_future().get()));
  src_.reset();
}


template<typename T, typename Alloc>
shared_state_nofn<T, Alloc>::shared_state_nofn(const Alloc& alloc,
                                               bool deferred)
: shared_state<T>(deferred),
  dependants_(alloc)
{}

template<typename T, typename Alloc>
auto shared_state_nofn<T, Alloc>::register_dependant_begin_() ->
    size_t {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    dependants_.emplace_back(&noop_dependant, std::weak_ptr<void>());
    return dependants_.size() - 1U;;
  case state_t::ready_value:
  case state_t::ready_exc:
    return 0;
  }
}

template<typename T, typename Alloc>
auto shared_state_nofn<T, Alloc>::register_dependant_commit_(
    size_t idx,
    void (*fn)(std::weak_ptr<void>), std::weak_ptr<void> arg) noexcept ->
    void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    dependants_[idx] = std::make_pair(std::move(fn), std::move(arg));
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    ready_cb_mtx_.unlock();
    (*fn)(std::move(arg));
    break;
  }
}

template<typename T, typename Alloc>
auto shared_state_nofn<T, Alloc>::register_dependant(
    void (*fn)(std::weak_ptr<void>), std::weak_ptr<void> arg) -> void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    dependants_.emplace_back(std::move(fn), std::move(arg));
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    ready_cb_mtx_.unlock();
    (*fn)(std::move(arg));
    break;
  }
}

template<typename T, typename Alloc>
auto shared_state_nofn<T, Alloc>::install_callback(
    std::unique_ptr<fut_callback_fn> cb) -> void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    assert(!ready_cb_);
    ready_cb_ = std::move(cb);
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    lck.unlock();
    (*cb)(this->as_future());
    break;
  }
}

template<typename T, typename Alloc>
auto shared_state_nofn<T, Alloc>::install_callback(
    std::unique_ptr<shared_fut_callback_fn> cb) -> void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    cb->chain = shared_ready_cb_.release();
    shared_ready_cb_ = move(cb);
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    lck.unlock();
    (*cb)(this->as_shared_future());
    break;
  }
}

template<typename T, typename Alloc>
auto shared_state_nofn<T, Alloc>::invoke_ready_cb() noexcept -> void {
  std::unique_ptr<fut_callback_fn> ready_cb;
  std::unique_ptr<shared_fut_callback_fn> shared_ready_cb;
  std::vector<std::pair<void (*)(std::weak_ptr<void>), std::weak_ptr<void>>,
              Alloc> dependants;

  /*
   * Copy all callbacks into local variables,
   * so we can operate on them without holding the lock.
   *
   * Releasing the lock is important, for instance if the callback
   * wants to install other callbacks.
   *
   * This also ensures multiple calls of invoke_ready_cb are idempotent
   * (i.e. they'll invoke each callback exactly once).
   */
  {
    std::lock_guard<std::mutex> lck{ ready_cb_mtx_ };
    std::swap(ready_cb, ready_cb_);
    std::swap(shared_ready_cb, shared_ready_cb_);
    std::swap(dependants, dependants_);
  }

  /* Callback for (unshared) future. */
  if (ready_cb)
    (*ready_cb)(this->as_future());
  /* Callback for shared future. */
  for (auto fn = shared_ready_cb.get(); fn != nullptr; fn = fn->chain)
    (*fn)(this->as_shared_future());
  /* Notification of dependants. */
  for (auto& dep : dependants)
    (*std::get<0>(std::move(dep)))(std::get<1>(std::move(dep)));
}


inline shared_state_base::register_dependant_tx::register_dependant_tx(
    register_dependant_tx&& o) noexcept
: self_(std::exchange(o.self_, nullptr)),
  idx_(o.idx_)
{}

inline auto shared_state_base::register_dependant_tx::operator=(
    register_dependant_tx&& o) noexcept -> register_dependant_tx& {
  self_ = std::exchange(o.self_, nullptr);
  idx_ = o.idx_;
  return *this;
}

inline shared_state_base::register_dependant_tx::register_dependant_tx(
    shared_state_base& self, size_t idx) noexcept
: self_(&self),
  idx_(idx)
{}


template<typename T>
template<typename Fn, typename... Args>
auto shared_state_fn_invoke_and_assign<T>::op(shared_state<T>& ss,
                                              Fn&& fn,
                                              Args&&... args) ->
    std::enable_if_t<!is_pass_promise<Fn>::value, void> {
  ss.set_value(detail::invoke(std::forward<Fn>(fn),
                              std::forward<Args>(args)...));
}

template<typename T>
template<typename Fn, typename... Args>
auto shared_state_fn_invoke_and_assign<T>::op(shared_state<T>& ss,
                                              Fn&& fn,
                                              Args&&... args) ->
    std::enable_if_t<is_pass_promise<Fn>::value, void> {
  detail::invoke(std::forward<Fn>(fn),
                 ss.as_promise(),
                 std::forward<Args>(args)...);
}

template<typename Fn, typename... Args>
auto shared_state_fn_invoke_and_assign<void>::op(shared_state<void>& ss,
                                                 Fn&& fn,
                                                 Args&&... args) ->
    std::enable_if_t<!is_pass_promise<Fn>::value, void> {
  detail::invoke(std::forward<Fn>(fn),
                 std::forward<Args>(args)...);
  ss.set_value();
}

template<typename Fn, typename... Args>
auto shared_state_fn_invoke_and_assign<void>::op(shared_state<void>& ss,
                                                 Fn&& fn,
                                                 Args&&... args) ->
    std::enable_if_t<is_pass_promise<Fn>::value, void> {
  detail::invoke(std::forward<Fn>(fn),
                 ss.as_promise(),
                 std::forward<Args>(args)...);
}


template<typename T, typename Alloc, typename Fn, typename... Args>
shared_state_fn<T, Alloc, Fn, Args...>::shared_state_fn(
    const Alloc& alloc, Fn fn, Args... args)
: shared_state_nofn<T, Alloc>(alloc, true),
  deferred_(std::move(fn), std::move(args)...)
{}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_fn<T, Alloc, Fn, Args...>::init_cb() -> void {
  using idx_seq = std::make_index_sequence<1U + sizeof...(Args)>;

  init_cb_(idx_seq());
}

template<typename T, typename Alloc, typename Fn, typename... Args>
template<std::size_t I, std::size_t... Tail>
auto shared_state_fn<T, Alloc, Fn, Args...>::init_cb_(
    std::index_sequence<I, Tail...>) ->
    std::enable_if_t<is_startable_future<std::remove_cv_t<
                         std::remove_reference_t<
                             typename std::tuple_element<I, deferred_type
                            >::type>>
                        >::value,
                     void> {
  auto& v = std::get<I>(deferred_);
  need_resolution_.fetch_add(1U, std::memory_order_acquire);
  try {
    if (!v.state_) __throw(future_errc::no_state);
    v.state_->register_dependant(&dependant_cb, this->shared_from_this());
  } catch (...) {
    need_resolution_.fetch_sub(1U, std::memory_order_release);
    throw;
  }

  init_cb_(std::index_sequence<Tail...>());
}

template<typename T, typename Alloc, typename Fn, typename... Args>
template<std::size_t I, std::size_t... Tail>
auto shared_state_fn<T, Alloc, Fn, Args...>::init_cb_(
    std::index_sequence<I, Tail...>) ->
    std::enable_if_t<!is_startable_future<std::remove_cv_t<
                          std::remove_reference_t<
                              typename std::tuple_element<I, deferred_type
                             >::type>>
                         >::value,
                     void> {
  init_cb_(std::index_sequence<Tail...>());
}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_fn<T, Alloc, Fn, Args...>::do_start_deferred(bool async)
    noexcept -> void {
  using idx_seq = std::make_index_sequence<1U + sizeof...(Args)>;

  if (!async && this->clear_deferred()) {
    start_args_(idx_seq());

    if (need_resolution_.fetch_sub(1U, std::memory_order_relaxed) == 1U)
      invoke_deferred();
  } else if (this->get_state() != state_t::uninitialized_deferred) {
    this->shared_state_nofn<T, Alloc>::do_start_deferred(async);
  }
}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_fn<T, Alloc, Fn, Args...>::invoke_deferred() noexcept ->
    void {
  using idx_seq = std::make_index_sequence<1U + sizeof...(Args)>;

  invoke_deferred_(idx_seq());
}

template<typename T, typename Alloc, typename Fn, typename... Args>
template<std::size_t... I>
auto shared_state_fn<T, Alloc, Fn, Args...>::invoke_deferred_(
    std::index_sequence<I...>) noexcept -> void {
  try {
    shared_state_fn_invoke_and_assign<T>::op(
        *this, resolve_future(std::get<I>(std::move(deferred_)))...);
  } catch (...) {
    this->set_exc(std::current_exception());
  }
}

template<typename T, typename Alloc, typename Fn, typename... Args>
template<std::size_t I, std::size_t... Tail>
auto shared_state_fn<T, Alloc, Fn, Args...>::start_args_(
    std::index_sequence<I, Tail...>) noexcept -> void {
  /* Should not throw, since callbacks have been installed by this time. */
  start_future(std::get<I>(deferred_));

  start_args_(std::index_sequence<Tail...>());
}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_fn<T, Alloc, Fn, Args...>::dependant_cb(
    std::weak_ptr<void> self_weak) noexcept -> void {
  std::shared_ptr<shared_state_fn> self =
      std::static_pointer_cast<shared_state_fn>(self_weak.lock());
  if (!self) return;

  if (self->need_resolution_.fetch_sub(1U, std::memory_order_relaxed) == 1U)
    self->invoke_deferred();
}


template<typename T, typename Alloc, typename Fn, typename... Args>
shared_state_wqjob<T, Alloc, Fn, Args...>::shared_state_wqjob(
    workq_ptr wq, unsigned int flags, const Alloc& alloc,
    Fn fn, Args... args)
: shared_state_fn<T, Alloc, Fn, Args...>(alloc, std::move(fn),
                                         std::move(args)...),
  workq_job(std::move(wq), flags | workq_job::TYPE_ONCE)
{
  if (flags & workq_job::TYPE_PERSIST)
    throw std::invalid_argument("cb_promise workq job cannot be persistant");
}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_wqjob<T, Alloc, Fn, Args...>::do_start_deferred(bool async)
    noexcept -> void {
  assert(self_ == nullptr);
  if (this->get_state() == state_t::uninitialized_deferred) {
    self_ = this->shared_from_this();
    this->shared_state_fn<T, Alloc, Fn, Args...>::do_start_deferred(false);
  } else {
    this->shared_state_nofn<T, Alloc>::do_start_deferred(async);
  }
}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_wqjob<T, Alloc, Fn, Args...>::invoke_deferred() noexcept ->
    void {
  assert(self_ != nullptr);
  this->activate(workq_job::ACT_IMMED);
}

template<typename T, typename Alloc, typename Fn, typename... Args>
auto shared_state_wqjob<T, Alloc, Fn, Args...>::run() noexcept -> void {
  assert(self_ != nullptr);
  this->shared_state_fn<T, Alloc, Fn, Args...>::invoke_deferred();
  self_.reset();  // Clear self reference to enable destruction.
}


template<typename T, typename... Args>
shared_state_task<T(Args...)>::shared_state_task()
: shared_state<T>(false)
{}


template<typename T, typename... Args, typename Alloc, typename Fn>
shared_state_task_impl<T(Args...), Alloc, Fn>::shared_state_task_impl(
    const Alloc& alloc, std::decay_t<Fn> fn)
: fn_(std::move(fn)),
  shared_ready_cb_(alloc),
  dependants_(alloc)
{}

template<typename T, typename... Args, typename Alloc, typename Fn>
auto shared_state_task_impl<T(Args...), Alloc, Fn>::operator()(Args... args)
    noexcept -> void {
  try {
    shared_state_fn_invoke_and_assign<T>::op(
        *this, std::forward<Fn>(fn_), std::forward<Args>(args)...);
  } catch (...) {
    this->set_exc(std::current_exception());
  }
}

template<typename T, typename... Args, typename Alloc, typename Fn>
auto shared_state_task_impl<T(Args...), Alloc, Fn>::register_dependant(
    void (*fn)(std::weak_ptr<void>), std::weak_ptr<void> arg) -> void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    dependants_.emplace_back(std::move(fn), std::move(arg));
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    lck.unlock();
    (*fn)(std::move(arg));
    break;
  }
}

template<typename T, typename... Args, typename Alloc, typename Fn>
auto shared_state_task_impl<T(Args...), Alloc, Fn>::install_callback(
    std::unique_ptr<fut_callback_fn> cb) -> void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    assert(!ready_cb_);
    ready_cb_ = std::move(cb);
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    lck.unlock();
    (*cb)(this->as_future());
    break;
  }
}

template<typename T, typename... Args, typename Alloc, typename Fn>
auto shared_state_task_impl<T(Args...), Alloc, Fn>::install_callback(
    std::unique_ptr<shared_fut_callback_fn> cb) -> void {
  std::unique_lock<std::mutex> lck{ ready_cb_mtx_ };

  switch (this->get_state()) {
  default:
    cb->chain = move(shared_ready_cb_);
    shared_ready_cb_ = move(cb);
    break;
  case state_t::ready_value:
  case state_t::ready_exc:
    lck.unlock();
    (*cb)(this->as_shared_future());
    break;
  }
}

template<typename T, typename... Args, typename Alloc, typename Fn>
auto shared_state_task_impl<T(Args...), Alloc, Fn>::invoke_ready_cb()
    noexcept -> void {
  std::unique_ptr<fut_callback_fn> ready_cb;
  std::unique_ptr<shared_fut_callback_fn> shared_ready_cb;
  std::vector<std::pair<void (*)(std::weak_ptr<void>), std::weak_ptr<void>>,
              Alloc> dependants;

  /*
   * Copy all callbacks into local variables,
   * so we can operate on them without holding the lock.
   *
   * Releasing the lock is important, for instance if the callback
   * wants to install other callbacks.
   *
   * This also ensures multiple calls of invoke_ready_cb are idempotent
   * (i.e. they'll invoke each callback exactly once).
   */
  {
    std::lock_guard<std::mutex> lck{ ready_cb_mtx_ };
    std::swap(ready_cb, ready_cb_);
    std::swap(shared_ready_cb, shared_ready_cb_);
    std::swap(dependants, dependants_);
  }

  /* Callback for (unshared) future. */
  if (ready_cb)
    (*ready_cb)(this->as_future());
  /* Callback for shared future. */
  while (shared_ready_cb) {
    std::unique_ptr<shared_fut_callback_fn> fn;
    swap(fn, shared_ready_cb);
    shared_ready_cb = std::unique_ptr<shared_fut_callback_fn>(
	std::exchange(fn->chain, nullptr));
    (*fn)(this->as_shared_future());
  }
  /* Notification of dependants. */
  for (auto& dep : dependants)
    (*std::get<0>(std::move(dep)))(std::get<1>(std::move(dep)));
}


template<typename T, typename Alloc>
std::shared_ptr<shared_state<T>> allocate_future_state(const Alloc& alloc) {
  using void_alloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<void>;
  using impl = shared_state_nofn<T, void_alloc>;

  return std::allocate_shared<impl>(alloc, alloc);
}

template<typename T, typename Alloc, typename Fn, typename... Args>
std::shared_ptr<shared_state<T>> allocate_future_state(const Alloc& alloc,
                                                       Fn&& fn,
                                                       Args&&... args) {
  using void_alloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<void>;
  using impl = shared_state_fn<T, void_alloc,
                               std::decay_t<Fn>, std::decay_t<Args>...>;

  auto rv = std::allocate_shared<impl>(alloc,
                                       alloc, std::forward<Fn>(fn),
                                       std::forward<Args>(args)...);
  rv->init_cb();
  return rv;
}

template<typename TArgs, typename Alloc, typename Fn>
std::shared_ptr<shared_state_task<TArgs>> allocate_future_state_task(
    const Alloc& alloc, Fn&& fn) {
  using void_alloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<void>;
  using impl = shared_state_task_impl<TArgs, void_alloc, std::decay_t<Fn>>;

  auto rv = std::allocate_shared<impl>(alloc,
                                       alloc, std::forward<Fn>(fn));
  return rv;
}


} /* namespace ilias::impl */


constexpr launch operator&(launch x, launch y) noexcept {
  using inttype = std::underlying_type<launch>::type;

  return static_cast<launch>(static_cast<inttype>(x) &
                             static_cast<inttype>(y));
}

constexpr launch operator|(launch x, launch y) noexcept {
  using inttype = std::underlying_type<launch>::type;

  return static_cast<launch>(static_cast<inttype>(x) |
                             static_cast<inttype>(y));
}

constexpr launch operator^(launch x, launch y) noexcept {
  using inttype = std::underlying_type<launch>::type;

  return static_cast<launch>(static_cast<inttype>(x) ^
                             static_cast<inttype>(y));
}

constexpr launch operator~(launch x) noexcept {
  using inttype = std::underlying_type<launch>::type;

  return static_cast<launch>(~static_cast<inttype>(x));
}


template<typename F, typename... Args>
auto async_lazy(F&& f, Args&&... args) ->
    cb_future<impl::future_result_type<F, Args...>> {
  using result_type = impl::future_result_type<F, Args...>;
  using future_type = cb_future<result_type>;

  return future_type(impl::allocate_future_state<result_type>(
                         std::allocator<void>(),
                         std::forward<F>(f), std::forward<Args>(args)...));
}

template<typename F, typename... Args>
auto async(workq_ptr wq, F&& f, Args&&... args) ->
    cb_future<typename std::enable_if<!impl::is_launch<F>::value,
                                   impl::future_result_type<F, Args...>
                                  >::type> {
  return async(std::move(wq), launch::dfl,
               std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto async(workq_service_ptr wqs, F&& f, Args&&... args) ->
    cb_future<typename std::enable_if<!impl::is_launch<F>::value,
                                   impl::future_result_type<F, Args...>
                                  >::type> {
  return async(std::move(wqs), launch::dfl,
               std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto async(workq_ptr wq, launch l, F&& f, Args&&... args) ->
    cb_future<impl::future_result_type<F, Args...>> {
  using alloc_type = std::allocator<void>;
  using result_type = impl::future_result_type<F, Args...>;
  using future_type = cb_future<result_type>;
  using job_type = impl::shared_state_wqjob<result_type, alloc_type,
                                            std::decay_t<F>,
                                            std::decay_t<Args>...>;

  unsigned int flags = 0;
  if ((l & launch::parallel) == launch::parallel)
    flags |= workq_job::TYPE_PARALLEL;
  if ((l & launch::aid) != launch::aid)
    flags |= workq_job::TYPE_NO_AID;

  future_type rv =
      future_type(new_workq_job<job_type>(wq, flags, alloc_type(),
                                          std::forward<F>(f),
                                          std::forward<Args>(args)...));
  if ((l & launch::defer) != launch::defer) rv.start();
  return rv;
}

template<typename F, typename... Args>
auto async(workq_service_ptr wqs, launch l, F&& f, Args&&... args) ->
    cb_future<impl::future_result_type<F, Args...>> {
  return async(wqs->new_workq(), l, std::forward<F>(f),
               std::forward<Args>(args)...);
}

template<typename T, typename U, typename Fn>
auto convert(cb_promise<T> prom, cb_future<U> src, Fn&& fn) -> void {
  using impl_t = impl::shared_state_converter_impl<T, U,
                                                   std::decay_t<Fn>>;

  if (!prom.state_ || !src.state_) impl::__throw(future_errc::no_state);

  auto impl = std::make_shared<impl_t>(prom.state_, move(src.state_),
                                       std::forward<Fn>(fn));
  impl->init_cb(prom.state_, false);
}

template<typename T, typename U, typename Fn>
auto convert(cb_promise<T> prom, shared_cb_future<U> src, Fn&& fn) -> void {
  using impl_t = impl::shared_state_converter_impl<T, U,
                                                   std::decay_t<Fn>>;

  if (!prom.state_ || !src.state_) impl::__throw(future_errc::no_state);

  auto impl = std::make_shared<impl_t>(prom.state_, move(src.state_),
                                       std::forward<Fn>(fn));
  impl->init_cb(prom.state_, true);
}


template<typename T>
cb_promise_exceptor<T>::cb_promise_exceptor(cb_promise_exceptor&& e) noexcept
: state_(std::move(e.state_))
{}

template<typename T>
auto cb_promise_exceptor<T>::operator=(cb_promise_exceptor&& e) noexcept ->
    cb_promise_exceptor& {
  state_ = std::move(e.state_);
  return *this;
}

template<typename T>
cb_promise_exceptor<T>::cb_promise_exceptor(cb_promise<T>& p) noexcept
: state_(p.state_)
{}

template<typename T>
auto cb_promise_exceptor<T>::set_current_exception() noexcept -> bool {
  using state_t = typename impl::shared_state<T>::state_t;

  if (!state_) return false;

  switch (state_->get_state()) {
  case state_t::uninitialized:
    break;
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
  case state_t::ready_value:
  case state_t::ready_exc:
    return false;
  }

  std::exception_ptr p = std::current_exception();
  if (!p) return false;

  state_->set_exc(move(p));
  state_.reset();
  return true;
}


template<typename R>
cb_promise<R>::cb_promise()
: cb_promise(std::allocator_arg, std::allocator<void>())
{}

template<typename R>
template<typename Alloc>
cb_promise<R>::cb_promise(std::allocator_arg_t, const Alloc& alloc)
: state_(impl::allocate_future_state<R>(alloc))
{}

template<typename R>
cb_promise<R>::cb_promise(cb_promise&& p) noexcept
: state_(std::move(p.state_))
{}

template<typename R>
auto cb_promise<R>::operator=(cb_promise&& p) noexcept -> cb_promise& {
  state_ = std::move(p.state_);
  return *this;
}

template<typename R>
auto cb_promise<R>::swap(cb_promise& p) noexcept -> void {
  std::swap(state_, p.state_);
}

template<typename R>
auto cb_promise<R>::get_future() -> cb_future<R> {
  if (!state_)
    impl::__throw(future_errc::no_state);
  if (!state_->mark_shared())
    impl::__throw(future_errc::future_already_retrieved);

  return cb_future<R>(state_);
}

template<typename R>
auto cb_promise<R>::set_value(const R& v) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_value(v);
}

template<typename R>
auto cb_promise<R>::set_value(R&& v) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_value(std::move(v));
}

template<typename R>
auto cb_promise<R>::set_exception(std::exception_ptr exc) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_exc(std::move(exc));
}

template<typename R>
cb_promise<R>::cb_promise(std::shared_ptr<impl::shared_state<R>> s) noexcept
: state_(s)
{}


template<typename R>
cb_promise<R&>::cb_promise()
: cb_promise(std::allocator_arg, std::allocator<void>())
{}

template<typename R>
template<typename Alloc>
cb_promise<R&>::cb_promise(std::allocator_arg_t, const Alloc& alloc)
: state_(impl::allocate_future_state<R&>(alloc))
{}

template<typename R>
cb_promise<R&>::cb_promise(cb_promise&& p) noexcept
: state_(std::move(p.state_))
{}

template<typename R>
auto cb_promise<R&>::operator=(cb_promise&& p) noexcept -> cb_promise& {
  state_ = std::move(p.state_);
  return *this;
}

template<typename R>
auto cb_promise<R&>::swap(cb_promise& p) noexcept -> void {
  std::swap(state_, p.state_);
}

template<typename R>
auto cb_promise<R&>::get_future() -> cb_future<R&> {
  if (!state_)
    impl::__throw(future_errc::no_state);
  if (!state_->mark_shared())
    impl::__throw(future_errc::future_already_retrieved);

  return cb_future<R&>(state_);
}

template<typename R>
auto cb_promise<R&>::set_value(R& v) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_value(v);
}

template<typename R>
auto cb_promise<R&>::set_exception(std::exception_ptr exc) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_exc(std::move(exc));
}

template<typename R>
cb_promise<R&>::cb_promise(std::shared_ptr<impl::shared_state<R&>> s) noexcept
: state_(s)
{}


template<typename Alloc>
cb_promise<void>::cb_promise(std::allocator_arg_t, const Alloc& alloc)
: state_(impl::allocate_future_state<void>(alloc))
{}

inline cb_promise<void>::cb_promise(cb_promise&& p) noexcept
: state_(std::move(p.state_))
{}

inline auto cb_promise<void>::operator=(cb_promise&& p) noexcept ->
    cb_promise& {
  state_ = std::move(p.state_);
  return *this;
}

inline auto cb_promise<void>::swap(cb_promise& p) noexcept -> void {
  std::swap(state_, p.state_);
}

inline cb_promise<void>::cb_promise(
    std::shared_ptr<impl::shared_state<void>> s) noexcept
: state_(s)
{}


template<typename R>
cb_future<R>::cb_future(cb_future&& f) noexcept
: state_(std::move(f.state_))
{}

template<typename R>
auto cb_future<R>::operator=(cb_future&& f) noexcept -> cb_future& {
  state_ = std::move(f.state_);
  return *this;
}

template<typename R>
auto cb_future<R>::swap(cb_future& f) noexcept -> void {
  std::swap(state_, f.state_);
}

template<typename R>
auto cb_future<R>::share() -> shared_cb_future<R> {
  return shared_cb_future<R>(std::move(state_));
}

template<typename R>
auto cb_future<R>::get() -> R {
  if (!state_)
    impl::__throw(future_errc::no_state);

  R rv = std::move(*state_->get());
  state_.reset();
  return rv;
}

template<typename R>
auto cb_future<R>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename R>
auto cb_future<R>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

template<typename R>
auto cb_future<R>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}

template<typename R>
template<typename Rep, typename Period>
auto cb_future<R>::wait_for(const std::chrono::duration<Rep, Period>& d)
    const -> future_status {
  using state_t = impl::shared_state_base::state_t;

  if (state_ && d.count() == 0) {
    switch (state_->get_state()) {
    case state_t::uninitialized_deferred:
    case state_t::uninitialized_convert:
      return future_status::deferred;
    case state_t::ready_value:
    case state_t::ready_exc:
      return future_status::ready;
    default:
      return future_status::timeout;
    }
  }

  return wait_until(std::chrono::steady_clock::now() + d);
}

template<typename R>
template<typename Clock, typename Duration>
auto cb_future<R>::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (!state_)
    impl::__throw(future_errc::no_state);

  switch (state_->wait_until(tp)) {
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
    return future_status::deferred;
  case state_t::ready_value:
  case state_t::ready_exc:
    return future_status::ready;
  default:
    return future_status::timeout;
  }
}

template<typename R>
cb_future<R>::cb_future(std::shared_ptr<impl::shared_state<R>> s) noexcept
: state_(std::move(s))
{}


template<typename R>
cb_future<R&>::cb_future(cb_future&& f) noexcept
: state_(std::move(f.state_))
{}

template<typename R>
auto cb_future<R&>::operator=(cb_future&& f) noexcept -> cb_future& {
  state_ = std::move(f.state_);
  return *this;
}

template<typename R>
auto cb_future<R&>::swap(cb_future& f) noexcept -> void {
  std::swap(state_, f.state_);
}

template<typename R>
auto cb_future<R&>::share() -> shared_cb_future<R&> {
  return shared_cb_future<R&>(std::move(state_));
}

template<typename R>
auto cb_future<R&>::get() -> R& {
  if (!state_)
    impl::__throw(future_errc::no_state);

  R& rv = *state_->get();
  state_.reset();
  return rv;
}

template<typename R>
auto cb_future<R&>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename R>
auto cb_future<R&>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

template<typename R>
auto cb_future<R&>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}

template<typename R>
template<typename Rep, typename Period>
auto cb_future<R&>::wait_for(const std::chrono::duration<Rep, Period>& d)
    const -> future_status {
  using state_t = impl::shared_state_base::state_t;

  if (state_ && d.count() == 0) {
    switch (state_->get_state()) {
    case state_t::uninitialized_deferred:
    case state_t::uninitialized_convert:
      return future_status::deferred;
    case state_t::ready_value:
    case state_t::ready_exc:
      return future_status::ready;
    default:
      return future_status::timeout;
    }
  }

  return wait_until(std::chrono::steady_clock::now() + d);
}

template<typename R>
template<typename Clock, typename Duration>
auto cb_future<R&>::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (!state_)
    impl::__throw(future_errc::no_state);

  switch (state_->wait_until(tp)) {
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
    return future_status::deferred;
  case state_t::ready_value:
  case state_t::ready_exc:
    return future_status::ready;
  default:
    return future_status::timeout;
  }
}

template<typename R>
cb_future<R&>::cb_future(std::shared_ptr<impl::shared_state<R&>> s) noexcept
: state_(std::move(s))
{}


inline cb_future<void>::cb_future(cb_future&& f) noexcept
: state_(std::move(f.state_))
{}

inline auto cb_future<void>::operator=(cb_future&& f) noexcept -> cb_future& {
  state_ = std::move(f.state_);
  return *this;
}

inline auto cb_future<void>::swap(cb_future& f) noexcept -> void {
  std::swap(state_, f.state_);
}

inline auto cb_future<void>::share() -> shared_cb_future<void> {
  return shared_cb_future<void>(std::move(state_));
}

inline auto cb_future<void>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename Rep, typename Period>
auto cb_future<void>::wait_for(const std::chrono::duration<Rep, Period>& d)
    const -> future_status {
  using state_t = impl::shared_state_base::state_t;

  if (state_ && d.count() == 0) {
    switch (state_->get_state()) {
    case state_t::uninitialized_deferred:
    case state_t::uninitialized_convert:
      return future_status::deferred;
    case state_t::ready_value:
    case state_t::ready_exc:
      return future_status::ready;
    default:
      return future_status::timeout;
    }
  }

  return wait_until(std::chrono::steady_clock::now() + d);
}

template<typename Clock, typename Duration>
auto cb_future<void>::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (!state_)
    impl::__throw(future_errc::no_state);

  switch (state_->wait_until(tp)) {
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
    return future_status::deferred;
  case state_t::ready_value:
  case state_t::ready_exc:
    return future_status::ready;
  default:
    return future_status::timeout;
  }
}

inline cb_future<void>::cb_future(std::shared_ptr<impl::shared_state<void>> s)
    noexcept
: state_(std::move(s))
{}


template<typename R>
shared_cb_future<R>::shared_cb_future(const shared_cb_future& f)
: state_(f.state_)
{}

template<typename R>
shared_cb_future<R>::shared_cb_future(shared_cb_future&& f) noexcept
: state_(std::move(f.state_))
{}

template<typename R>
shared_cb_future<R>::shared_cb_future(cb_future<R>&& f) noexcept
: shared_cb_future(f.share())
{}

template<typename R>
auto shared_cb_future<R>::operator=(const shared_cb_future& f) ->
    shared_cb_future& {
  state_ = f.state_;
  return *this;
}

template<typename R>
auto shared_cb_future<R>::operator=(shared_cb_future&& f) noexcept ->
    shared_cb_future& {
  state_ = std::move(f.state_);
  return *this;
}

template<typename R>
auto shared_cb_future<R>::swap(shared_cb_future& f) noexcept -> void {
  std::swap(state_, f.state_);
}

template<typename R>
auto shared_cb_future<R>::get() const -> const R& {
  if (!state_)
    impl::__throw(future_errc::no_state);
  return *state_->get();
}

template<typename R>
auto shared_cb_future<R>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename R>
auto shared_cb_future<R>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

template<typename R>
auto shared_cb_future<R>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}

template<typename R>
template<typename Rep, typename Period>
auto shared_cb_future<R>::wait_for(const std::chrono::duration<Rep, Period>& d)
    const -> future_status {
  using state_t = impl::shared_state_base::state_t;

  if (state_ && d.count() == 0) {
    switch (state_->get_state()) {
    case state_t::uninitialized_deferred:
    case state_t::uninitialized_convert:
      return future_status::deferred;
    case state_t::ready_value:
    case state_t::ready_exc:
      return future_status::ready;
    default:
      return future_status::timeout;
    }
  }

  return wait_until(std::chrono::steady_clock::now() + d);
}

template<typename R>
template<typename Clock, typename Duration>
auto shared_cb_future<R>::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (!state_)
    impl::__throw(future_errc::no_state);

  switch (state_->wait_until(tp)) {
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
    return future_status::deferred;
  case state_t::ready_value:
  case state_t::ready_exc:
    return future_status::ready;
  default:
    return future_status::timeout;
  }
}

template<typename R>
shared_cb_future<R>::shared_cb_future(std::shared_ptr<impl::shared_state<R>> s)
    noexcept
: state_(std::move(s))
{}


template<typename R>
shared_cb_future<R&>::shared_cb_future(const shared_cb_future& f)
: state_(f.state_)
{}

template<typename R>
shared_cb_future<R&>::shared_cb_future(shared_cb_future&& f) noexcept
: state_(std::move(f.state_))
{}

template<typename R>
shared_cb_future<R&>::shared_cb_future(cb_future<R&>&& f) noexcept
: shared_cb_future(f.share())
{}

template<typename R>
auto shared_cb_future<R&>::operator=(const shared_cb_future& f) ->
    shared_cb_future& {
  state_ = f.state_;
  return *this;
}

template<typename R>
auto shared_cb_future<R&>::operator=(shared_cb_future&& f) noexcept ->
    shared_cb_future& {
  state_ = std::move(f.state_);
  return *this;
}

template<typename R>
auto shared_cb_future<R&>::swap(shared_cb_future& f) noexcept -> void {
  std::swap(state_, f.state_);
}

template<typename R>
auto shared_cb_future<R&>::get() const -> R& {
  if (!state_)
    impl::__throw(future_errc::no_state);
  return *state_->get();
}

template<typename R>
auto shared_cb_future<R&>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename R>
auto shared_cb_future<R&>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

template<typename R>
auto shared_cb_future<R&>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}

template<typename R>
template<typename Rep, typename Period>
auto shared_cb_future<R&>::wait_for(
    const std::chrono::duration<Rep, Period>& d) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (state_ && d.count() == 0) {
    switch (state_->get_state()) {
    case state_t::uninitialized_deferred:
    case state_t::uninitialized_convert:
      return future_status::deferred;
    case state_t::ready_value:
    case state_t::ready_exc:
      return future_status::ready;
    default:
      return future_status::timeout;
    }
  }

  return wait_until(std::chrono::steady_clock::now() + d);
}

template<typename R>
template<typename Clock, typename Duration>
auto shared_cb_future<R&>::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (!state_)
    impl::__throw(future_errc::no_state);

  switch (state_->wait_until(tp)) {
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
    return future_status::deferred;
  case state_t::ready_value:
  case state_t::ready_exc:
    return future_status::ready;
  default:
    return future_status::timeout;
  }
}

template<typename R>
shared_cb_future<R&>::shared_cb_future(
    std::shared_ptr<impl::shared_state<R&>> s) noexcept
: state_(std::move(s))
{}


inline shared_cb_future<void>::shared_cb_future(const shared_cb_future& f)
: state_(f.state_)
{}

inline shared_cb_future<void>::shared_cb_future(shared_cb_future&& f) noexcept
: state_(std::move(f.state_))
{}

inline shared_cb_future<void>::shared_cb_future(cb_future<void>&& f) noexcept
: shared_cb_future(f.share())
{}

inline auto shared_cb_future<void>::operator=(const shared_cb_future& f) ->
    shared_cb_future& {
  state_ = f.state_;
  return *this;
}

inline auto shared_cb_future<void>::operator=(shared_cb_future&& f) noexcept ->
    shared_cb_future& {
  state_ = std::move(f.state_);
  return *this;
}

inline auto shared_cb_future<void>::swap(shared_cb_future& f) noexcept ->
    void {
  std::swap(state_, f.state_);
}

inline auto shared_cb_future<void>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename Rep, typename Period>
auto shared_cb_future<void>::wait_for(
    const std::chrono::duration<Rep, Period>& d) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (state_ && d.count() == 0) {
    switch (state_->get_state()) {
    case state_t::uninitialized_deferred:
    case state_t::uninitialized_convert:
      return future_status::deferred;
    case state_t::ready_value:
    case state_t::ready_exc:
      return future_status::ready;
    default:
      return future_status::timeout;
    }
  }

  return wait_until(std::chrono::steady_clock::now() + d);
}

template<typename Clock, typename Duration>
auto shared_cb_future<void>::wait_until(
    const std::chrono::time_point<Clock, Duration>& tp) const ->
    future_status {
  using state_t = impl::shared_state_base::state_t;

  if (!state_)
    impl::__throw(future_errc::no_state);

  switch (state_->wait_until(tp)) {
  case state_t::uninitialized_deferred:
  case state_t::uninitialized_convert:
    return future_status::deferred;
  case state_t::ready_value:
  case state_t::ready_exc:
    return future_status::ready;
  default:
    return future_status::timeout;
  }
}

inline shared_cb_future<void>::shared_cb_future(
    std::shared_ptr<impl::shared_state<void>> s) noexcept
: state_(std::move(s))
{}


template<typename R, typename... Args>
packaged_task<R(Args...)>::packaged_task(packaged_task&& pt) noexcept
: state_(std::move(pt.state_))
{}

template<typename R, typename... Args>
template<typename F>
packaged_task<R(Args...)>::packaged_task(F&& f)
: packaged_task(std::allocator_arg, std::allocator<void>(), std::forward<F>(f))
{}

template<typename R, typename... Args>
template<typename F, typename Alloc>
packaged_task<R(Args...)>::packaged_task(std::allocator_arg_t,
                                         const Alloc& alloc, F&& f)
: state_(impl::allocate_future_state_task<R(Args...)>(alloc,
                                                      std::forward<F>(f)))
{}

template<typename R, typename... Args>
auto packaged_task<R(Args...)>::operator=(packaged_task&& pt) noexcept ->
    packaged_task& {
  state_ = std::move(pt.state_);
  return *this;
}

template<typename R, typename... Args>
auto packaged_task<R(Args...)>::valid() const noexcept -> bool {
  return state_ != nullptr;
}

template<typename R, typename... Args>
auto packaged_task<R(Args...)>::get_future() -> cb_future<R> {
  if (!state_)
    impl::__throw(future_errc::no_state);
  if (!state_->mark_shared())
    impl::__throw(future_errc::future_already_retrieved);

  return cb_future<R>(state_);
}

template<typename R, typename... Args>
auto packaged_task<R(Args...)>::operator()(Args... args) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  (*state_)(std::forward<Args>(args)...);
}

template<typename R, typename... Args>
auto packaged_task<R(Args...)>::reset() -> void {
  state_.reset();
}


template<typename ResultType, typename F>
pass_promise_t<ResultType, F>::pass_promise_t()
    noexcept(std::is_nothrow_default_constructible<F>::value)
{}

template<typename ResultType, typename F>
pass_promise_t<ResultType, F>::pass_promise_t(const pass_promise_t& o)
    noexcept(std::is_nothrow_copy_constructible<F>::value)
: f_(o.f_)
{}

template<typename ResultType, typename F>
pass_promise_t<ResultType, F>::pass_promise_t(pass_promise_t&& o)
    noexcept(std::is_nothrow_move_constructible<F>::value)
: f_(std::move(o.f_))
{}

template<typename ResultType, typename F>
pass_promise_t<ResultType, F>::pass_promise_t(F&& f)
    noexcept(std::is_nothrow_move_constructible<F>::value)
: f_(std::move(f))
{}

template<typename ResultType, typename F>
pass_promise_t<ResultType, F>::pass_promise_t(const F& f)
    noexcept(std::is_nothrow_copy_constructible<F>::value)
: f_(f)
{}

template<typename ResultType, typename F>
template<typename... Args>
auto pass_promise_t<ResultType, F>::operator()(Args&&... args)
    noexcept(noexcept(detail::invoke(std::declval<F>(),
                                     std::declval<Args>()...))) ->
    void {
  detail::invoke(std::move(f_), std::forward<Args>(args)...);
}

template<typename ResultType, typename F>
auto pass_promise(F&& f) -> pass_promise_t<ResultType, F> {
  return pass_promise_t<ResultType, F>(std::forward<F>(f));
}


template<typename R, typename Fn>
void callback(cb_future<R>&& f, Fn&& fn) {
  using std::remove_cv_t;
  using std::remove_reference_t;
  using fn_impl =
      impl::future_callback_functor_impl<cb_future<R>,
                                         remove_cv_t<remove_reference_t<Fn>>>;
  using fn_base = impl::future_callback_functor<cb_future<R>>;
  using std::make_unique;
  using std::move;

  if (!f.state_) impl::__throw(future_errc::no_state);
  switch (f.state_->get_state()) {
  case impl::shared_state_base::state_t::ready_exc:
  case impl::shared_state_base::state_t::ready_value:
    fn_impl(move(fn))(move(f));
    break;
  default:
    {
      auto fn_ptr = std::unique_ptr<fn_base>(new fn_impl(move(fn)));
      f.state_->install_callback(move(fn_ptr));
    }
    f.start();
    f.state_ = nullptr;
    break;
  }
}

template<typename R, typename Fn>
void callback(shared_cb_future<R> f, Fn&& fn, promise_start ps) {
  using std::remove_cv_t;
  using std::remove_reference_t;
  using fn_impl =
      impl::future_callback_functor_impl<shared_cb_future<R>,
                                         remove_cv_t<remove_reference_t<Fn>>>;
  using fn_base = impl::future_callback_functor<shared_cb_future<R>>;
  using std::make_unique;
  using std::move;

  if (!f.state_) impl::__throw(future_errc::no_state);
  switch (f.state_->get_state()) {
  case impl::shared_state_base::state_t::ready_exc:
  case impl::shared_state_base::state_t::ready_value:
    fn_impl(move(fn))(move(f));
    break;
  default:
    {
      auto fn_ptr = std::unique_ptr<fn_base>(new fn_impl(move(fn)));
      f.state_->install_callback(move(fn_ptr));
    }
    if (ps == promise_start::start) f.start();
    f.state_ = nullptr;
    break;
  }
}


} /* namespace ilias */

#endif /* _ILIAS_FUTURE_INL_H_ */
