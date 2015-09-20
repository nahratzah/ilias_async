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
#include <ilias/ll_queue.h>
#include <cassert>

namespace ilias {
namespace ll_queue_detail {


const ll_qhead::token_ ll_qhead::token = {};
constexpr ll_qhead::atom_flags ll_qhead::UNMARKED;
constexpr ll_qhead::atom_flags ll_qhead::MARKED;


auto ll_qhead::push_back_(elem* e) noexcept -> void {
  using std::get;
  using std::memory_order_acq_rel;
  using std::memory_order_acquire;
  using std::memory_order_relaxed;
  using std::memory_order_release;

  assert(e);
  e->ensure_unused();
  e->m_succ.store(atom_vt(&m_head, UNMARKED), memory_order_relaxed);

  hazard_t hz;
  elem* p = m_tail.load(memory_order_relaxed);

  m_size.fetch_add(1, memory_order_release);

  for (;;) {
    bool done = false;
    atom_vt p_succ = atom_vt(&m_head, UNMARKED);

    hz.do_hazard(*p,
                 [&]() {
                   elem* p_ = m_tail.load(memory_order_acquire);
                   if (p != p_) {
                     p = p_;
                     return;
                   }

                   if (p->m_succ.compare_exchange_weak(p_succ,
                                                       atom_vt(e, UNMARKED),
                                                       memory_order_acq_rel,
                                                       memory_order_acquire)) {
                     m_tail.compare_exchange_strong(p, e,
                                                    memory_order_release,
                                                    memory_order_relaxed);
                     done = true;
                     return;
                   }

                   if (m_tail.compare_exchange_weak(p, get<0>(p_succ),
                       memory_order_release,
                       memory_order_relaxed))
                     p = get<0>(p_succ);
                 },
                 []() {
                   assert(false);  // We don't use grants.
                 });

    if (done) return;

    if (get<1>(p_succ) == MARKED) {
      if (p == &m_head) p = get<0>(p_succ);
      p = pop_front_aid_(hz, p, true);
    }
  }
}

auto ll_qhead::pop_front_() noexcept -> ll_qhead::elem* {
  using std::get;
  using std::memory_order_acquire;
  using std::memory_order_acq_rel;
  using std::memory_order_consume;
  using std::memory_order_relaxed;
  using std::memory_order_release;

  hazard_t hz;

  atom_vt e = m_head.m_succ.load(memory_order_relaxed);
  do {
    if (get<1>(e) == MARKED)
      e = atom_vt(pop_front_aid_(hz, get<0>(e), true), UNMARKED);
  } while (get<0>(e) != &m_head &&
           !m_head.m_succ.compare_exchange_weak(e, atom_vt(get<0>(e), MARKED),
                                                memory_order_acquire,
                                                memory_order_relaxed));
  assert(get<1>(e) == UNMARKED);
  if (get<0>(e) == &m_head) return nullptr;

  m_size.fetch_sub(1U, memory_order_release);
  pop_front_aid_(hz, get<0>(e), false);
  return get<0>(e);
}

auto ll_qhead::push_front_(elem* e) noexcept -> void {
  using std::get;
  using std::memory_order_release;
  using std::memory_order_relaxed;

  assert(e);
  e->ensure_unused();

  m_size.fetch_add(1U, memory_order_release);

  atom_vt s = m_head.m_succ.load(memory_order_relaxed);
  for (;;) {
    if (get<1>(s) == MARKED) {
      hazard_t hz;
      s = atom_vt(pop_front_aid_(hz, get<0>(s), true), UNMARKED);
    }

    e->m_succ.store(s, memory_order_relaxed);
    if (m_head.m_succ.compare_exchange_weak(s, atom_vt(e, UNMARKED),
                                            memory_order_release,
                                            memory_order_relaxed))
      return;
  }
}

auto ll_qhead::pop_front_aid_(hazard_t& hz, elem* s, bool until_valid)
    noexcept -> elem* {
  using std::get;
  using std::memory_order_acquire;
  using std::memory_order_release;
  using std::memory_order_acq_rel;
  using std::memory_order_relaxed;

  do {
    assert(s != nullptr);
    assert(s != &m_head);
    atom_vt h_succ;  // Will contain m_head.m_succ.

    hz.do_hazard(*s,
                 [&]() {
                   /* Validate hazard acquisition. */
                   h_succ = m_head.m_succ.load(memory_order_acquire);
                   if (get<0>(h_succ) != s) return;
                   assert(get<1>(h_succ) == MARKED);

                   /*
                    * Mark s.m_succ, to prevent it from changing.
                    * Also load s.m_succ into ss.
                    */
                   atom_vt ss = atom_vt(&m_head, UNMARKED);
                   while (get<1>(ss) == UNMARKED &&
                          !s->m_succ.compare_exchange_weak(
                              ss, atom_vt(get<0>(ss), MARKED),
                              memory_order_acq_rel,
                              memory_order_acquire)) {
                     // ss == &m_head  ==>  ss is unmarked
                     assert(get<1>(ss) == UNMARKED || get<0>(ss) != &m_head);
                   }

                   /* Move tail out of the way. */
                   {
                     elem* expect = s;
                     m_tail.compare_exchange_strong(expect, get<0>(ss),
                                                    memory_order_release,
                                                    memory_order_relaxed);
                   }

                   /* Update m_head to point at successor of s. */
                   get<1>(ss) = UNMARKED;
                   if (m_head.m_succ.compare_exchange_strong(
                           h_succ, ss,
                           memory_order_acq_rel,
                           memory_order_acquire))
                     h_succ = ss;
                 },
                 []() {
                   assert(false);  // We don't use grants.
                 });

    s = get<0>(h_succ);
    if (get<1>(h_succ) == UNMARKED) return s;
  } while (until_valid);
  return s;
}


}} /* namespace ilias::ll_queue_detail */
