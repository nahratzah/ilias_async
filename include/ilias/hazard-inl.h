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
#ifndef ILIAS_HAZARD_INL_H
#define ILIAS_HAZARD_INL_H

#include "hazard.h"

namespace ilias {


inline basic_hazard::basic_hazard(std::uintptr_t owner)
: hazard_(allocate_hazard(validate_owner(owner)))
{
  assert(hazard_.value.load(std::memory_order_relaxed) == 0U);
}

inline basic_hazard::~basic_hazard() noexcept {
  assert(hazard_.value.load(std::memory_order_relaxed) == 0U);
  hazard_.owner.fetch_and(hazard_t::FLAG, std::memory_order_release);
}

inline auto basic_hazard::is_lock_free() const noexcept -> bool {
  return atomic_is_lock_free(&hazard_.owner) &&
      atomic_is_lock_free(&hazard_.value);
}

inline auto atomic_is_lock_free(const basic_hazard* h) noexcept -> bool {
  return h && h->is_lock_free();
}

template<typename OperationFn, typename NilFn>
auto basic_hazard::do_hazard(std::uintptr_t value, OperationFn&& operation,
                             NilFn&& on_nil) noexcept -> void {
  auto ov = hazard_.value.exchange(value, std::memory_order_acq_rel);
  assert(ov == 0U);
  do_noexcept(operation);
  if (hazard_.value.exchange(0U, std::memory_order_release) == 0U)
    do_noexcept(on_nil);
}

template<typename AcquireFn, typename ReleaseFn>
auto basic_hazard::grant(AcquireFn&& acquire, ReleaseFn&& release,
                         std::uintptr_t owner, std::uintptr_t value,
                         std::size_t nrefs) -> void {
  validate_owner(owner);

  do_noexcept([&]() {
                const auto hzc = hazard_count;

                if (nrefs < hzc) {
                  acquire(hzc - nrefs);
                  nrefs = hzc;
                }

                nrefs -= hazard_grant(owner, value);
                if (nrefs > 0U)
                  release(nrefs);
              });
}

inline auto basic_hazard::grant_n(std::uintptr_t owner, std::uintptr_t value,
                                  std::size_t nrefs) -> std::size_t {
  validate_owner(owner);
  return hazard_grant_n(owner, value, nrefs);
}

inline auto basic_hazard::wait_unused(std::uintptr_t owner,
                                      std::uintptr_t value) -> void {
  validate_owner(owner);
  hazard_wait(owner, value);
}

inline auto basic_hazard::validate_owner(std::uintptr_t p) -> std::uintptr_t {
  if (p == 0U)
    throw std::invalid_argument("hazard: owner must be non-null");

  if ((p & hazard_t::FLAG) != 0U)
    throw std::invalid_argument("hazard: owner may not have LSB set");

  return p;
}


template<typename O, typename V>
auto hazard<O, V>::owner_key(owner_reference v) noexcept -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(std::addressof(v));
}

template<typename O, typename V>
auto hazard<O, V>::value_key(value_reference v) noexcept -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(std::addressof(v));
}

template<typename O, typename V>
hazard<O, V>::hazard(owner_reference v)
: basic_hazard(owner_key(v))
{}

template<typename O, typename V>
template<typename OperationFn, typename NilFn>
auto hazard<O, V>::do_hazard(value_reference value, OperationFn&& operation,
                             NilFn&& on_nil) noexcept ->
    decltype(std::declval<basic_hazard>().do_hazard(
                 std::declval<std::uintptr_t>(),
                 std::declval<OperationFn>(),
                 std::declval<NilFn>()))
{
  return this->basic_hazard::do_hazard(value_key(value),
                                       std::forward<OperationFn>(operation),
                                       std::forward<NilFn>(on_nil));
}

template<typename O, typename V>
template<typename AcquireFn, typename ReleaseFn>
auto hazard<O, V>::grant(AcquireFn&& acquire, ReleaseFn&& release,
                         owner_reference owner, value_reference value,
                         std::size_t nrefs) -> void {
  basic_hazard::grant(std::forward<AcquireFn>(acquire),
                      std::forward<ReleaseFn>(release),
                      owner_key(owner), value_key(value), nrefs);
}

template<typename O, typename V>
auto hazard<O, V>::grant_n(owner_reference owner, value_reference value,
                           std::size_t nrefs) -> std::size_t {
  return basic_hazard::grant_n(owner_key(owner),
      value_key(value), nrefs);
}

template<typename O, typename V>
auto hazard<O, V>::wait_unused(owner_reference owner, value_reference value) ->
    void {
  basic_hazard::wait_unused(owner_key(owner), value_key(value));
}


} /* namespace ilias */

#endif /* ILIAS_HAZARD_INL_H */
