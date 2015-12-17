/*
 * Copyright (c) 2015 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef _ILIAS_ASYNC_LL_INL_H_
#define _ILIAS_ASYNC_LL_INL_H_

#include "ll_list.h"
#include <cassert>
#include <stdexcept>

namespace ilias {
namespace ll_list_detail {


inline auto elem_acqrel::acquire(const elem& e, size_t nrefs) noexcept ->
    void {
  size_t old = e.link_count_.fetch_add(nrefs, memory_order_acquire);
  assert(old + nrefs >= old);  // Wrap-around.
}

inline auto elem_acqrel::release(const elem& e, size_t nrefs) noexcept ->
    void {
  size_t old = e.link_count_.fetch_sub(nrefs, memory_order_release);
  assert(old >= nrefs);  // Insufficient references.
}


inline auto atomic_is_lock_free(const elem* e) noexcept -> bool {
  return e &&
      atomic_is_lock_free(&e->link_count_) &&
      atomic_is_lock_free(&e->linking_) &&
      atomic_is_lock_free(&e->succ_) && atomic_is_lock_free(&e->pred_);
}


inline auto list::is_unlinked_(const elem& e) noexcept -> bool {
  const auto e_p = e.pred_.load_no_acquire(memory_order_acquire);
  return get<0>(e_p) == nullptr || get<1>(e_p) == MARKED;
}

inline auto list::get_elem_type(const elem& e) noexcept -> elem_type {
  return e.type_;
}

inline auto list::is_lock_free() const noexcept -> bool {
  return atomic_is_lock_free(&data_);
}

inline auto atomic_is_lock_free(const list* l) noexcept -> bool {
  return l->is_lock_free();
}


inline iter_link::iter_link(const iter_link& o) noexcept
: iter_link()
{
  list::link_result rv = list::link_after_(const_cast<iter_link&>(o), *this);
  assert(rv == list::LINK_OK);  // Fail and Twice are not possible.
}

inline auto iter_link::operator=(const iter_link& o) noexcept -> iter_link& {
  {
    list::unlink_result ur;
    do {
      elem_ptr p = list::pred_(*this);
      ur = (p ? list::unlink_(move(p), *this, 0) : list::UNLINK_FAIL);
    } while (ur == list::UNLINK_RETRY);
  }

  if (!list::is_unlinked_(o)) {
    list::link_result rv = list::link_after_(const_cast<iter_link&>(o), *this);
    assert(rv == list::LINK_OK);  // Fail and Twice are not possible.
  }
  return *this;
}

inline iter_link::~iter_link() noexcept {
  list::unlink_result ur;
  do {
    elem_ptr p = list::pred_(*this);
    if (!p) return;
    ur = list::unlink_(move(p), *this, 0);
  } while (ur == list::UNLINK_RETRY);
}


template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_elem_(reference r)
    noexcept -> elem_ptr {
  hook_type& hook = r;
  return elem_ptr(&hook);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_elem_(const_reference r)
    noexcept -> elem_ptr {
  const hook_type& hook = r;
  return elem_ptr(const_cast<hook_type*>(&hook));
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_elem_(const pointer& p)
    noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  return as_elem_(*p);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_elem_(const const_pointer& p)
    noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  return as_elem_(*p);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_type_(const elem_ptr& p)
    noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(list::get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return pointer(&v);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_type_unlinked_(
    const elem_ptr& p) noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(list::get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return pointer(&v, false);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::release_pointer_(pointer&& p)
    noexcept -> void {
  p.release();
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::release_pointer_(
    const_pointer&& p) noexcept -> void {
  p.release();
}


template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_elem_(reference r)
    noexcept -> elem_ptr {
  hook_type& hook = r;
  return elem_ptr(&hook);
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_elem_(const_reference r)
    noexcept -> elem_ptr {
  const hook_type& hook = r;
  return elem_ptr(const_cast<hook_type*>(&hook));
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_elem_(const pointer& p)
    noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  return as_elem_(*p);
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_elem_(
    const const_pointer& p) noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  return as_elem_(*p);
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_type_(
    const elem_ptr& p) noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(list::get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return &v;
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_type_unlinked_(
    const elem_ptr& p) noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(list::get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return &v;
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::release_pointer_(pointer&& p)
    noexcept -> void {
  p = nullptr;
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::release_pointer_(
    const_pointer&& p) noexcept -> void {
  p = nullptr;
}


} /* namespace ilias::ll_list_detail */


