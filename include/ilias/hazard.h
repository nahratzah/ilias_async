/*
 * Copyright (c) 2013 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef ILIAS_HAZARD_H
#define ILIAS_HAZARD_H

#include <ilias/ilias_async_export.h>
#include <ilias/util.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace ilias {
namespace hazard_detail {


/* Hazard pointer logic. */
struct alignas(64) hazard_t
{
  static constexpr std::uintptr_t FLAG = 0x1U;
  static constexpr std::uintptr_t MASK = ~FLAG;

  std::atomic<std::uintptr_t> owner;
  std::atomic<std::uintptr_t> value;
};


} /* namespace ilias::hazard_detail */


/*
 * Basic hazard-pointer implementation.
 */
class basic_hazard
{
 private:
  using hazard_t = hazard_detail::hazard_t;

  hazard_t& m_hazard;

  static std::uintptr_t validate_owner(std::uintptr_t p);

  ILIAS_ASYNC_EXPORT static hazard_t& allocate_hazard(std::uintptr_t) noexcept;
  ILIAS_ASYNC_EXPORT static const std::size_t hazard_count;
  ILIAS_ASYNC_EXPORT static std::size_t hazard_grant(std::uintptr_t,
                                                     std::uintptr_t) noexcept;
  ILIAS_ASYNC_EXPORT static void hazard_wait(std::uintptr_t, std::uintptr_t)
      noexcept;
  ILIAS_ASYNC_EXPORT static std::size_t hazard_grant_n(std::uintptr_t,
                                                       std::uintptr_t,
                                                       std::size_t) noexcept;

 public:
  basic_hazard(std::uintptr_t);
  basic_hazard(const basic_hazard&) = delete;
  basic_hazard(basic_hazard&&) = delete;
  basic_hazard& operator=(const basic_hazard&) = delete;
  ~basic_hazard() noexcept;

  bool is_lock_free() const noexcept;
  friend bool atomic_is_lock_free(const basic_hazard*) noexcept;

  template<typename OperationFn, typename NilFn>
  void do_hazard(std::uintptr_t, OperationFn&&, NilFn&&) noexcept;

  template<typename AcquireFn, typename ReleaseFn>
  static void grant(AcquireFn&&, ReleaseFn&&,
                    std::uintptr_t, std::uintptr_t, std::size_t = 0U);
  static std::size_t grant_n(std::uintptr_t, std::uintptr_t, std::size_t);
  static void wait_unused(std::uintptr_t, std::uintptr_t);
};

/*
 * Hazard handler for a specific owner type.
 */
template<typename OwnerType, typename ValueType>
class hazard
:	public basic_hazard
{
  static_assert((std::alignment_of<OwnerType>::value &
      hazard_detail::hazard_t::FLAG) == 0U,
      "hazard: "
      "reference type must be aligned on an even number of bytes");

 public:
  using owner_type = OwnerType;
  using value_type = ValueType;
  using owner_reference = const owner_type&;
  using value_reference = const value_type&;

  static std::uintptr_t owner_key(owner_reference) noexcept;
  static std::uintptr_t value_key(value_reference) noexcept;

  hazard(owner_reference v);

  template<typename OperationFn, typename NilFn>
  auto do_hazard(value_reference,
                 OperationFn&&, NilFn&&) noexcept ->
      decltype(std::declval<basic_hazard>().do_hazard(
                   std::declval<std::uintptr_t>(),
                   std::declval<OperationFn>(),
                   std::declval<NilFn>()));

  template<typename AcquireFn, typename ReleaseFn>
  static void grant(AcquireFn&&, ReleaseFn&&,
                    owner_reference, value_reference, std::size_t = 0U);
  static std::size_t grant_n(owner_reference, value_reference, std::size_t);

  static void wait_unused(owner_reference, value_reference);
};


} /* namespace ilias */

#include "hazard-inl.h"

#endif /* ILIAS_HAZARD_H */
