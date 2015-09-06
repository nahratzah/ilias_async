#ifndef _ILIAS_ASYNC_LL_INL_H_
#define _ILIAS_ASYNC_LL_INL_H_

#include "ll.h"
#include <cassert>

namespace ilias {
namespace ll_detail {


inline auto elem_acqrel::acquire(const elem& e, size_t nrefs) noexcept ->
    void {
  size_t old = e.link_count_.fetch_add(nrefs, memory_order_acquire);
  assert(old + nrefs >= old);  // Wrap-around.
}

inline auto elem_acqrel::release(const elem& e, size_t nrefs) noexcept ->
    void {
  size_t old = e.link_count_.fetch_add(nrefs, memory_order_acquire);
  assert(old >= nrefs);  // Insufficient references.
}


inline auto list::is_unlinked_(const elem& e) noexcept -> bool {
  const auto e_p = e.pred_.load_no_acquire(memory_order_acquire);
  return get<0>(e_p) == nullptr || get<1>(e_p) == MARKED;
}

inline auto list::get_elem_type(const elem& e) noexcept -> elem_type {
  return e.type_;
}


inline iter_link::iter_link(const iter_link& o) noexcept
: iter_link()
{
  if (!list::is_unlinked_(o)) {
    const bool link_succeeded =
        list::link_after_(const_cast<iter_link&>(o), *this);
    assert(link_succeeded);
  }
}

inline auto iter_link::operator=(const iter_link& o) noexcept -> iter_link& {
  while (list::unlink_(*list::pred_(*this), *this, 0) == list::UNLINK_RETRY);
  if (!list::is_unlinked_(o)) {
    const bool link_succeeded =
        list::link_after_(const_cast<iter_link&>(o), *this);
    assert(link_succeeded);
  }
  return *this;
}

inline iter_link::~iter_link() noexcept {
  while (list::unlink_(*list::pred_(*this), *this, 0) == list::UNLINK_RETRY);
}


}} /* namespace ilias::ll_detail */

#endif /* _ILIAS_ASYNC_LL_INL_H_ */
