/*
 * Copyright (c) 2015 Ariane van der Steldt <ariane@stack.nl>
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
