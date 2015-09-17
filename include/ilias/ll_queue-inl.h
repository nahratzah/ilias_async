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
#ifndef ILIAS_LL_QUEUE_INL_H
#define ILIAS_LL_QUEUE_INL_H

#include "ll_queue.h"

namespace ilias {
namespace ll_queue_detail {


inline auto ll_qhead::elem::ensure_unused() const noexcept -> void {
  hazard_t::wait_unused(token, *this);
  std::atomic_thread_fence(std::memory_order_acq_rel);
}

inline ll_qhead::elem::elem(tag_head) noexcept
: m_succ(this)
{}

inline ll_qhead::elem::elem(const elem&) noexcept
: elem()
{}

inline ll_qhead::elem::elem(elem&&) noexcept
: elem()
{}

inline ll_qhead::elem::~elem() noexcept {
  this->ensure_unused();
}

inline auto ll_qhead::elem::operator=(const elem&) noexcept -> elem& {
  return *this;
}

inline auto ll_qhead::elem::operator=(elem&&) noexcept -> elem& {
  return *this;
}

inline auto ll_qhead::elem::operator==(const elem&) const noexcept -> bool {
  return true;
}


inline ll_qhead::hazard_t::hazard_t() noexcept
: hazard<token_, elem>(token)
{}


inline auto ll_qhead::push_back(elem* e) -> void {
  if (!e)
    throw std::invalid_argument("ll_queue: cannot push back nil");

  push_back_(e);
}

inline auto ll_qhead::pop_front() noexcept -> elem* {
  return pop_front_();
}

inline auto ll_qhead::push_front(elem* e) -> void {
  if (!e)
    throw std::invalid_argument("ll_queue: cannot push back nil");

  push_front_(e);
}

inline auto ll_qhead::size() const noexcept -> size_type {
  return m_size.load(std::memory_order_consume);
}

inline auto ll_qhead::empty() const noexcept -> bool {
  return (m_head.m_succ.load(std::memory_order_consume) == &m_head);
}

inline auto ll_qhead::is_lock_free() const noexcept -> bool {
  hazard_t hz;
  return atomic_is_lock_free(&m_head.m_succ) &&
         atomic_is_lock_free(&m_tail) &&
         atomic_is_lock_free(&hz);
}

inline auto atomic_is_lock_free(const ll_qhead* h) noexcept -> bool {
  return h && h->is_lock_free();
}


} /* namespace ilias::ll_queue_detail */


namespace {


template<typename Type, typename Tag>
auto atomic_is_lock_free(const ll_queue<Type, Tag>* q) noexcept -> bool {
  return q && q->is_lock_free();
}

template<typename Type, typename AcqRel, typename Tag>
auto atomic_is_lock_free(const ll_smartptr_queue<Type, AcqRel, Tag>* q)
    noexcept -> bool {
  return q && q->is_lock_free();
}


} /* namespace ilias::<unnamed> */


template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::link_convert(pointer p) noexcept ->
    ll_queue_hook<Tag>* {
  using rv_type = ll_queue_hook<Tag>*;
  return (p ? rv_type{ p } : nullptr);
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::unlink_convert(ll_queue_hook<Tag>* p) noexcept ->
    pointer {
  return (p ? &static_cast<reference>(*p) : nullptr);
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::unlink_convert(ll_queue_detail::ll_qhead::elem* e)
    noexcept -> pointer {
  return (e ?
          unlink_convert(&static_cast<ll_queue_hook<Tag>&>(*e)) :
          nullptr);
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::empty() const noexcept -> bool {
  return m_impl.empty();
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::size() const noexcept -> size_type {
  return m_impl.size();
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::push_back(pointer p) -> void {
  m_impl.push_back(link_convert(p));
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::pop_front() noexcept -> pointer {
  return unlink_convert(m_impl.pop_front());
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::push_front(pointer p) -> void {
  m_impl.push_front(link_convert(p));
}

template<typename Type, typename Tag>
auto ll_queue<Type, Tag>::is_lock_free() const noexcept -> bool {
  return m_impl.is_lock_free();
}


template<typename Type>
ll_queue<Type, no_intrusive_tag>::elem::elem(const_reference v)
    noexcept(std::is_nothrow_copy_constructible<value_type>::value)
: m_value(v)
{}

template<typename Type>
ll_queue<Type, no_intrusive_tag>::elem::elem(rvalue_reference v)
    noexcept(std::is_nothrow_move_constructible<value_type>::value)
: m_value(v)
{}

template<typename Type>
template<typename... Args>
ll_queue<Type, no_intrusive_tag>::elem::elem(Args&&... args)
    noexcept(std::is_nothrow_constructible<value_t, Args...>::value)
: m_value(std::forward<Args>(args)...)
{}


template<typename Type>
ll_queue<Type, no_intrusive_tag>::~ll_queue()
    noexcept(std::is_nothrow_destructible<elem>::value) {
  while (pop_front());
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::pop_front()
    noexcept(std::is_nothrow_move_constructible<value_type>::value ||
             std::is_nothrow_copy_constructible<value_type>::value) ->
    pointer {
  std::unique_ptr<elem> e = m_impl.pop_front();
  pointer rv;
  if (e) rv.reset(std::move_if_noexcept(e->m_value));
  return rv;
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::push_back(const_reference e) -> void {
  m_impl.push_back(new elem(e));
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::push_back(rvalue_reference e) -> void {
  m_impl.push_back(new elem(std::move(e)));
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::push_front(const_reference e) -> void {
  m_impl.push_front(new elem(e));
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::push_front(rvalue_reference e) -> void {
  m_impl.push_front(new elem(std::move(e)));
}

template<typename Type>
template<typename... Args>
auto ll_queue<Type, no_intrusive_tag>::emplace_back(Args&&... args) -> void {
  m_impl.push_back(new elem(std::forward<Args>(args)...));
}

template<typename Type>
template<typename... Args>
auto ll_queue<Type, no_intrusive_tag>::emplace_front(Args&&... args) -> void {
  m_impl.push_front(new elem(std::forward<Args>(args)...));
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::size() const noexcept -> size_type {
  return m_impl.size();
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::empty() const noexcept -> bool {
  return m_impl.empty();
}

template<typename Type>
auto ll_queue<Type, no_intrusive_tag>::is_lock_free() const noexcept -> bool {
  return m_impl.is_lock_free();
}


template<typename Type, typename AcqRel, typename Tag>
ll_smartptr_queue<Type, AcqRel, Tag>::~ll_smartptr_queue()
    noexcept(std::is_nothrow_destructible<pointer>::value) {
  while (pop_front());
}

template<typename Type, typename AcqRel, typename Tag>
auto ll_smartptr_queue<Type, AcqRel, Tag>::empty() const noexcept -> bool {
  return m_impl.empty();
}

template<typename Type, typename AcqRel, typename Tag>
auto ll_smartptr_queue<Type, AcqRel, Tag>::size() const noexcept -> size_type {
  return m_impl.size();
}

template<typename Type, typename AcqRel, typename Tag>
auto ll_smartptr_queue<Type, AcqRel, Tag>::push_back(pointer p) -> void {
  m_impl.push_back(p.release());
}

template<typename Type, typename AcqRel, typename Tag>
auto ll_smartptr_queue<Type, AcqRel, Tag>::pop_front() noexcept -> pointer {
  return pointer(m_impl.pop_front(), false);
}

template<typename Type, typename AcqRel, typename Tag>
auto ll_smartptr_queue<Type, AcqRel, Tag>::push_front(pointer p) -> void {
  m_impl.push_front(p.release());
}

template<typename Type, typename AcqRel, typename Tag>
auto ll_smartptr_queue<Type, AcqRel, Tag>::is_lock_free() const noexcept ->
    bool {
  return this->m_impl.is_lock_free();
}


template<typename Type, typename AcqRel>
auto ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>::empty()
    const noexcept -> bool {
  return this->m_impl.empty();
}

template<typename Type, typename AcqRel>
auto ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>::size()
    const noexcept -> size_type {
  return this->m_impl.size();
}

template<typename Type, typename AcqRel>
auto ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>::push_back(pointer p) ->
    void {
  m_impl.push_back(std::move(p));
}

template<typename Type, typename AcqRel>
auto ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>::pop_front() noexcept ->
    pointer {
  auto pp = m_impl.pop_front();
  pointer p = (pp ? std::move(*pp) : nullptr);
  return p;
}

template<typename Type, typename AcqRel>
auto ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>::push_front(
    pointer p) -> void {
  m_impl.push_front(std::move(p));
}

template<typename Type, typename AcqRel>
auto ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>::is_lock_free()
    const noexcept -> bool {
  return this->m_impl.is_lock_free();
}


} /* namespace ilias */

#endif /* ILIAS_LL_QUEUE_INL_H */
