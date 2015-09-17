/*
 * Copyright (c) 2014, 2015 Ariane van der Steldt <ariane@stack.nl>
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
#include <ilias/future.h>

namespace ilias {
namespace impl {


void __throw(std::future_errc ec) {
  throw std::future_error(std::make_error_code(ec));
}


void noop_dependant(std::weak_ptr<void>) noexcept {}


shared_state_base::shared_state_base(bool deferred) noexcept
: state_(deferred ? state_t::uninitialized_deferred : state_t::uninitialized),
  lck_(false),
  shared_(false),
  start_deferred_called_(false),
  start_deferred_value_(false)
{}

auto shared_state_base::start_deferred(bool async) noexcept -> void {
  start_deferred_called_.store(true, std::memory_order_relaxed);
  if (async) start_deferred_value_.store(true, std::memory_order_relaxed);
  do_start_deferred(async);
}

auto shared_state_base::do_start_deferred(bool) noexcept -> void {
  const state_t s = get_state();
  assert(s != state_t::uninitialized_deferred &&
         s != state_t::uninitialized_convert);
}

auto shared_state_base::lock() noexcept -> void {
  unsigned int spincount = 0U;

  for (;;) {
    bool expect = false;
    if (_predict_true(lck_.compare_exchange_weak(expect, true,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)))
      return;

    ++spincount;
    if (expect == true && spincount > 40U)
      std::this_thread::yield();
  }
}

auto shared_state_base::unlock() noexcept -> void {
  bool old_lck = lck_.exchange(false, std::memory_order_release);
  assert(old_lck == true);
}

auto shared_state_base::ensure_uninitialized() const -> void {
  if (_predict_false(get_state() != state_t::uninitialized))
    __throw(future_errc::promise_already_satisfied);
}

auto shared_state_base::register_dependant_begin() -> register_dependant_tx {
  return register_dependant_tx(*this, register_dependant_begin_());
}

auto shared_state_base::register_dependant(void (*fn)(std::weak_ptr<void>),
                                           std::weak_ptr<void> arg) -> void {
  register_dependant_begin().commit(move(fn), move(arg));
}

auto shared_state_base::set_ready_val(
    std::unique_lock<shared_state_base> lck) noexcept -> void {
  auto old_state = state_.exchange(state_t::ready_value,
                                   std::memory_order_release);
  assert(old_state == state_t::uninitialized);
  lck.unlock();

  invoke_ready_cb();
}

auto shared_state_base::set_ready_exc(
    std::unique_lock<shared_state_base> lck) noexcept -> void {
  auto old_state = state_.exchange(state_t::ready_exc,
                                   std::memory_order_release);
  assert(old_state == state_t::uninitialized);
  lck.unlock();

  invoke_ready_cb();
}

auto shared_state_base::add_promise_reference_() noexcept -> void {
  promise_refcnt_.fetch_add(1U, std::memory_order_acquire);
}

auto shared_state_base::remove_promise_reference_() noexcept -> void {
  using std::make_exception_ptr;
  using std::future_error;
  using std::make_error_code;
  constexpr future_errc broken_prom = future_errc::broken_promise;

  if (promise_refcnt_.fetch_sub(1U, std::memory_order_release) == 1U) {
    if (_predict_false(get_state() == state_t::uninitialized))
      set_exc(make_exception_ptr(future_error(make_error_code(broken_prom))));
  }
}


shared_state_converter<void>::~shared_state_converter() noexcept {}

auto shared_state_converter<void>::install_value() -> void {
  auto prom = prom_.lock();
  prom_.reset();
  if (prom) {
    prom->clear_convert();
    prom->set_value();
  }
}

auto shared_state_converter<void>::install_exc(std::exception_ptr e) -> void {
  auto prom = prom_.lock();
  prom_.reset();
  if (prom) {
    prom->clear_convert();
    prom->set_exc(move(e));
  }
}


shared_state<void>::shared_state(bool deferred) noexcept
: shared_state_base(deferred)
{}

shared_state<void>::~shared_state() noexcept {
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

auto shared_state<void>::set_value() -> void {
  std::unique_lock<shared_state_base> lck{ *this };
  this->ensure_uninitialized();
  this->set_ready_val(std::move(lck));
}

auto shared_state<void>::set_exc(std::exception_ptr ptr) -> void {
  using exception_ptr = std::exception_ptr;

  std::unique_lock<shared_state_base> lck{ *this };
  this->ensure_uninitialized();

  void* storage_ptr = &storage_;
  new (storage_ptr) exception_ptr(std::move(ptr));
  this->set_ready_exc(std::move(lck));
}

auto shared_state<void>::get() -> void {
  void* storage_ptr = &storage_;

  switch (this->wait()) {
  case state_t::ready_value:
    return;
  case state_t::ready_exc:
    std::rethrow_exception(*static_cast<std::exception_ptr*>(storage_ptr));
    __builtin_unreachable();
  default:
    __builtin_unreachable();
  }

  assert(false);
  for (;;);
}

auto shared_state<void>::do_start_deferred(bool async) noexcept -> void {
  if (auto converter = atomic_load_explicit(&this->convert_,
                                            std::memory_order_relaxed))
    converter->start_deferred(async);
  else
    this->shared_state_base::do_start_deferred(async);
}


auto shared_state_base::register_dependant_tx::commit(
    void (*fn)(std::weak_ptr<void>),
    std::weak_ptr<void> arg) noexcept -> void {
  assert(self_ != nullptr);

  auto self_ptr = self_;
  self_ = nullptr;
  self_ptr->register_dependant_commit_(idx_, std::move(fn), std::move(arg));
}


} /* namespace ilias::impl */


cb_promise<void>::cb_promise()
: cb_promise(std::allocator_arg, std::allocator<void>())
{}

auto cb_promise<void>::get_future() -> cb_future<void> {
  if (!state_)
    impl::__throw(future_errc::no_state);
  if (!state_->mark_shared())
    impl::__throw(future_errc::future_already_retrieved);

  return cb_future<void>(state_.underlying_ptr());
}

auto cb_promise<void>::set_value() -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_value();
}

auto cb_promise<void>::set_exception(std::exception_ptr exc) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_exc(std::move(exc));
}


auto cb_future<void>::get() -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->get();
  state_.reset();
}

auto cb_future<void>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

auto cb_future<void>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}


auto shared_cb_future<void>::get() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->get();
}

auto shared_cb_future<void>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

auto shared_cb_future<void>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}


} /* namespace ilias */
