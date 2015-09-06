#ifndef _ILIAS_ASYNC_LL_H_
#define _ILIAS_ASYNC_LL_H_

#include <atomic>
#include <cstdint>
#include <ilias/llptr.h>

namespace ilias {


struct no_acqrel {};


namespace ll_detail {


using namespace std;

class elem;
class list;
class iter_link;

enum class elem_type : uint8_t {
  head,
  element,
  iterator
};

struct elem_acqrel {
  static void acquire(const elem&, size_t nrefs) noexcept;
  static void release(const elem&, size_t nrefs) noexcept;
};

using elem_ptr = refpointer<elem, elem_acqrel>;
using elem_llptr = llptr<elem, elem_acqrel, 1>;
using elem_flags = elem_llptr::flags_type;

constexpr elem_flags MARKED = elem_flags(1);
constexpr elem_flags UNMARKED = elem_flags(0);

class elem {
  friend class elem_acqrel;
  friend class list;
  friend class iter_link;

 public:
  elem() noexcept = default;

 protected:
  elem(const elem&) noexcept : elem() {}
  elem& operator=(const elem&) noexcept { return *this; }

 private:
  elem(elem_type type) noexcept : type_(type) {}

  mutable elem_llptr succ_;
  mutable elem_llptr pred_;
  mutable atomic<size_t> link_count_;
  const elem_type type_ = elem_type::element;
  mutable atomic<bool> link_lock_;
};

class list {
  friend class iter_link;

 public:
  enum unlink_result {
    UNLINK_OK,  // unlink operation succeeded
    UNLINK_RETRY,  // unlink operation failed, because a.succ != x
    UNLINK_FAIL  // unlink operation failed, because x is not linked
  };

  enum link_result {
    LINK_OK,  // link operation succeeded
    LINK_LOST,  // sibling is not linked
    LINK_TWICE,  // x was already linked
  };

  enum axb_link_result {
    XLINK_OK,  // link operation succeeded
    XLINK_RETRY,  // a and b are no longer direct successors
    XLINK_TWICE,  // x was already linked
    XLINK_LOST_A,  // a is not linked
    XLINK_LOST_B,  // b is not linked
    XLINK_LOST_AB  // neither a nor b are linked
  };

  list() noexcept;
  list(const list&) = delete;
  list& operator=(const list&) = delete;
  ~list() noexcept;

 private:
  static bool is_unlinked_(const elem&) noexcept;
  static elem_ptr succ_(elem&) noexcept;
  static elem_ptr pred_(elem&) noexcept;
  static axb_link_result link_(elem&, elem&, elem&) noexcept;
  static unlink_result unlink_(elem&, elem&, size_t) noexcept;
  static tuple<elem_ptr, elem_flags> unlink_aid_(elem&, elem&) noexcept;
  static elem_type get_elem_type(const elem&) noexcept;

  static elem_ptr succ_elem_(elem&) noexcept;
  static elem_ptr pred_elem_(elem&) noexcept;
  static link_result link_after_(elem&, elem&) noexcept;
  static link_result link_before_(elem&, elem&) noexcept;

  static void wait_link0(elem&, size_t) noexcept;

  elem data_;
};

class iter_link final
: public elem {
 public:
  iter_link() noexcept : elem(elem_type::iterator) {}
  iter_link(const iter_link&) noexcept;
  iter_link& operator=(const iter_link&) noexcept;
  ~iter_link() noexcept;
};


template<typename T, typename Tag, typename AcqRel>
class ll_list_transformations {
 public:
  using value_type = T;
  using pointer = refpointer<value_type, AcqRel>;
  using const_pointer = refpointer<const value_type, AcqRel>;
  using reference = value_type&;
  using const_reference = const value_type&;
};

template<typename T, typename Tag>
class ll_list_transformations<T, Tag, no_acqrel> {
 public:
  using value_type = T;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;
};


} /* namespace ilias::ll_detail */


template<typename Tag = void>
class ll_list_hook
: private ll_detail::elem
{
  template<typename, typename, typename>
      friend class ll_detail::ll_list_transformations;
};

template<typename T, typename Tag = void,
         typename AcqRel = default_refcount_mgr<T>>
class ll_list
: private ll_detail::ll_list_transformations<T, Tag, AcqRel>
{
 private:
  using transformations_type =
      ll_detail::ll_list_transformations<T, Tag, AcqRel>;

 public:
  using value_type = T;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  class iterator;
  class const_iterator;

  ll_list() noexcept = default;
  ll_list(const ll_list&) = delete;
  ll_list& operator=(const ll_list&) = delete;
  ~ll_list() noexcept;

  bool empty() const noexcept;
  size_type size() const noexcept;

  pointer pop_front() noexcept;
  pointer pop_back() noexcept;

  iterator iterator_to(reference) noexcept;
  const_iterator iterator_to(const_reference) noexcept;
  iterator iterator_to(const pointer&);
  const_iterator iterator_to(const const_pointer&);

  iterator begin() noexcept;
  iterator end() noexcept;
  const_iterator begin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator cend() const noexcept;

  inline bool link_back(pointer);
  inline bool link_front(pointer);
  inline std::pair<iterator, bool> link_after(const const_iterator&, pointer);
  inline std::pair<iterator, bool> link_after(const iterator&, pointer);
  inline std::pair<iterator, bool> link_before(const const_iterator&, pointer);
  inline std::pair<iterator, bool> link_before(const iterator&, pointer);

  inline iterator link(const const_iterator&, pointer);
  inline iterator link(const iterator&, pointer);

  template<typename Disposer> void clear_and_dispose(Disposer)
      noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  void clear() noexcept;
  template<typename Disposer> iterator erase_and_dispose(const const_iterator&,
                                                         Disposer)
      noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  template<typename Disposer> iterator erase_and_dispose(const iterator&,
                                                         Disposer)
      noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  iterator erase(const const_iterator&) noexcept;
  iterator erase(const iterator&) noexcept;

  template<typename Disposer> iterator erase_and_dispose(const const_iterator&,
                                                         const const_iterator&,
                                                         Disposer)
      noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  template<typename Disposer> iterator erase_and_dispose(const iterator&,
                                                         const iterator&,
                                                         Disposer)
      noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  iterator erase(const const_iterator&, const const_iterator&) noexcept;
  iterator erase(const iterator&, const iterator&) noexcept;

  template<typename Predicate, typename Disposer>
  void remove_and_dispose_if(Predicate, Disposer)
      noexcept(noexcept(std::declval<Predicate&>()(
                            std::declval<const_reference>())) &&
               noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  template<typename Disposer>
  void remove_and_dispose(const_reference, Disposer)
      noexcept(noexcept(std::declval<const_reference>() ==
                            std::declval<const_reference>()) &&
               noexcept(std::declval<Disposer&>()(std::declval<pointer>())));
  template<typename Predicate>
  void remove_if(Predicate)
      noexcept(noexcept(std::declval<Predicate&>()(
                            std::declval<const_reference>())));
  template<typename Predicate>
  void remove(const_reference)
      noexcept(noexcept(std::declval<const_reference>() ==
                            std::declval<const_reference>()));

  template<typename Functor>
  Functor visit(Functor)
      noexcept(noexcept(std::declval<Functor&>()(std::declval<reference>())));
  template<typename Functor>
  Functor visit(Functor) const
      noexcept(noexcept(std::declval<Functor&>()(
                            std::declval<const_reference>())));

 private:
  mutable ll_detail::list data_;
};


} /* namespace ilias */

#include "ll-inl.h"

#endif /* _ILIAS_ASYNC_LL_H_ */
