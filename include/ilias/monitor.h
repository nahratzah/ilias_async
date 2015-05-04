#ifndef _ILIAS_MONITOR_H_
#define _ILIAS_MONITOR_H_

#include <ilias/future.h>
#include <list>
#include <mutex>

namespace ilias {


class monitor {
 public:
  enum class access {
    read,
    write
  };

  class token;

 private:
  using queue_type = std::list<cb_promise<token>>;

 public:
  monitor() noexcept;
  monitor(const monitor&) = delete;
  monitor& operator=(const monitor&) = delete;
  ~monitor() noexcept;

  cb_future<token> queue(access = access::write);

 private:
  void unlock_(access) noexcept;
  void add_(access) noexcept;

  std::mutex mtx_;
  uintptr_t active_readers_ = 0;
  uintptr_t active_writers_ = 0;
  queue_type w_queue_;
  queue_type r_queue_;
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
  bool locked() const noexcept { return m_; }
  explicit operator bool() const noexcept { return locked(); }

 private:
  monitor* m_ = nullptr;
  monitor::access access_;
};

using monitor_access = monitor::access;
using monitor_token = monitor::token;


} /* namespace ilias */

#include "monitor-inl.h"

#endif /* _ILIAS_MONITOR_H_ */
