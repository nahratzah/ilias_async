#include <ilias/ll_list.h>
#include <thread>

namespace ilias {
namespace ll_list_detail {


/*
 * Terminology:
 * - successor: a following item in the sequence
 * - predecessor: a previous item in the sequence
 * - direct successor/predecessor:
 *     a successor/predecessor
 *     with no items between
 * - indirect successor/predecessor:
 *     a successor/predecessor with 0 or more items between
 * - marked: the pointer flag is set
 * - unmarked: the pointer flag is cleared
 * - head: the item denoting the list
 * - iterator: an item owned by a stack, indicating a position
 * - link count:
 *     reference counter for how many references an item has,
 *     originating from within the list or its algorithms
 * - reference count:
 *     reference counter used outside the list, by application code
 *     the list will maintain a single reference if the item is linked
 *     the list does not depend on this reference count
 *
 * Invariants:
 * - a.succ == b  ==>  b is the direct successor of a
 * - b.pred == a  ==>  a is an indirect predecessor of b
 * - x.pred == nil  ==>  x.succ == nil
 * - x.succ != nil  ==>  x.pred != nil
 * - x.pred is marked OR x.pred == nil  ==>  x is unlinked
 * - x.pred == (a, marked)  ==>  a.succ == (x, marked) OR a.succ != x
 * - reachable(a, b) AND reachable(b, x)  ==>  reachable(a, x)
 * - a.succ == (b, unmarked)  ==>  reachable(a, b)
 * - head is owned  (NOTE: enforced by user of algorithm)
 * - x.link_count == sum(a.succ == x, a.pred == x, link references on stack)
 * - x.link_count > 0  ==>  single reference count held
 * - x.pred == (b, marked)  ==>  b is direct predecessor of unlinked x
 * - x.pred == (*, unmarked) AND x.succ == (b, unmarked)  ==>  all(y : y.pred == (*, unmarked) AND y.succ == b  ==>  y == x)
 *
 * Rules:
 * - only the creator of an iterator may link/unlink it
 * - traversal of the list always happens from a iterator that is owned
 * - when an element that is to be deleted is encounter,
 *   the algorithm will aid in deletion
 * - after marking x.pred,
 *   move x.pred back until NOT(x.pred.pred == (*, marked))
 * - if x.succ == (*, marked) AND x.pred == (*, marked)
 *   then x.succ.pred should be moved back and marked
 * - if x.succ == (*, d_marked), may not set marked flag
 * - if x.succ == (*, marked, d_marked),
 *   must unlink_aid_ x.succ prior to final stage unlinking of x
 */


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

} /* namespace ilias::ll_list_detail::<unnamed> */

constexpr list::unlink_result list::UNLINK_OK;
constexpr list::unlink_result list::UNLINK_RETRY;
constexpr list::unlink_result list::UNLINK_FAIL;

constexpr list::link_result list::LINK_OK;
constexpr list::link_result list::LINK_LOST;
constexpr list::link_result list::LINK_TWICE;

constexpr list::axb_link_result list::XLINK_OK;
constexpr list::axb_link_result list::XLINK_RETRY;
constexpr list::axb_link_result list::XLINK_TWICE;
constexpr list::axb_link_result list::XLINK_LOST_A;
constexpr list::axb_link_result list::XLINK_LOST_B;
constexpr list::axb_link_result list::XLINK_LOST_AB;


class elem_linking_lock {
 public:
  elem_linking_lock(const elem&) noexcept;
  elem_linking_lock(const elem_linking_lock&) = delete;
  elem_linking_lock& operator=(const elem_linking_lock&) = delete;
  ~elem_linking_lock() noexcept;

  explicit operator bool() const noexcept;

  static bool is_locked(const elem&) noexcept;

