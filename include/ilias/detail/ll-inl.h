#ifndef _ILIAS_ASYNC_LL_INL_H_
#define _ILIAS_ASYNC_LL_INL_H_

#include "ll.h"
#include <cassert>

namespace ilias {
namespace ll_detail {


inline auto elem_acqrel::acquire(const elem& e, size_t nrefs) noexcept ->
    void {
  size_t old = e.link_count_.fetch_add(nrefs, memory_order_acquire);
  assert(nrefs == 0 || old + nrefs > old);  // Wrap-around.
}

inline auto elem_acqrel::release(const elem& e, size_t nrefs) noexcept ->
    void {
  size_t old = e.link_count_.fetch_add(nrefs, memory_order_acquire);
  assert(old >= nrefs);  // Insufficient references.
}


inline list::list() noexcept {
  auto p = elem_ptr(&data_);
  data_.succ_.store(make_tuple(p, elem_succ_t::flags_type()),
                    memory_order_release);
  data_.pred_.store(make_tuple(p, elem_pred_t::flags_type()),
                    memory_order_release);
}

inline list::~list() noexcept {
  assert(succ_(data_) == &data_);
  assert(pred_(data_) == &data_);
}


}} /* namespace ilias::ll_detail */

#endif /* _ILIAS_ASYNC_LL_INL_H_ */
