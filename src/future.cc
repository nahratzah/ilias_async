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
#include <ilias/future.h>

namespace ilias {
namespace impl {


void __throw(std::future_errc ec) {
  throw std::future_error(std::make_error_code(ec));
}


shared_state_base::shared_state_base(bool deferred) noexcept
: state_(deferred ? state_t::uninitialized_deferred : state_t::uninitialized)
{}

auto shared_state_base::start_deferred(bool) noexcept -> void {
  assert(get_state() != state_t::uninitialized_deferred);
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


} /* namespace ilias::impl */


promise<void>::promise()
: promise(std::allocator_arg, std::allocator<void>())
{}

auto promise<void>::get_future() -> future<void> {
  if (!state_)
    impl::__throw(future_errc::no_state);
  if (!state_->mark_shared())
    impl::__throw(future_errc::future_already_retrieved);

  return future<void>(state_);
}

auto promise<void>::set_value() -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_value();
}

auto promise<void>::set_exception(std::exception_ptr exc) -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->set_exc(std::move(exc));
}


auto future<void>::get() -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);

  state_->get();
  state_.reset();
}

auto future<void>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

auto future<void>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}


auto shared_future<void>::get() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->get();
}

auto shared_future<void>::start() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->start_deferred();
}

auto shared_future<void>::wait() const -> void {
  if (!state_)
    impl::__throw(future_errc::no_state);
  state_->wait();
}


} /* namespace ilias */