 private:
  const elem* locked_ = nullptr;
};


inline elem_linking_lock::elem_linking_lock(const elem& e) noexcept {
  if (!e.linking_.exchange(true, memory_order_acquire))
    locked_ = &e;
}

inline elem_linking_lock::~elem_linking_lock() noexcept {
  if (locked_)
    locked_->linking_.store(false, memory_order_release);
}

inline elem_linking_lock::operator bool() const noexcept {
  return locked_ != nullptr;
}

inline auto elem_linking_lock::is_locked(const elem& e) noexcept -> bool {
  return e.linking_.load(memory_order_relaxed);
}


elem::elem() noexcept {}

elem::elem(elem_type type) noexcept
: type_(type)
{}

elem::~elem() noexcept {}


list::list() noexcept
: data_(elem_type::head)
{
  {
    auto p = elem_ptr(&data_);
    data_.succ_.store(make_tuple(p, S_UNMARKED), memory_order_release);
    data_.pred_.store(make_tuple(p, UNMARKED), memory_order_release);
  }
  assert(data_.link_count_.load() == 2);
}

list::~list() noexcept {
  assert(empty());

  {
    unsigned int spin = SPIN;

    auto lc = data_.link_count_.load(memory_order_acquire);
    while (lc > 2 || succ_(data_) != &data_) {
      spinwait(spin);
      lc = data_.link_count_.load(memory_order_acquire);
    }
  }

  assert(succ_(data_) == &data_);
  assert(pred_(data_) == &data_);

  data_.succ_.store(make_tuple(nullptr, S_UNMARKED), memory_order_release);
  data_.pred_.store(make_tuple(nullptr, UNMARKED), memory_order_release);
}

auto list::empty() const noexcept -> bool {
  return (get_elem_type(*succ_elem_(const_cast<elem&>(data_))) ==
          elem_type::head);
}

auto list::size() const noexcept -> size_type {
  for (;;) {
    size_type sz = 0;
    elem_ptr i;
    for (i = succ_elem_(const_cast<elem&>(data_));
         i != nullptr && get_elem_type(*i) != elem_type::head;
         i = succ_elem_(*i))
      ++sz;
    if (i != nullptr) return sz;
  }
}

auto list::init_begin(position& pos) const noexcept -> elem_ptr {
  link_result rs;

  pos.unlink();

  elem_ptr rv = succ_elem_(const_cast<elem&>(data_));
  for (;;) {
    rs = link_after_(*rv, pos.front_());
    switch (rs) {
    case LINK_OK:
      rs = link_after_(const_cast<elem&>(data_), pos.back_());
      assert(rs == LINK_OK);
      return rv;
    case LINK_LOST:
      rv = succ_elem_(const_cast<elem&>(data_));
      break;
    case LINK_TWICE:
      assert(rs != LINK_TWICE);
      break;
    }
  }
  /* UNREACHABLE */
}

auto list::init_end(position& pos) const noexcept -> elem_ptr {
  link_result rs;
  elem_ptr rv = const_cast<elem*>(&data_);

  pos.unlink();

  rs = link_before_(*rv, pos.back_());
  assert(rs == LINK_OK);
  rs = link_after_(*rv, pos.front_());
  assert(rs == LINK_OK);
  return rv;
}

auto list::succ_(elem& x) noexcept -> elem_ptr {
  auto s = x.succ_.load(memory_order_acquire);
  while (get<0>(s) != nullptr && (get<1>(s) & S_MARKED) == S_MARKED) {
    tie(ignore, get<0>(s), get<1>(s)) = unlink_aid_(x, *get<0>(s));
    if (get<0>(s) == nullptr) s = x.succ_.load(memory_order_acquire);
  }
  return get<0>(s);
}

auto list::pred_(elem& x) noexcept -> elem_ptr {
  /*
   * Predecessor of x.
   *
   * x has no predecessor if:
   *   x.pred == nil
   * p is a direct predecessor of x if:
   *   p.succ == x
   *   OR
   *   x.pred == (p, MARKED)
   * p is an unlinked predecessor of x if:
   *   p.pred is unmarked and p.pred != nil
   */

  auto p = x.pred_.load(memory_order_acquire);
  while (get<0>(p) != nullptr) {
    if (get<1>(p) == UNMARKED) {
      /*
       * x.pred == (a, unmarked)  ==>  a may be indirect predecessor of x
       */
      auto ps = get<0>(p)->succ_.load(memory_order_acquire);

      if (get<0>(ps) != nullptr && get<0>(ps) != &x &&
          (get<1>(ps) & S_MARKED) == S_MARKED) {
        tie(ignore, get<0>(ps), get<1>(ps)) =
            unlink_aid_(*get<0>(p), *get<0>(ps));
      }
      if (get<0>(ps) == nullptr) {
        /*
         * p was unlinked from under us or
         * unlink_aid_ couldn't figure out successor
         */
        auto pp = get<0>(p)->pred_.load(memory_order_acquire);
        assert(get<0>(pp) != nullptr);  // linked element with reference
        get<1>(pp) = get<1>(p);  // Don't modify x.pred_ flags.
        x.pred_.compare_exchange_weak(p, pp,
                                      memory_order_acq_rel,
                                      memory_order_acquire);
        continue;
      }
      if (get<0>(ps) != &x) {
        // Don't change the flags.
        if (x.pred_.compare_exchange_weak(p,
                                          make_tuple(get<0>(ps), UNMARKED),
                                          memory_order_acq_rel,
                                          memory_order_acquire))
          get<0>(p) = get<0>(move(ps));
        continue;
      }
    }

    /*
     * p is a direct predecessor of x
     *
     * p.pred is unmarked and p.pred != nil  ==>  p is linked predecessor of x
     */
    auto pp = get<0>(p)->pred_.load(memory_order_acquire);
    assert(get<0>(pp) != nullptr);  // linked element with reference
    if (get<1>(pp) == MARKED) {
      get<1>(pp) = get<1>(p);  // Don't modify x.pred_ flags.
      x.pred_.compare_exchange_weak(p, move(pp),
                                    memory_order_acq_rel,
                                    memory_order_acquire);
      continue;
    }

    return get<0>(p);
  }

  return nullptr;
}

auto list::link_(elem& a, elem& x, elem& b) noexcept -> axb_link_result {
  elem_linking_lock lck{ x };
  if (!lck) return XLINK_TWICE;

  /*
   * Link a and b from x.
   */
  if (!x.pred_.compare_exchange_strong(make_tuple(static_cast<elem*>(nullptr),
                                                  UNMARKED),
                                       make_tuple(elem_ptr(&a), UNMARKED),
                                       memory_order_acq_rel,
                                       memory_order_acquire))
    return XLINK_TWICE;
  {
    auto x_orig_succ = x.succ_.exchange(make_tuple(elem_ptr(&b), S_UNMARKED),
                                        memory_order_acq_rel);
    assert(x_orig_succ == make_tuple(nullptr, S_UNMARKED));
  }

  /*
   * Link x from a.
   */
  auto as_expect = make_tuple(&b, S_UNMARKED);
  if (a.succ_.compare_exchange_strong(as_expect,
                                      make_tuple(elem_ptr(&x), S_UNMARKED),
                                      memory_order_acq_rel,
                                      memory_order_acquire)) {
    /*
     * Fix b.pred to point at x.
     */
    auto bp_expect = make_tuple(&a, UNMARKED);
    if (!b.pred_.compare_exchange_strong(bp_expect,
                                         make_tuple(elem_ptr(&x), UNMARKED),
                                         memory_order_acq_rel,
                                         memory_order_acquire)) {
      if (bp_expect == make_tuple(&a, MARKED) &&
          b.pred_.compare_exchange_strong(bp_expect,
                                          make_tuple(elem_ptr(&x), MARKED),
                                          memory_order_acq_rel,
                                          memory_order_acquire)) {
        /* SKIP */
      } else {
        pred_(b);
      }
    }
    return XLINK_OK;
  }

  axb_link_result error;
  if (get<0>(as_expect) == nullptr) {
    auto bp = b.pred_.load_no_acquire(memory_order_relaxed);
    if (get<1>(bp) == MARKED)
      error = XLINK_LOST_AB;
    else
      error = XLINK_LOST_A;
  } else if (get<0>(as_expect) != &b) {
    error = XLINK_RETRY;
  } else /* if (get<1>(as_expect) == MARKED) */ {
    auto ap = a.pred_.load_no_acquire(memory_order_relaxed);
    if (get<1>(ap) == MARKED)
      error = XLINK_LOST_AB;
    else
      error = XLINK_LOST_B;
  }

  x.succ_.store(make_tuple(nullptr, S_UNMARKED), memory_order_release);
  x.pred_.store(make_tuple(nullptr, UNMARKED), memory_order_release);
  return error;
}

auto list::unlink_(elem_ptr a, elem& x, size_t expect) noexcept ->
    unlink_result {
  {
    /*
     * Extra scope, to hold references to x
     * without caring about affecting 'expect' value.
     */
    auto as_expect = make_tuple(&x, S_UNMARKED);
    const auto as_assign = make_tuple(elem_ptr(&x), S_MARKED);
    if (!a->succ_.compare_exchange_strong(as_expect, as_assign,
                                          memory_order_acquire,
                                          memory_order_relaxed)) {
      if (get<0>(as_expect) == &x &&
          (get<1>(as_expect) & S_MARKED) == S_MARKED)
        return UNLINK_FAIL;
      if (get<1>(x.pred_.load_no_acquire(memory_order_relaxed)) == MARKED)
        return UNLINK_FAIL;
      if (get<0>(as_expect) != nullptr &&
          (get<1>(as_expect) & D_MARKED) == D_MARKED)
        pred_(*get<0>(as_expect));  // Aid unlinking of a, prior to retry.
      return UNLINK_RETRY;
    }
  }

  elem_ptr b;
  tie(a, b, ignore) = unlink_aid_(*a, x);
  assert(a != nullptr);  // Only this function will reset x.pred_.
  assert(get<0>(x.pred_.load_no_acquire()) == a);

  auto lc = x.link_count_.load(memory_order_acquire);
  while (lc > expect) {
    /* Fix b, if it has become a nullptr. */
    while (b == nullptr) {
      b = get<0>(a->succ_.load(memory_order_acquire));
      if (b == nullptr)
        a = get<0>(a->pred_.load(memory_order_acquire));
    }

    /* Ensure b does not point at x. */
    auto b_pred = get<0>(b->pred_.load_no_acquire(memory_order_acquire));
    while (get<0>(b_pred) == &x) {
      b_pred = pred_(*b).get();
      if (b_pred == nullptr)
        b_pred = get<0>(b->pred_.load_no_acquire(memory_order_acquire));
    }

    /* Prepare for next iteration. */
    lc = x.link_count_.load(memory_order_acquire);
    if (get_elem_type(*b) == elem_type::head) break;
    b = get<0>(b->succ_.load(memory_order_acquire));
  }
  a = nullptr;
  b = nullptr;

  if (lc > expect) wait_link0(x, expect);
  x.pred_.store(make_tuple(nullptr, UNMARKED), memory_order_release);

  return UNLINK_OK;
}

auto list::unlink_aid_(elem& a, elem& x) noexcept ->
    tuple<elem_ptr, elem_ptr, elem_s_flags> {
  tuple<elem_ptr, elem_s_flags> b_ptr;
  get<1>(b_ptr) = S_UNMARKED;  // Not really needed, but nice to initialize.

  auto xp_expect = make_tuple(elem_ptr(&a), UNMARKED);
  while (!x.pred_.compare_exchange_weak(xp_expect,
                                        make_tuple(elem_ptr(&a), MARKED),
                                        memory_order_acq_rel,
                                        memory_order_acquire) &&
         get<1>(xp_expect) != MARKED) {
    if (get<0>(xp_expect) == nullptr)
      return tuple_cat(make_tuple(nullptr), move(b_ptr));
  }
  const elem_ptr a_ptr = (get<1>(xp_expect) == MARKED ?
                          get<0>(move(xp_expect)) :
                          elem_ptr(&a));

  /* Publish intent to move x.succ_ to a_ptr->succ_. */
  auto x_succ = x.succ_.load(memory_order_acquire);
  while (get<0>(x_succ) != nullptr &&
         (get<1>(x_succ) & D_MARKED) != D_MARKED) {
    x.succ_.compare_exchange_weak(x_succ,
                                  make_tuple(get<0>(x_succ),
                                             get<1>(x_succ) | D_MARKED),
                                  memory_order_acq_rel,
                                  memory_order_acquire);
  }

  /* Cascade aiding into successive unlinked elements. */
  while (get<0>(x_succ) != nullptr &&
         (get<1>(x_succ) & S_MARKED) == S_MARKED) {
    tie(ignore, get<0>(x_succ), get<1>(x_succ)) =
        unlink_aid_(x, *get<0>(x_succ));  // RECURSION
    if (get<0>(x_succ) == nullptr)
      x_succ = x.succ_.load(memory_order_acquire);
  }
  if (get<0>(x_succ) == nullptr)
    return tuple_cat(make_tuple(a_ptr), move(b_ptr));

  auto as_expect = make_tuple(elem_ptr(&x), S_MARKED);
  bool cas_succeeded;
  do {
    assert(get<0>(as_expect) == &x &&
           (get<1>(as_expect) & S_MARKED) == S_MARKED);
    get<1>(x_succ) = get<1>(as_expect) & ~S_MARKED;  // Maintain D_MARKED bit.
    cas_succeeded =
        a_ptr->succ_.compare_exchange_weak(as_expect, x_succ,
                                           memory_order_acq_rel,
                                           memory_order_acquire);
  } while (!cas_succeeded && get<0>(as_expect) == &x);

  if (cas_succeeded) {
    b_ptr = move(x_succ);
    auto bp_expect = make_tuple(&x, UNMARKED);
    auto bp_assign = make_tuple(a_ptr, UNMARKED);
    if (!get<0>(b_ptr)->pred_.compare_exchange_strong(bp_expect,
                                                      move(bp_assign),
                                                      memory_order_release,
                                                      memory_order_relaxed) &&
        bp_expect == make_tuple(&x, MARKED)) {
      bp_assign = make_tuple(a_ptr, MARKED);
      get<0>(b_ptr)->pred_.compare_exchange_strong(bp_expect,
                                                   move(bp_assign),
                                                   memory_order_release,
                                                   memory_order_relaxed);
    }
  } else {
    b_ptr = move(as_expect);
  }

  x.succ_.store(make_tuple(nullptr, S_UNMARKED), memory_order_release);

  return tuple_cat(make_tuple(move(a_ptr)), move(b_ptr));
}

auto list::succ_elem_(elem& x) noexcept -> elem_ptr {
  elem_ptr s = succ_(x);
  if (s != nullptr) {
    while (get_elem_type(*s) == elem_type::iterator) {
      s = succ_(*s);
      if (s == nullptr) s = succ_(x);
      if (s == nullptr) return s;
    }
  }
  return s;
}

auto list::pred_elem_(elem& x) noexcept -> elem_ptr {
  elem_ptr p = pred_(x);
  if (p != nullptr) {
    while (get_elem_type(*p) == elem_type::iterator) {
      p = pred_(*p);
      if (p == nullptr) p = pred_(x);
      if (p == nullptr) return p;
    }
  }
  return p;
}

auto list::link_after_(elem& a, elem& x) noexcept -> link_result {
  for (;;) {
    elem_ptr b = succ_(a);
    if (!b) return LINK_LOST;
    switch (link_(a, x, *b)) {
    case XLINK_OK:
      return LINK_OK;
    case XLINK_TWICE:
      return LINK_TWICE;
    case XLINK_RETRY:
    case XLINK_LOST_B:
      break;
    case XLINK_LOST_A:
    case XLINK_LOST_AB:
      return LINK_LOST;
    }
  }
}

auto list::link_before_(elem& b, elem& x) noexcept -> link_result {
  for (;;) {
    elem_ptr a = pred_(b);
    if (!a) return LINK_LOST;
    switch (link_(*a, x, b)) {
    case XLINK_OK:
      return LINK_OK;
    case XLINK_TWICE:
      return LINK_TWICE;
    case XLINK_RETRY:
    case XLINK_LOST_A:
      break;
    case XLINK_LOST_B:
    case XLINK_LOST_AB:
      return LINK_LOST;
    }
  }
}

auto list::wait_link0(elem& x, size_t expect) noexcept -> void {
  unsigned int spin = SPIN;
  assert(is_unlinked_(x));

  auto lc = x.link_count_.load(memory_order_acquire);
  while (lc > expect) {
    spinwait(spin);
    lc = x.link_count_.load(memory_order_acquire);
  }
}

auto list::pop_front() noexcept -> elem_ptr {
  elem_ptr e = succ_elem_(data_);
  if (get_elem_type(*e) == elem_type::head) return nullptr;

  for (;;) {
    switch (unlink_(pred_(*e), *e, 1)) {
    case UNLINK_OK:
      return e;
    case UNLINK_RETRY:
      break;
    case UNLINK_FAIL:
      e = succ_elem_(data_);
      if (get_elem_type(*e) == elem_type::head) return nullptr;
      break;
    }
  }
}

auto list::pop_back() noexcept -> elem_ptr {
  elem_ptr e = pred_elem_(data_);
  if (get_elem_type(*e) == elem_type::head) return nullptr;

  for (;;) {
    switch (unlink_(pred_(*e), *e, 1)) {
    case UNLINK_OK:
      return e;
    case UNLINK_RETRY:
      break;
    case UNLINK_FAIL:
      e = pred_elem_(data_);
      if (get_elem_type(*e) == elem_type::head) return nullptr;
      break;
    }
  }
}

auto list::link_front(elem& e) noexcept -> bool {
  link_result rs = link_after_(const_cast<elem&>(data_), e);
  switch (rs) {
  case LINK_OK:
  case LINK_TWICE:
    break;
  case LINK_LOST:
    assert(rs != LINK_LOST);
    break;
  }
  return rs == LINK_OK;
}

auto list::link_back(elem& e) noexcept -> bool {
  link_result rs = link_before_(const_cast<elem&>(data_), e);
  switch (rs) {
  case LINK_OK:
  case LINK_TWICE:
    break;
  case LINK_LOST:
    assert(rs != LINK_LOST);
    break;
  }
  return rs == LINK_OK;
}

auto list::link_after(const position& p, elem& e, position* out) ->
    tuple<elem_ptr, bool> {
  link_result lr;
  position dummy;

  if (&p == out) {
    throw invalid_argument("input and output iterator may not be the same "
                           "(use a temporary!)");
  }
  if (out == nullptr)
    out = &dummy;
  else
    out->unlink();

  lr = link_after_(p.front_(), out->back_());
  assert(lr == LINK_OK);
  lr = link_after_(out->back_(), out->front_());
  assert(lr == LINK_OK);

  elem_ptr e_ptr = &e;
  lr = link_after_(out->back_(), e);
  switch (lr) {
  case LINK_OK:
    return make_tuple(e_ptr, true);
  case LINK_LOST:
    throw std::invalid_argument("unlinked position");
  case LINK_TWICE:
    out->unlink();
    return make_tuple(nullptr, false);
  }
  /* UNREACHABLE */
}

auto list::link_before(const position& p, elem& e, position* out) ->
    tuple<elem_ptr, bool> {
  link_result lr;
  position dummy;

  if (&p == out) {
    throw invalid_argument("input and output iterator may not be the same "
                           "(use a temporary!)");
  }
  if (out == nullptr)
    out = &dummy;
  else
    out->unlink();

  lr = link_before_(p.back_(), out->front_());
  assert(lr == LINK_OK);
  lr = link_before_(out->front_(), out->back_());
  assert(lr == LINK_OK);

  elem_ptr e_ptr = &e;
  lr = link_before_(out->front_(), e);
  switch (lr) {
  case LINK_OK:
    return make_tuple(e_ptr, true);
  case LINK_LOST:
    throw std::invalid_argument("unlinked position");
  case LINK_TWICE:
    out->unlink();
    return make_tuple(nullptr, false);
  }
  /* UNREACHABLE */
}

auto list::unlink(elem& e, position* out, size_t expect) ->
    tuple<elem_ptr, bool> {
  unlink_result ur;
  link_result lr;
  position dummy;

  if (out == nullptr)
    out = &dummy;
  else
    out->unlink();

  elem_ptr e_ptr = &e;  // expect + 1
  lr = link_after_(e, out->front_());
  if (lr == LINK_LOST) return make_tuple(nullptr, false);
  assert(lr == LINK_OK);

  for (;;) {
    lr = link_before_(e, out->back_());
    if (lr == LINK_LOST) return make_tuple(nullptr, false);
    assert(lr == LINK_OK);

    ur = unlink_(&out->back_(), e, expect + 1U);
    switch (ur) {
    case UNLINK_OK:
      return make_tuple(e_ptr, true);
    case UNLINK_RETRY:
      break;
    case UNLINK_FAIL:
      out->unlink();
      return make_tuple(nullptr, false);
    }

    out->unlink();
  }
  /* UNREACHABLE */
}

auto list::iterator_to(elem& e, position* out) noexcept -> bool {
  assert(out != nullptr);
  link_result lr;

  out->unlink();

  for (;;) {
    if (elem_linking_lock::is_locked(e)) return false;

    lr = list::link_before_(e, out->back_());
    switch (lr) {
    case list::LINK_TWICE:
      assert(lr != list::LINK_TWICE);
      /* FALLTHROUGH */
    case list::LINK_OK:
      break;
    case list::LINK_LOST:
      return false;
    }

    lr = list::link_after_(e, out->front_());
    switch (lr) {
    case list::LINK_TWICE:
      assert(lr != list::LINK_TWICE);
      /* FALLTHROUGH */
    case list::LINK_OK:
      return true;
    case list::LINK_LOST:
      break;  // Returning false, after unlinking back_() below. */
    }

    /* Unlink linked back_ part, so it can be relinked in the next loop. */
    for (unlink_result ur =
             list::unlink_(pred_(out->back_()), out->back_(), 0);
         ur != UNLINK_OK;
         ur = list::unlink_(pred_(out->back_()), out->back_(), 0)) {
      assert(ur != UNLINK_FAIL);
    }

    if (lr == list::LINK_LOST) return false;
  }
}


list::position::position() noexcept {}

list::position::position(const position& o) noexcept
: pos_(o.pos_),
  swap_(o.swap_)
{}

auto list::position::operator=(const position& o) noexcept -> position& {
  pos_ = o.pos_;
  swap_ = o.swap_;
  return *this;
}

list::position::~position() noexcept {}

auto list::position::unlink() noexcept -> void {
  for (iter_link& i : pos_)
    unlink_iter_(i);
  swap_ = 0;
}

auto list::position::link_around(elem& x) noexcept -> bool {
  link_result rs;

  unlink();

  rs = link_after_(x, front_());
  switch (rs) {
  case LINK_OK:
    break;
  case LINK_LOST:
    return false;
  case LINK_TWICE:
    assert(rs != LINK_TWICE);
    break;
  }

  rs = link_before_(x, back_());
  switch (rs) {
  case LINK_OK:
    break;
  case LINK_LOST:
    unlink();
    return false;
  case LINK_TWICE:
    assert(rs != LINK_TWICE);
    break;
  }

  return true;
}

auto list::position::step_forward() noexcept -> elem_ptr {
  link_result rs;

  if (!unlink_iter_(back_())) return nullptr;  // Not linked.

  for (;;) {
    elem_ptr e = succ_elem_(front_());
    rs = link_after_(*e, back_());
    switch (rs) {
    case LINK_OK:
      swap_ ^= 1U;
      return e;
    case LINK_LOST:
      break;
    case LINK_TWICE:
      assert(rs != LINK_TWICE);
    }
  }
}

auto list::position::step_backward() noexcept -> elem_ptr {
  link_result rs;

  if (!unlink_iter_(front_())) return nullptr;  // Not linked.

  for (;;) {
    elem_ptr e = pred_elem_(back_());
    rs = link_before_(*e, front_());
    switch (rs) {
    case LINK_OK:
      swap_ ^= 1U;
      return e;
    case LINK_LOST:
      break;
    case LINK_TWICE:
      assert(rs != LINK_TWICE);
    }
  }
}

auto list::position::operator==(const position& o) const noexcept -> bool {
  if (is_unlinked_(back_()) && is_unlinked_(o.back_())) return true;
  if (is_unlinked_(back_()) || is_unlinked_(o.back_())) return false;

  elem_ptr s1 = succ_(back_()),
           s2 = succ_(o.back_());
  while (get_elem_type(*s1) == elem_type::iterator &&
         get_elem_type(*s2) == elem_type::iterator) {
    if (s1 == &o.back_() || s2 == &back_()) return true;

    s1 = succ_(*s1);
    if (s1 == nullptr) s1 = succ_(back_());
    s2 = succ_(*s2);
    if (s2 == nullptr) s2 = succ_(o.back_());
  }

  while (get_elem_type(*s1) == elem_type::iterator) {
    if (s1 == &o.back_()) return true;

    s1 = succ_(*s1);
    if (s1 == nullptr) s1 = succ_(back_());
  }

  while (get_elem_type(*s2) == elem_type::iterator) {
    if (s2 == &back_()) return true;

    s2 = succ_(*s2);
    if (s2 == nullptr) s2 = succ_(o.back_());
  }

  return false;
}

auto list::position::unlink_iter_(iter_link& i) noexcept -> bool {
  for (;;) {
    elem_ptr p = pred_(i);
    if (!p) return false;

    switch (unlink_(move(p), i, 0)) {
    case UNLINK_OK:
      return true;
    case UNLINK_FAIL:
      return false;
    case UNLINK_RETRY:
      break;
    }
  }
}


}} /* namespace ilias::ll_list_detail */
