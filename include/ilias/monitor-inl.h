#ifndef _ILIAS_MONITOR_INL_H_
#define _ILIAS_MONITOR_INL_H_

#include "monitor.h"
#include <utility>

namespace ilias {


inline monitor::token::token(const token& t) noexcept
: m_(t.m_),
  access_(t.access_)
{
  if (m_) m_->add_(access_);
}

inline auto monitor::token::operator=(const token& t) noexcept -> token& {
  token(t).swap(*this);
  return *this;
}

inline monitor::token::token(token&& t) noexcept
: m_(std::exchange(t.m_, nullptr)),
  access_(t.access_)
{}

inline auto monitor::token::operator=(token&& t) noexcept -> token& {
  token(t).swap(*this);
  return *this;
}

inline monitor::token::~token() noexcept {
  if (m_) m_->unlock_(access_);
}

inline auto monitor::token::swap(token& t) noexcept -> void {
  using std::swap;

  swap(m_, t.m_);
  swap(access_, t.access_);
}

inline auto monitor::token::operator==(const token& t) const noexcept -> bool {
  return m_ == t.m_ && access_ == t.access_;
}

inline auto monitor::token::operator!=(const token& t) const noexcept -> bool {
  return !(*this == t);
}

inline auto monitor::token::operator<(const token& t) const noexcept -> bool {
  return m_ < t.m_ || (m_ == t.m_ && access_ == t.access_);
}

inline auto monitor::token::operator>(const token& t) const noexcept -> bool {
  return t < *this;
}

inline auto monitor::token::operator<=(const token& t) const noexcept -> bool {
  return !(t < *this);
}

inline auto monitor::token::operator>=(const token& t) const noexcept -> bool {
  return !(*this < t);
}

inline monitor::token::token(monitor& m, monitor::access a) noexcept
: m_(&m),
  access_(a)
{}

inline auto monitor::token::locked() const noexcept -> bool {
  return m_ != nullptr && access_ != monitor::access::none;
}


} /* namespace ilias */

#endif /* _ILIAS_MONITOR_INL_H_ */
