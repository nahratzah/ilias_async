#include <ilias/monitor.h>
#include <cassert>
#include <functional>
#include <iterator>

namespace ilias {


monitor::monitor() noexcept {};

monitor::~monitor() noexcept {
  assert(active_readers_ == 0);
  assert(upgrade_active_ == 0);
  assert(active_writers_ == 0);
  assert(w_queue_.empty());
  assert(r_queue_.empty());
}

auto monitor::queue(access a) -> cb_future<token> {
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;

  if (a == access::none) {
    return async_lazy([](monitor& self) { return token(self, access::none); },
                      std::ref(*this));
  }

  return async_lazy(pass_promise<token>(bind(&monitor::queue_, _2, _1, _3)),
                    this, a);
}

auto monitor::queue_(cb_promise<token> p, access a) -> void {
  std::unique_lock<std::mutex> lck{ mtx_ };

  /* Read access, when immediately available. */
  if (a == access::read && (active_writers_ == 0 || upgrade_active_ != 0)) {
    ++active_readers_;

    lck.unlock();
    p.set_value(token(*this, a));
    return;
  }

  /* Write access, when immediately available. */
  if ((a == access::write || a == access::upgrade) &&
      active_writers_ == 0 && active_readers_ == 0) {
    assert(upgrade_active_ == 0);
    ++active_writers_;
    if (a == access::upgrade) ++upgrade_active_;

    lck.unlock();
    p.set_value(token(*this, a));
    return;
  }

  /* Delayed access. */
  switch (a) {
  case access::read:
    r_queue_.emplace_front(std::move(p));
    return;
  case access::write:
  case access::upgrade:
    w_queue_.emplace_back(a, std::move(p));
    return;
  case access::none:
    lck.unlock();
    p.set_value(token(*this, access::none));
    return;
  }
}

auto monitor::try_immediate(access a) noexcept -> token {
  std::lock_guard<std::mutex> lck{ mtx_ };

  /* Read access, when immediately available. */
  if (a == access::read && active_writers_ == upgrade_active_) {
    ++active_readers_;
  } else if (a == access::upgrade && active_writers_ == 0) {
    assert(upgrade_active_ == 0);
    ++active_writers_;
    ++upgrade_active_;
  } else if (a == access::write &&
             active_writers_ == 0 && active_readers_ == 0) {
    assert(upgrade_active_ == 0);
    ++active_writers_;
  } else {
    return token(*this, access::none);
  }
  return token(*this, a);
}

auto monitor::unlock_(access a) noexcept -> void {
  if (a == access::none) return;

  std::unique_lock<std::mutex> lck{ mtx_ };

  switch (a) {
  case access::read:
    --active_readers_;
    break;
  case access::upgrade:
    --upgrade_active_;
    /* FALLTHROUGH */
  case access::write:
    --active_writers_;
    break;
  case access::none:
    break;
  }

  /*
   * Take an element off the queue, so we can enable it without holding a
   * lock.
   *
   * This way, if the promise triggers immediate destruction of the token,
   * for instance because the future was abandoned,
   * the monitor won't deadlock.
   */
  if (!u_queue_.empty()) {
    if (active_readers_ == 0) {
      u_queue_type q = std::move(u_queue_);
      /* No need to increment active_writers_: this is done when promise
       * is created on the queue. */
      lck.unlock();

      std::for_each(q.begin(), q.end(),
                    [this](cb_promise<token>& p) {
                      p.set_value(token(*this, access::write));
                    });
    }
    return;
  }

  if (!w_queue_.empty() && active_writers_ == 0) {
    /* Move unfulfilled write/upgrade-token promise
     * out of the critical section. */
    assert(upgrade_active_ == 0);
    std::tuple<access, cb_promise<token>> ap;
    ap = std::move(w_queue_.front());
    w_queue_.pop_front();
    ++active_writers_;
    if (std::get<0>(ap) == access::upgrade) ++upgrade_active_;
    lck.unlock();

    /* Unblock a writer. */
    std::get<1>(ap).set_value(token(*this, std::get<0>(ap)));
    if (std::get<0>(ap) == access::write) return;

    lck.lock();
  }

  if (!r_queue_.empty() && active_writers_ == upgrade_active_) {
    /* Move all unfulfilled read-token promises out of the critical section. */
    r_queue_type q = std::move(r_queue_);
    active_readers_ += std::distance(q.begin(), q.end());
    lck.unlock();

    /* Unblock all readers. */
    std::for_each(q.begin(), q.end(),
                  [this](cb_promise<token>& p) {
                    p.set_value(token(*this, access::read));
                  });
  }
}

auto monitor::add_(access a) noexcept -> void {
  std::lock_guard<std::mutex> lck{ mtx_ };

  switch (a) {
  case access::read:
    ++active_readers_;
    break;
  case access::upgrade:
    ++upgrade_active_;
    /* FALLTHROUGH */
  case access::write:
    ++active_writers_;
    break;
  case access::none:
    break;
  }
}

auto monitor::upgrade_to_write_() -> cb_future<token> {
  cb_promise<token> prom;
  cb_future<token> fut = prom.get_future();

  std::lock_guard<std::mutex> lck{ mtx_ };

  ++active_writers_;
  if (active_readers_ == 0)
    prom.set_value(token(*this, access::write));
  else
    u_queue_.push_front(std::move(prom));

  return fut;
}

auto monitor::try_lock() noexcept -> bool {
  std::lock_guard<std::mutex> lck{ mtx_ };

  if (active_writers_ == 0 && active_readers_ == 0) {
    assert(upgrade_active_ == 0);
    ++active_writers_;
    return true;
  }
  return false;
}

auto monitor::lock() noexcept -> void {
  std::unique_lock<std::mutex> lck{ mtx_ };

  while (active_writers_ != 0 || active_readers_ != 0) {
    lck.unlock();
    std::this_thread::yield();
    lck.lock();
  }

  assert(upgrade_active_ == 0);
  ++active_writers_;
}

auto monitor::unlock() noexcept -> void {
  unlock_(access::write);
}

auto monitor::try_lock_shared() noexcept -> bool {
  std::lock_guard<std::mutex> lck{ mtx_ };

  if (active_writers_ == upgrade_active_) {
    ++active_readers_;
    return true;
  }
  return false;
}

auto monitor::lock_shared() noexcept -> void {
  std::unique_lock<std::mutex> lck{ mtx_ };

  while (active_readers_ != upgrade_active_) {
    lck.unlock();
    std::this_thread::yield();
    lck.lock();
  }

  assert(upgrade_active_ == 0);
  ++active_readers_;
}

auto monitor::unlock_shared() noexcept -> void {
  unlock_(access::read);
}


auto monitor_token::upgrade_to_write() const -> cb_future<token> {
  if (m_ == nullptr)
    throw std::invalid_argument("attempt to upgrade unlocked monitor");

  switch (access_) {
  case access::upgrade:
    break;
  case access::read:
    throw std::invalid_argument("attempt to upgrade read-locked monitor");
    break;
  case access::write:
    throw std::invalid_argument("attempt to upgrade write-locked monitor");
    break;
  case access::none:
    throw std::invalid_argument("attempt to upgrade un-locked monitor");
    break;
  }

  return m_->upgrade_to_write_();
}

auto monitor_token::downgrade_to_read() const -> token {
  if (m_ == nullptr)
    throw std::invalid_argument("attempt to downgrade unlocked monitor");

  switch (access_) {
  case access::upgrade:
    break;
  case access::read:
    throw std::invalid_argument("attempt to downgrade read-locked monitor "
                                "to read");
    break;
  case access::write:
    break;
  case access::none:
    throw std::invalid_argument("attempt to downgrade un-locked monitor "
                                "to read");
    break;
  }

  m_->add_(access::read);
  return monitor_token(*m_, access::read);
}

auto monitor_token::downgrade_to_upgrade() const -> token {
  if (m_ == nullptr)
    throw std::invalid_argument("attempt to downgrade unlocked monitor");

  switch (access_) {
  case access::upgrade:
    throw std::invalid_argument("attempt to downgrade upgrade-locked monitor "
                                "to upgrade");
    break;
  case access::read:
    throw std::invalid_argument("attempt to downgrade read-locked monitor "
                                "to upgrade");
    break;
  case access::write:
    break;
  case access::none:
    throw std::invalid_argument("attempt to downgrade un-locked monitor "
                                "to upgrade");
    break;
  }

  m_->add_(access::upgrade);
  return monitor_token(*m_, access::upgrade);
}


} /* namespace ilias */
