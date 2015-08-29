#include <ilias/detail/ll.h>
#include <thread>
#include <tuple>
#include <utility>

namespace ilias {
namespace ll_detail {


auto list::succ_(elem& x) noexcept -> elem_ptr {
  auto x_succ = x.succ_.load(memory_order_acquire);

  while (get<1>(x_succ) != elem_succ_t::flags_type()) {
    auto x_sp = get<0>(x_succ)->pred_.load_no_acquire(memory_order_acquire);
    if (get<1>(x_sp) != elem_pred_t::flags_type()) {
      /* SKIP */
    } else if (get<0>(x_sp) != &x) {
      const auto new_xsp_val = make_tuple(&x, elem_pred_t::flags_type(1));
      if (!get<0>(x_succ)->pred_.compare_exchange_weak(move(x_sp),
                                                       move(new_xsp_val),
                                                       memory_order_relaxed,
                                                       memory_order_relaxed) &&
          get<1>(x_sp) != elem_pred_t::flags_type()) {
        x_succ = x.succ_.load(memory_order_acquire);
        continue;
      }
    }

    auto new_successor = get<0>(x_succ)->succ_.load(memory_order_acquire);
    if (x.succ_.compare_exchange_weak(x_succ, new_successor,
                                      memory_order_acquire,
                                      memory_order_relaxed))
      x_succ = move(new_successor);
  }

  auto rv = get<0>(move(x_succ));
  return rv;
}

auto list::pred_(elem& x) noexcept -> elem_ptr {
  auto x_pred = x.pred_.load(memory_order_acquire);

  for (;;) {
    if (get<1>(x_pred) == elem_pred_t::flags_type()) {
      auto x_ps = get<0>(x_pred)->succ_.load(memory_order_acquire);
      if (get<0>(x_ps) == &x) break;  // GUARD
      if (get<1>(x_ps) == elem_succ_t::flags_type()) {
        auto new_predecessor = make_tuple(get<0>(move(x_ps)),
                                          elem_pred_t::flags_type());
        if (x.pred_.compare_exchange_weak(x_pred, new_predecessor,
                                          memory_order_acquire,
                                          memory_order_acquire))
          x_pred = move(new_predecessor);
      } else {
        auto new_predecessor =
            get<0>(x_pred)->pred_.load(memory_order_acquire);
        if (x.pred_.compare_exchange_weak(x_pred, new_predecessor,
                                          memory_order_acquire,
                                          memory_order_acquire))
          x_pred = move(new_predecessor);
      }
    } else {
      auto x_pp = get<0>(x_pred)->pred_.load(memory_order_acquire);
      if (get<1>(x_pp) != elem_pred_t::flags_type()) {
        assert(get<1>(x_pred) == get<1>(x_pp));
        if (x.pred_.compare_exchange_weak(x_pred, x_pp,
            memory_order_acquire,
            memory_order_relaxed))
          x_pred = move(x_pp);
      } else {
        break;
      }
    }
  }

  auto rv = get<0>(move(x_pred));
  return rv;
}

auto list::link_axb(elem& a, elem& x, elem& b) noexcept -> bool {
  assert(x.succ_.load_no_acquire() ==
         make_tuple(nullptr, elem_succ_t::flags_type()));
  assert(x.pred_.load_no_acquire() ==
         make_tuple(nullptr, elem_pred_t::flags_type()));
  x.succ_.store(make_tuple(&b, elem_succ_t::flags_type()),
                memory_order_release);
  x.pred_.store(make_tuple(&a, elem_pred_t::flags_type()),
                memory_order_release);

  auto as_expect = make_tuple(&b, elem_succ_t::flags_type());
  auto as_assign = make_tuple(elem_ptr(&x), elem_succ_t::flags_type());
  if (!a.succ_.compare_exchange_weak(move(as_expect), move(as_assign),
                                     memory_order_release,
                                     memory_order_relaxed)) {
    auto bp_expect = make_tuple(&a, elem_pred_t::flags_type());
    auto bp_assign = make_tuple(elem_ptr(&x), elem_pred_t::flags_type());
    if (!b.pred_.compare_exchange_strong(move(bp_expect), move(bp_assign),
                                         memory_order_release,
                                         memory_order_relaxed))
      pred_(b);

    return true;
  }

  x.succ_.store(make_tuple(nullptr, elem_succ_t::flags_type()),
                memory_order_release);
  x.pred_.store(make_tuple(nullptr, elem_pred_t::flags_type()),
                memory_order_release);
  return false;
}

auto list::unlink_axb(elem& x) noexcept -> bool {
  assert(get<0>(x.succ_.load_no_acquire()) != nullptr);
  assert(get<0>(x.pred_.load_no_acquire()) != nullptr);

  const auto unlink_assign = make_tuple(elem_ptr(&x),
                                        elem_succ_t::flags_type(1));
  for (;;) {
    const elem_ptr a = pred_(x);
    auto as_expect = make_tuple(&x, elem_succ_t::flags_type());
    if (a->succ_.compare_exchange_weak(as_expect, unlink_assign,
                                       memory_order_acq_rel,
                                       memory_order_relaxed)) {
      elem_pred_t::no_acquire_t xp_expect;
      const auto xp_assign = make_tuple(a, elem_pred_t::flags_type(1));
      do {
        xp_expect = make_tuple(a.get(), elem_pred_t::flags_type());
      } while (!x.pred_.compare_exchange_weak(xp_expect, xp_assign,
                                              memory_order_acq_rel,
                                              memory_order_acquire) &&
               get<1>(xp_expect) == elem_pred_t::flags_type());

      pred_(x);
      succ_(x);
      pred_(*succ_(*a));
      return true;
    }
    if (as_expect == unlink_assign) return false;
  }
}

namespace {

constexpr unsigned int SPIN = 100;

inline void spinwait(unsigned int& spin) noexcept {
  if (spin-- != 0) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    /* MS-compiler x86/x86_64 assembly. */
    __asm {
      __asm pause
    };
#elif (defined(__GNUC__) || defined(__clang__)) &&                      \
      (defined(__amd64__) || defined(__x86_64__) ||                     \
       defined(__i386__) || defined(__ia64__))
    /* GCC/clang assembly. */
    __asm __volatile("pause":::"memory");
#else
    this_thread::yield();
#endif
  } else {
    spin = SPIN;
    this_thread::yield();
  }
}

} /* namespace ilias::ll_detail::<unnamed> */

auto list::wait_link0(elem& x, size_t expect) noexcept -> void {
  unsigned int spin = SPIN;

  auto lc = x.link_count_.load(memory_order_acquire);
  if (lc <= expect) return;
  for (auto i = succ_(x);
       lc > expect && i != &data_;
       i = succ_(*i)) {
    pred_(*i);
    lc = x.link_count_.load(memory_order_acquire);
  }
  while (lc > expect) {
    pred_(x);
    succ_(x);
    spinwait(spin);
    lc = x.link_count_.load(memory_order_acquire);
  }
}


}} /* namespace ilias::ll_detail */
