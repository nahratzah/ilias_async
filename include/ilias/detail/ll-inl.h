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


} /* namespace ilias */

#endif /* _ILIAS_ASYNC_LL_INL_H_ */
