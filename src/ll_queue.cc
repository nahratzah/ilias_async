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

namespace ilias {
namespace ll_queue_detail {


const ll_qhead::token_ ll_qhead::token = {};


auto ll_qhead::push_back_(elem* e) noexcept -> void {
  using std::memory_order_relaxed;
  using std::memory_order_release;

  assert(e);
  e->ensure_unused();
  e->m_succ.store(&m_head, memory_order_relaxed);

  hazard_t hz;
  elem* p = m_tail.load(memory_order_relaxed);
  bool done = false;

  while (!done) {
    hz.do_hazard(*p,
                 [&]() {
                   elem* p_ = m_tail.load(memory_order_relaxed);
                   if (p != p_) {
                     p = p_;
                     return;
                   }

                   elem* expect = &m_head;
                   if (p->m_succ.compare_exchange_weak(expect, e,
                                                       memory_order_release,
                                                       memory_order_relaxed)) {
                     m_tail.compare_exchange_strong(p, e,
                                                    memory_order_relaxed,
                                                    memory_order_relaxed);
                     done = true;
                   } else if (m_tail.compare_exchange_weak(p, expect,
                                  memory_order_relaxed,
                                  memory_order_relaxed)) {
                     p = expect;
                   }
                 },
                 []() {
                   assert(false);
                 });
  }

  m_size.fetch_add(1, memory_order_release);
}

auto ll_qhead::pop_front_() noexcept -> ll_qhead::elem* {
  using std::memory_order_acq_rel;
  using std::memory_order_consume;
  using std::memory_order_relaxed;
  using std::memory_order_release;

  hazard_t hz;
  elem* e = m_head.m_succ.load(memory_order_consume);
  bool done = false;

  while (!done && e != &m_head) {
    hz.do_hazard(*e,
                 [&]() {
                   elem* e_ = m_head.m_succ.load(memory_order_relaxed);
                   if (e != e_) {
                     e = e_;
                     return;
                   }

                   elem* succ = e->m_succ.load(memory_order_relaxed);
                   if (m_head.m_succ.compare_exchange_strong(
                                         e, succ,
                                         memory_order_acq_rel,
                                         memory_order_consume)) {
                     m_tail.compare_exchange_strong(e_, &m_head,
                                                    memory_order_relaxed,
                                                    memory_order_relaxed);
                     done = true;
                   }
                 },
                 []() {
                   assert(false);
                 });
  }

  if (e == &m_head) e = nullptr;
  if (e) m_size.fetch_sub(1U, memory_order_release);
  return e;
}

auto ll_qhead::push_front_(elem* e) noexcept -> void {
  using std::memory_order_release;
  using std::memory_order_relaxed;

  assert(e);
  e->ensure_unused();

  elem* s = m_head.m_succ.load(memory_order_relaxed);
  do {
    e->m_succ.store(s, memory_order_relaxed);
  } while (!m_head.m_succ.compare_exchange_weak(s, e,
                                                memory_order_release,
                                                memory_order_relaxed));

  m_size.fetch_add(1U, memory_order_release);
}


}} /* namespace ilias::ll_queue_detail */
