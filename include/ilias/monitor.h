#ifndef _ILIAS_MONITOR_H_
#define _ILIAS_MONITOR_H_

#include <ilias/ilias_async_export.h>
#include <ilias/future.h>
#include <list>
#include <forward_list>
#include <mutex>
#include <tuple>

namespace ilias {


class monitor {
 public:
  enum class access {
    none,
    read,
    upgrade,
    write
  };

  class token;

 private:
  using w_queue_type = std::list<std::tuple<access, cb_promise<token>>>;
  using r_queue_type = std::forward_list<cb_promise<token>>;
  using u_queue_type = std::forward_list<cb_promise<token>>;

 public:
  ILIAS_ASYNC_EXPORT monitor() noexcept;
  monitor(const monitor&) = delete;
  monitor& operator=(const monitor&) = delete;
  ILIAS_ASYNC_EXPORT ~monitor() noexcept;

  ILIAS_ASYNC_EXPORT cb_future<token> queue(access = access::write);

 private:
  void queue_(cb_promise<token>, access = access::write);

 public:
  ILIAS_ASYNC_EXPORT token try_lock(access = access::write) noexcept;

 private:
  ILIAS_ASYNC_EXPORT void unlock_(access) noexcept;
  ILIAS_ASYNC_EXPORT void add_(access) noexcept;
  ILIAS_ASYNC_EXPORT cb_future<token> upgrade_to_write_();

  std::mutex mtx_;
  uintptr_t active_readers_ = 0;
  uintptr_t active_writers_ = 0;
  uintptr_t upgrade_active_ = 0;
  w_queue_type w_queue_;
  r_queue_type r_queue_;
  u_queue_type u_queue_;
};

class monitor::token {
  friend monitor;

 public:
  token() noexcept = default;
  token(const token&) noexcept;
  token& operator=(const token&) noexcept;
  token(token&&) noexcept;
  token& operator=(token&&) noexcept;
  ~token() noexcept;

  void swap(token&) noexcept;

  bool operator==(const token&) const noexcept;
  bool operator!=(const token&) const noexcept;
  bool operator<(const token&) const noexcept;
  bool operator>(const token&) const noexcept;
  bool operator<=(const token&) const noexcept;
  bool operator>=(const token&) const noexcept;

 private:
  token(monitor&, monitor::access) noexcept;

 public:
  monitor::access access() const noexcept { return access_; }
  monitor* owner() const noexcept { return m_; }
  bool locked() const noexcept;
  explicit operator bool() const noexcept { return locked(); }

  ILIAS_ASYNC_EXPORT cb_future<token> upgrade_to_write() const;
  ILIAS_ASYNC_EXPORT token downgrade_to_read() const;
  ILIAS_ASYNC_EXPORT token downgrade_to_upgrade() const;

 private:
  monitor* m_ = nullptr;
  monitor::access access_ = monitor::access::none;
};

using monitor_access = monitor::access;
using monitor_token = monitor::token;


} /* namespace ilias */

#include "monitor-inl.h"

#endif /* _ILIAS_MONITOR_H_ */
