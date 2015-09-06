#ifndef _ILIAS_ASYNC_LL_INL_H_
#define _ILIAS_ASYNC_LL_INL_H_

#include "ll.h"
#include <cassert>
#include <stdexcept>

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
  list::link_result rv = list::link_after_(const_cast<iter_link&>(o), *this);
  assert(rv == list::LINK_OK);  // Fail and Twice are not possible.
}

inline auto iter_link::operator=(const iter_link& o) noexcept -> iter_link& {
  while (list::unlink_(*list::pred_(*this), *this, 0) == list::UNLINK_RETRY);

  if (!list::is_unlinked_(o)) {
    list::link_result rv = list::link_after_(const_cast<iter_link&>(o), *this);
    assert(rv == list::LINK_OK);  // Fail and Twice are not possible.
  }
  return *this;
}

inline iter_link::~iter_link() noexcept {
  while (list::unlink_(*list::pred_(*this), *this, 0) == list::UNLINK_RETRY);
}


template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_elem_(const pointer& p)
    noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  hook_type& hook = *p;
  return elem_ptr(&hook);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_elem_(const const_pointer& p)
    noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  const hook_type& hook = *p;
  return elem_ptr(const_cast<hook_type*>(&hook));
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_type_(const elem_ptr& p)
    noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return pointer(&v);
}

template<typename T, typename Tag, typename AcqRel>
auto ll_list_transformations<T, Tag, AcqRel>::as_type_unlinked_(
    const elem_ptr& p) noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return pointer(v, false);
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
auto ll_list_transformations<T, Tag, no_acqrel>::as_elem_(const pointer& p)
    noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  hook_type& hook = *p;
  return elem_ptr(&hook);
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_elem_(
    const const_pointer& p) noexcept -> elem_ptr {
  if (p == nullptr) return nullptr;
  const hook_type& hook = *p;
  return elem_ptr(const_cast<hook_type*>(&hook));
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_type_(
    const elem_ptr& p) noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(get_elem_type(*p) == elem_type::element);
  hook_type& hook = static_cast<hook_type&>(*p);
  reference v = static_cast<reference>(hook);
  return &v;
}

template<typename T, typename Tag>
auto ll_list_transformations<T, Tag, no_acqrel>::as_type_unlinked_(
    const elem_ptr& p) noexcept -> pointer {
  if (p == nullptr) return nullptr;
  assert(get_elem_type(*p) == elem_type::element);
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


} /* namespace ilias::ll_detail */


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
auto ll_smartptr_list<T, Tag, AcqRel>::begin() noexcept -> iterator {
  iterator rv;
  rv.ptr_ = this->as_type_(data_.init_begin(rv.pos_));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::end() noexcept -> iterator {
  iterator rv;
  auto head = this->as_type_(data_.init_begin(rv.pos_));
  assert(ll_detail::list::get_elem_type(*head) == element_type::head);
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
  auto head = this->as_type_(data_.init_begin(rv.pos_));
  assert(ll_detail::list::get_elem_type(*head) == element_type::head);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_front(pointer p) ->
    bool {
  if (p == nullptr) throw std::invalid_argument("null element");
  bool rv = data_.link_front(this->as_elem_(p));
  if (rv) release_pointer_(move(p));
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::link_back(pointer p) ->
    bool {
  if (p == nullptr) throw std::invalid_argument("null element");
  bool rv = data_.link_back(this->as_elem_(p));
  if (rv) release_pointer_(move(p));
  return rv;
}


template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::get() const noexcept ->
    const pointer& {
  return ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator->() const noexcept ->
    const pointer& {
  assert(ptr_ != nullptr);
  return ptr_;
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
  pointer rv = move(ptr_);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::iterator::operator++()
    noexcept -> iterator& {
  auto e = pos_.step_forward();
  if (e == nullptr ||
      ll_detail::list::get_elem_type(*e) == ll_detail::elem_type::head) {
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
      ll_detail::list::get_elem_type(*e) == ll_detail::elem_type::head) {
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
ll_smartptr_list<T, Tag, AcqRel>::const_iterator::const_iterator(
    const iterator& o) noexcept
: pos_(o.pos_),
  ptr_(o.ptr_)
{}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::get() const noexcept ->
    const pointer& {
  return ptr_;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator->()
    const noexcept -> const pointer& {
  assert(ptr_ != nullptr);
  return ptr_;
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
  pointer rv = move(ptr_);
  return rv;
}

template<typename T, typename Tag, typename AcqRel>
auto ll_smartptr_list<T, Tag, AcqRel>::const_iterator::operator++()
    noexcept -> const_iterator& {
  auto e = pos_.step_forward();
  if (e == nullptr ||
      ll_detail::list::get_elem_type(*e) == ll_detail::elem_type::head) {
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
      ll_detail::list::get_elem_type(*e) == ll_detail::elem_type::head) {
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
