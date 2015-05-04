#include <ilias/monitor.h>
#include <cassert>
#include <iterator>

namespace ilias {


monitor::monitor() noexcept {};

monitor::~monitor() noexcept {
  assert(active_readers_ == 0);
  assert(!active_writers_);
  assert(w_queue_.empty());
  assert(r_queue_.empty());
}

auto monitor::queue(access a) -> cb_future<token> {
  std::unique_lock<std::mutex> lck{ mtx_ };

  /* Read access, when immediately available. */
  if (a == access::read && active_writers_ == 0) {
    cb_promise<token> p;
    p.set_value(token(*this, a));
    ++active_readers_;
    return p.get_future();
  }

  /* Write access, when immediately available. */
  if (a == access::write && active_writers_ == 0 && active_readers_ == 0) {
    cb_promise<token> p;
    p.set_value(token(*this, a));
    ++active_writers_;
    return p.get_future();
  }

  /* Delayed access. */
  switch (a) {
  case access::read:
    r_queue_.emplace_front();
    return r_queue_.front().get_future();
  case access::write:
    w_queue_.emplace_back();
    return w_queue_.back().get_future();
  }
}

auto monitor::unlock_(access a) noexcept -> void {
  std::unique_lock<std::mutex> lck{ mtx_ };

  switch (a) {
  case access::read:
    --active_readers_;
    break;
  case access::write:
    --active_writers_;
    break;
  }

  if (active_readers_ != 0 || active_writers_ != 0) return;

  /*
   * Take an element off the queue, so we can enable it without holding a
   * lock.
   *
   * This way, if the promise triggers immediate destruction of the token,
   * for instance because the future was abandoned,
   * the monitor won't deadlock.
   */
  if (!w_queue_.empty()) {
    /* Move unfulfilled write-token promise out of the critical section. */
    cb_promise<token> p = std::move(w_queue_.front());
    w_queue_.pop_front();
    ++active_writers_;
    lck.unlock();

    /* Unblock a writer. */
    p.set_value(token(*this, access::write));
  } else if (!r_queue_.empty()) {
    /* Move all unfulfilled read-token promises out of the critical section. */
    r_queue_type q = move(r_queue_);
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
  case access::write:
    ++active_writers_;
    break;
  }
}


} /* namespace ilias */