template<typename T, typename Tag, typename AcqRel>
ll_smartptr_list<T, Tag, AcqRel>::~ll_smartptr_list() noexcept {
  clear();
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::empty() const noexcept -> bool {
  return data_.empty();
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::size() const noexcept -> size_type {
  return data_.size();
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::pop_front() noexcept -> pointer {
  return this->as_type_unlinked_(data_.pop_front());
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::pop_back() noexcept -> pointer {
  return this->as_type_unlinked_(data_.pop_back());
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator_to(reference r)
    noexcept -> iterator {
  iterator rv;

  rv.ptr_ = &r;
  ll_list_detail::elem_ptr e_ptr = this->as_elem_(r);
  if (!ll_list_detail::list::iterator_to(*e_ptr, &rv.pos_))
    data_.init_end(rv.pos_);

  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator_to(const_reference r)
    noexcept -> const_iterator {
  const_iterator rv;

  rv.ptr_ = &r;
  ll_list_detail::elem_ptr e_ptr = this->as_elem_(r);
  if (!ll_list_detail::list::iterator_to(*e_ptr, &rv.pos_))
    data_.init_end(rv.pos_);

  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator_to(const pointer& p) ->
    iterator {
  if (p == nullptr) throw std::invalid_argument("null pointer");
  return iterator_to(*p);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator_to(const const_pointer& p) ->
    const_iterator {
  if (p == nullptr) throw std::invalid_argument("null pointer");
  return iterator_to(*p);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::begin() noexcept -> iterator {
  iterator rv;
  auto first = data_.init_begin(rv.pos_);
  if (ll_list_detail::list::get_elem_type(*first) !=
      ll_list_detail::elem_type::head)
    rv.ptr_ = this->as_type_(first);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::end() noexcept -> iterator {
  iterator rv;
  auto head = data_.init_end(rv.pos_);
  assert(ll_list_detail::list::get_elem_type(*head) ==
         ll_list_detail::elem_type::head);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::begin() const noexcept ->
    const_iterator {
  const_iterator rv;
  rv.ptr_ = this->as_type_(data_.init_begin(rv.pos_));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::end() const noexcept -> const_iterator {
  const_iterator rv;
  auto head = data_.init_end(rv.pos_);
  assert(ll_list_detail::list::get_elem_type(*head) ==
         ll_list_detail::elem_type::head);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_front(pointer p) ->
    bool {
  if (p == nullptr) throw std::invalid_argument("null element");
  bool rv = data_.link_front(*this->as_elem_(p));
  if (rv) this->release_pointer_(std::move(p));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_back(pointer p) ->
    bool {
  if (p == nullptr) throw std::invalid_argument("null element");
  bool rv = data_.link_back(*this->as_elem_(p));
  if (rv) this->release_pointer_(std::move(p));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_after(const const_iterator& i,
                                                  pointer p) ->
    std::pair<iterator, bool> {
  using std::get;
  std::pair<iterator, bool> result;

  if (p == nullptr) throw std::invalid_argument("null element");
  auto link_result =
      data_.link_after(i.pos_, this->as_elem_(p), &result.first.pos_);

  result.first = this->as_type_(get<0>(link_result));
  result.second = get<1>(link_result);
  if (get<1>(link_result)) this->release_pointer_(std::move(p));
  return result;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_after(const iterator& i,
                                                  pointer p) ->
    std::pair<iterator, bool> {
  using std::get;
  std::pair<iterator, bool> result;

  if (p == nullptr) throw std::invalid_argument("null element");
  auto link_result =
      data_.link_after(i.pos_, this->as_elem_(p), &get<0>(result).pos_);

  result.first = this->as_type_(get<0>(link_result));
  result.second = get<1>(link_result);
  if (get<1>(link_result)) this->release_pointer_(std::move(p));
  return result;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_before(const const_iterator& i,
                                                   pointer p) ->
    std::pair<iterator, bool> {
  using std::get;
  std::pair<iterator, bool> result;

  if (p == nullptr) throw std::invalid_argument("null element");
  auto link_result =
      data_.link_before(i.pos_, this->as_elem_(p), &get<0>(result).pos_);

  result.first = this->as_type_(get<0>(link_result));
  result.second = get<1>(link_result);
  if (get<1>(link_result)) this->release_pointer_(std::move(p));
  return result;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_before(const iterator& i,
                                                   pointer p) ->
    std::pair<iterator, bool> {
  using std::get;
  std::pair<iterator, bool> result;

  if (p == nullptr) throw std::invalid_argument("null element");
  auto link_result =
      data_.link_before(i.pos_, this->as_elem_(p), &get<0>(result).pos_);

  result.first = this->as_type_(get<0>(link_result));
  result.second = get<1>(link_result);
  if (get<1>(link_result)) this->release_pointer_(std::move(p));
  return result;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link(const const_iterator& i,
                                            pointer p) -> iterator {
  iterator rv;
  rv.pos_ = i.pos_;
  rv.ptr_ = i.ptr_;

  bool link_success;
  tie(std::ignore, link_success) =
      data_.link_before(i.pos_, this->as_elem_(p), nullptr);
  if (link_success) this->release_pointer_(std::move(p));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link(const iterator& i,
                                            pointer p) -> iterator {
  iterator rv;
  rv.pos_ = i.pos_;
  rv.ptr_ = i.ptr_;

  bool link_success;
  tie(std::ignore, link_success) =
      data_.link_before(i.pos_, *this->as_elem_(p), nullptr);
  if (link_success) this->release_pointer_(std::move(p));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
template<typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::clear_and_dispose(Disposer disp)
    noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    void {
  while (pointer p = pop_front())
    disp(std::move(p));
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::clear()
    noexcept -> void {
  clear_and_dispose([](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
template<typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::erase_and_dispose(
    const const_iterator& i, Disposer disp)
    noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    iterator {
  using std::tie;

  const auto& i_ptr = i.get();
  if (!i_ptr) throw std::invalid_argument("invalid iterator");
  iterator out;

  ll_list_detail::elem& e = *this->as_elem_(i_ptr);  // expect = 0
  ll_list_detail::elem_ptr ep;
  bool unlink_success;
  tie(ep, unlink_success) = data_.unlink(e, &out.pos_, 0);
  if (unlink_success)
    disp(this->as_type_unlinked_(ep));
  else
    out.pos_ = i.pos_;
  ++out;
  return out;
}

template<typename T, typename Tag, typename AcqRel>
template<typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::erase_and_dispose(
    const iterator& i, Disposer disp)
    noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    iterator {
  using std::tie;

  const auto& i_ptr = i.get();
  if (!i_ptr) throw std::invalid_argument("invalid iterator");
  iterator out;

  ll_list_detail::elem& e = *this->as_elem_(i_ptr);  // expect = 0
  ll_list_detail::elem_ptr ep;
  bool unlink_success;
  tie(ep, unlink_success) = data_.unlink(e, &out.pos_, 0);
  if (unlink_success)
    disp(this->as_type_unlinked_(ep));
  else
    out.pos_ = i.pos_;
  ++out;
  return out;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::erase(const const_iterator& i)
    noexcept -> iterator {
  return erase_and_dispose(i, [](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::erase(const iterator& i)
    noexcept-> iterator {
  return erase_and_dispose(i, [](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
template<typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::erase_and_dispose(
    const const_iterator& b, const const_iterator& e, Disposer disp)
    noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    iterator {
  iterator i;
  i.pos_ = b.pos_;
  i.ptr_ = const_pointer_cast<value_type>(b.ptr_);

  while (i != e) {
    bool fired = false;
    i = erase(i, [&fired, &disp](pointer&& p) {
                   disp(std::move(p));
                   fired = true;
                 });
    if (!fired && i != e) ++i;
  }
  return i;
}

template<typename T, typename Tag, typename AcqRel>
template<typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::erase_and_dispose(
    const iterator& b, const iterator& e, Disposer disp)
    noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    iterator {
  iterator i;
  i.pos_ = b.pos_;
  i.ptr_ = b.ptr_;

  while (i != e) {
    bool fired = false;
    i = erase(i, [&fired, &disp](pointer&& p) {
                   disp(std::move(p));
                   fired = true;
                 });
    if (!fired && i != e) ++i;
  }
  return i;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::erase(
    const const_iterator& b, const const_iterator& e) noexcept -> iterator {
  return erase_and_dispose(b, e, [](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::erase(
    const iterator& b, const iterator& e) noexcept -> iterator {
  return erase_and_dispose(b, e, [](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
template<typename Predicate, typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::remove_and_dispose_if(Predicate pred,
                                                             Disposer disp)
    noexcept(noexcept(std::declval<Predicate&>()(
                          std::declval<const_reference>())) &&
             noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    void {
  iterator i = begin(), e = end();
  while (i != e) {
    bool fired = false;
    if (pred(*i)) {
      i = erase_and_dispose(i, [&fired, &disp](pointer&& p) {
                                 disp(std::move(p));
                                 fired = true;
                               });
    }
    if (!fired) ++i;
  }
}

template<typename T, typename Tag, typename AcqRel>
template<typename Disposer>
auto ll_smartptr_list<T, Tag, AcqRel>::remove_and_dispose(const_reference v,
                                                          Disposer disp)
    noexcept(noexcept(std::declval<const_reference>() ==
                          std::declval<const_reference>()) &&
             noexcept(std::declval<Disposer&>()(std::declval<pointer>()))) ->
    void {
  return remove_and_dispose_if([&v](const_reference& x) { return x == v; },
                               std::move(disp));
}

template<typename T, typename Tag, typename AcqRel>
template<typename Predicate>
auto ll_smartptr_list<T, Tag, AcqRel>::remove_if(Predicate pred)
    noexcept(noexcept(std::declval<Predicate&>()(
                          std::declval<const_reference>()))) ->
    void {
  return remove_and_dispose_if(std::move(pred), [](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
template<typename Predicate>
auto ll_smartptr_list<T, Tag, AcqRel>::remove(const_reference v)
    noexcept(noexcept(std::declval<Predicate&>()(
                          std::declval<const_reference>()))) ->
    void {
  return remove_and_dispose_if([&v](const_reference& x) { return x == v; },
                               [](const pointer&) {});
}

template<typename T, typename Tag, typename AcqRel>
template<typename Functor>
auto ll_smartptr_list<T, Tag, AcqRel>::visit(Functor fn)
    noexcept(noexcept(std::declval<Functor&>()(std::declval<reference>()))) ->
    Functor {
  for (reference i : *this) fn(i);
  return fn;
}

template<typename T, typename Tag, typename AcqRel>
template<typename Functor>
auto ll_smartptr_list<T, Tag, AcqRel>::visit(Functor fn)
    const
    noexcept(noexcept(std::declval<Functor&>()(std::declval<reference>()))) ->
    Functor {
  for (const_reference i : *this) fn(i);
  return fn;
}


template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator==(
    const iterator& o) const noexcept -> bool {
  return pos_ == o.pos_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator!=(
    const iterator& o) const noexcept -> bool {
  return !(*this == o);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::get() const noexcept ->
    pointer {
  return ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator->() const noexcept ->
    value_type* {
  assert(ptr_ != nullptr);
  return &*ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator*() const noexcept ->
    reference {
  assert(ptr_ != nullptr);
  return *ptr_;
}

template<typename T, typename Tag, typename AcqRel>
ll_smartptr_list<T, Tag, AcqRel>::iterator::operator pointer() const noexcept {
  return *ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::release() noexcept ->
    pointer {
  pointer rv = std::move(ptr_);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator++()
    noexcept -> iterator& {
  auto e = pos_.step_forward();
  if (e == nullptr ||
      (ll_list_detail::list::get_elem_type(*e) ==
       ll_list_detail::elem_type::head)) {
    ptr_ = nullptr;
  } else {
    ptr_ = this->as_type_(e);
  }
  return *this;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator++(int)
    noexcept -> iterator {
  iterator clone = *this;
  ++clone;
  return clone;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator--()
    noexcept -> iterator& {
  auto e = pos_.step_backward();
  if (e == nullptr ||
      (ll_list_detail::list::get_elem_type(*e) ==
       ll_list_detail::elem_type::head)) {
    ptr_ = nullptr;
  } else {
    ptr_ = this->as_type_(e);
  }
  return *this;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator--(int)
    noexcept -> iterator {
  iterator clone = *this;
  --clone;
  return clone;
}


template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator==(
    const const_iterator& o) const noexcept -> bool {
  return pos_ == o.pos_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator!=(
    const const_iterator& o) const noexcept -> bool {
  return !(*this == o);
}

template<typename T, typename Tag, typename AcqRel>
ll_smartptr_list<T, Tag, AcqRel>::const_iterator::const_iterator(
    const iterator& o) noexcept
: pos_(o.pos_),
  ptr_(o.ptr_)
{}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::get() const noexcept ->
    pointer {
  return ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator->()
    const noexcept -> value_type* {
  assert(ptr_ != nullptr);
  return &*ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator*()
    const noexcept -> reference {
  assert(ptr_ != nullptr);
  return *ptr_;
}

template<typename T, typename Tag, typename AcqRel>
ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator pointer()
    const noexcept {
  return *ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::release()
    noexcept -> pointer {
  pointer rv = std::move(ptr_);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator++()
    noexcept -> const_iterator& {
  auto e = pos_.step_forward();
  if (e == nullptr ||
      (ll_list_detail::list::get_elem_type(*e) ==
       ll_list_detail::elem_type::head)) {
    ptr_ = nullptr;
  } else {
    ptr_ = this->as_type_(e);
  }
  return *this;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator++(int)
    noexcept -> const_iterator {
  const_iterator clone = *this;
  ++clone;
  return clone;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator--()
    noexcept -> const_iterator& {
  auto e = pos_.step_backward();
  if (e == nullptr ||
      (ll_list_detail::list::get_elem_type(*e) ==
       ll_list_detail::elem_type::head)) {
    ptr_ = nullptr;
  } else {
    ptr_ = this->as_type_(e);
  }
  return *this;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator--(int)
    noexcept -> const_iterator {
  const_iterator clone = *this;
  --clone;
  return clone;
}


} /* namespace ilias */

#endif /* _ILIAS_ASYNC_LL_INL_H_ */
