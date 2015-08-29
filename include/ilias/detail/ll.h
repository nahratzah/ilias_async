#ifndef _ILIAS_ASYNC_LL_H_
#define _ILIAS_ASYNC_LL_H_

#include <atomic>
#include <cstdint>
#include <ilias/llptr.h>

namespace ilias {
namespace ll_detail {


using namespace std;

class elem;
class list;

struct elem_acqrel {
  static void acquire(const elem&, size_t nrefs) noexcept;
  static void release(const elem&, size_t nrefs) noexcept;
};

using elem_ptr = refpointer<elem, elem_acqrel>;
using elem_pred_t = llptr<elem, elem_acqrel, 1>;
using elem_succ_t = llptr<elem, elem_acqrel, 1>;

class elem {
  friend class elem_acqrel;
  friend class list;

 public:
  elem() noexcept = default;

 protected:
  elem(const elem&) noexcept : elem() {}
  elem& operator=(const elem&) noexcept { return *this; }

 private:
  mutable elem_succ_t succ_;
  mutable elem_pred_t pred_;
  mutable atomic<size_t> link_count_;
};

class list {
 public:
  list() noexcept;
  list(const list&) = delete;
  list& operator=(const list&) = delete;
  ~list() noexcept;

 private:
  static elem_ptr succ_(elem&) noexcept;
  static elem_ptr pred_(elem&) noexcept;
  static bool link_axb(elem&, elem&, elem&) noexcept;
  static bool unlink_axb(elem&) noexcept;
  void wait_link0(elem&, size_t) noexcept;

  elem data_;
};


}} /* namespace ilias::ll_detail */

#include "ll-inl.h"

#endif /* _ILIAS_ASYNC_LL_H_ */
