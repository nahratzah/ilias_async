#ifndef _ILIAS_ASYNC_LL_H_
#define _ILIAS_ASYNC_LL_H_

#include <ilias/ilias_async_export.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <utility>
#include <ilias/llptr.h>

namespace ilias {


struct no_acqrel {};
template<typename = void> class ll_list_hook;


namespace ll_list_detail {


using namespace std;

class elem;
class list;
class iter_link;
class elem_linking_lock;

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

ILIAS_ASYNC_EXPORT constexpr elem_flags MARKED = elem_flags(1);
ILIAS_ASYNC_EXPORT constexpr elem_flags UNMARKED = elem_flags(0);

class elem {
  friend class elem_linking_lock;
  friend struct elem_acqrel;
  friend class list;
  friend class iter_link;

 public:
  ILIAS_ASYNC_EXPORT elem() noexcept;

 protected:
  elem(const elem&) noexcept : elem() {}
  elem& operator=(const elem&) noexcept { return *this; }
  ILIAS_ASYNC_EXPORT ~elem() noexcept;

 private:
  ILIAS_ASYNC_EXPORT elem(elem_type) noexcept;

  mutable elem_llptr succ_;
  mutable elem_llptr pred_;
  mutable atomic<size_t> link_count_{ 0U };
  const elem_type type_ = elem_type::element;
  mutable atomic<bool> linking_{ false };
};

class list {
  friend class iter_link;

 public:
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  class position;

  enum class unlink_result : unsigned char {
    UNLINK_OK,  // unlink operation succeeded
    UNLINK_RETRY,  // unlink operation failed, because a.succ != x
    UNLINK_FAIL  // unlink operation failed, because x is not linked
  };
  ILIAS_ASYNC_EXPORT static constexpr unlink_result UNLINK_OK =
      unlink_result::UNLINK_OK;
  ILIAS_ASYNC_EXPORT static constexpr unlink_result UNLINK_RETRY =
      unlink_result::UNLINK_RETRY;
  ILIAS_ASYNC_EXPORT static constexpr unlink_result UNLINK_FAIL =
      unlink_result::UNLINK_FAIL;

  enum class link_result : unsigned char {
    LINK_OK,  // link operation succeeded
    LINK_LOST,  // sibling is not linked
    LINK_TWICE,  // x was already linked
  };
  ILIAS_ASYNC_EXPORT static constexpr link_result LINK_OK =
      link_result::LINK_OK;
  ILIAS_ASYNC_EXPORT static constexpr link_result LINK_LOST =
      link_result::LINK_LOST;
  ILIAS_ASYNC_EXPORT static constexpr link_result LINK_TWICE =
      link_result::LINK_TWICE;

  enum class axb_link_result : unsigned char {
    XLINK_OK,  // link operation succeeded
    XLINK_RETRY,  // a and b are no longer direct successors
    XLINK_TWICE,  // x was already linked
    XLINK_LOST_A,  // a is not linked
    XLINK_LOST_B,  // b is not linked
    XLINK_LOST_AB  // neither a nor b are linked
  };
  ILIAS_ASYNC_EXPORT static constexpr axb_link_result XLINK_OK =
      axb_link_result::XLINK_OK;
  ILIAS_ASYNC_EXPORT static constexpr axb_link_result XLINK_RETRY =
      axb_link_result::XLINK_RETRY;
  ILIAS_ASYNC_EXPORT static constexpr axb_link_result XLINK_TWICE =
      axb_link_result::XLINK_TWICE;
  ILIAS_ASYNC_EXPORT static constexpr axb_link_result XLINK_LOST_A =
      axb_link_result::XLINK_LOST_A;
  ILIAS_ASYNC_EXPORT static constexpr axb_link_result XLINK_LOST_B =
      axb_link_result::XLINK_LOST_B;
  ILIAS_ASYNC_EXPORT static constexpr axb_link_result XLINK_LOST_AB =
      axb_link_result::XLINK_LOST_AB;

  ILIAS_ASYNC_EXPORT list() noexcept;
  list(const list&) = delete;
  list& operator=(const list&) = delete;
  ILIAS_ASYNC_EXPORT ~list() noexcept;

  ILIAS_ASYNC_EXPORT bool empty() const noexcept;
  ILIAS_ASYNC_EXPORT size_type size() const noexcept;

  ILIAS_ASYNC_EXPORT elem_ptr init_begin(position&) const noexcept;
  ILIAS_ASYNC_EXPORT elem_ptr init_end(position&) const noexcept;

 private:
  static bool is_unlinked_(const elem&) noexcept;
  ILIAS_ASYNC_EXPORT static elem_ptr succ_(elem&) noexcept;
  ILIAS_ASYNC_EXPORT static elem_ptr pred_(elem&) noexcept;
  ILIAS_ASYNC_EXPORT static axb_link_result link_(elem&, elem&, elem&)
      noexcept;
  ILIAS_ASYNC_EXPORT static unlink_result unlink_(elem&, elem&, size_t)
      noexcept;
  ILIAS_ASYNC_EXPORT static tuple<elem_ptr, elem_flags> unlink_aid_(elem&,
                                                                    elem&)
      noexcept;

 public:
  static elem_type get_elem_type(const elem&) noexcept;

 private:
  ILIAS_ASYNC_EXPORT static elem_ptr succ_elem_(elem&) noexcept;
  ILIAS_ASYNC_EXPORT static elem_ptr pred_elem_(elem&) noexcept;
  ILIAS_ASYNC_EXPORT static link_result link_after_(elem&, elem&) noexcept;
  ILIAS_ASYNC_EXPORT static link_result link_before_(elem&, elem&) noexcept;

  ILIAS_ASYNC_EXPORT static void wait_link0(elem&, size_t) noexcept;

 public:
  ILIAS_ASYNC_EXPORT elem_ptr pop_front() noexcept;
  ILIAS_ASYNC_EXPORT elem_ptr pop_back() noexcept;

  ILIAS_ASYNC_EXPORT bool link_front(elem&) noexcept;
  ILIAS_ASYNC_EXPORT bool link_back(elem&) noexcept;
  ILIAS_ASYNC_EXPORT tuple<elem_ptr, bool> link_after(const position&, elem&,
                                                      position*);
  ILIAS_ASYNC_EXPORT tuple<elem_ptr, bool> link_before(const position&, elem&,
                                                       position*);
  ILIAS_ASYNC_EXPORT tuple<elem_ptr, bool> unlink(elem&, position*, size_t);

  ILIAS_ASYNC_EXPORT static bool iterator_to(elem&, position*) noexcept;

 private:
  elem data_;
};

class iter_link final
: public elem
{
 public:
  iter_link() noexcept : elem(elem_type::iterator) {}
  iter_link(const iter_link&) noexcept;
  iter_link& operator=(const iter_link&) noexcept;
  ~iter_link() noexcept;
};

class list::position {
  friend list;

 public:
  ILIAS_ASYNC_EXPORT position() noexcept;
  ILIAS_ASYNC_EXPORT position(const position&) noexcept;
  ILIAS_ASYNC_EXPORT position& operator=(const position&) noexcept;
  ILIAS_ASYNC_EXPORT ~position() noexcept;

  ILIAS_ASYNC_EXPORT void unlink() noexcept;
  ILIAS_ASYNC_EXPORT bool link_around(elem&) noexcept;
  ILIAS_ASYNC_EXPORT elem_ptr step_forward() noexcept;
  ILIAS_ASYNC_EXPORT elem_ptr step_backward() noexcept;
  ILIAS_ASYNC_EXPORT bool operator==(const position&) const noexcept;

 private:
  ILIAS_ASYNC_EXPORT static bool unlink_iter_(iter_link&) noexcept;
  iter_link& front_() const noexcept { return pos_[swap_]; }
  iter_link& back_() const noexcept { return pos_[swap_ ^ 1U]; }

  mutable array<iter_link, 2> pos_;
  unsigned int swap_ : 1;
};


template<typename T, typename Tag, typename AcqRel>
class ll_list_transformations {
 public:
  using value_type = T;
  using pointer = refpointer<value_type, AcqRel>;
  using const_pointer = refpointer<const value_type, AcqRel>;
  using reference = value_type&;
  using const_reference = const value_type&;
  using difference_type = list::difference_type;

 private:
  using hook_type = ll_list_hook<Tag>;

 protected:
  static elem_ptr as_elem_(reference) noexcept;
  static elem_ptr as_elem_(const_reference) noexcept;
  static elem_ptr as_elem_(const pointer&) noexcept;
  static elem_ptr as_elem_(const const_pointer&) noexcept;
  static pointer as_type_(const elem_ptr&) noexcept;
  static pointer as_type_unlinked_(const elem_ptr&) noexcept;
  static void release_pointer_(pointer&&) noexcept;
  static void release_pointer_(const_pointer&&) noexcept;
};

template<typename T, typename Tag>
class ll_list_transformations<T, Tag, no_acqrel> {
 public:
  using value_type = T;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;
  using difference_type = list::difference_type;

 private:
  using hook_type = ll_list_hook<Tag>;

 protected:
  static elem_ptr as_elem_(reference) noexcept;
  static elem_ptr as_elem_(const_reference) noexcept;
  static elem_ptr as_elem_(const pointer&) noexcept;
  static elem_ptr as_elem_(const const_pointer&) noexcept;
  static pointer as_type_(const elem_ptr&) noexcept;
  static pointer as_type_unlinked_(const elem_ptr&) noexcept;
  static void release_pointer_(pointer&&) noexcept;
  static void release_pointer_(const_pointer&&) noexcept;
};


} /* namespace ilias::ll_list_detail */


template<typename Tag>
class ll_list_hook
: private ll_list_detail::elem
{
  template<typename, typename, typename>
      friend class ll_list_detail::ll_list_transformations;

 protected:
  ll_list_hook& operator=(const ll_list_hook&) noexcept { return *this; }
};

template<typename T, typename Tag = void,
         typename AcqRel = default_refcount_mgr<T>>
class ll_smartptr_list
: private ll_list_detail::ll_list_transformations<T, Tag, AcqRel>
{
 private:
  using transformations_type =
      ll_list_detail::ll_list_transformations<T, Tag, AcqRel>;

 public:
  using value_type = typename transformations_type::value_type;
  using pointer = typename transformations_type::pointer;
  using const_pointer = typename transformations_type::const_pointer;
  using reference = typename transformations_type::reference;
  using const_reference = typename transformations_type::const_reference;
  using size_type = ll_list_detail::list::size_type;
  using difference_type = ll_list_detail::list::difference_type;
  class iterator;
  class const_iterator;

  ll_smartptr_list() noexcept = default;
  ll_smartptr_list(const ll_smartptr_list&) = delete;
  ll_smartptr_list& operator=(const ll_smartptr_list&) = delete;
  ~ll_smartptr_list() noexcept;

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
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return begin(); }

  bool link_front(pointer);
  bool link_back(pointer);
  std::pair<iterator, bool> link_after(const const_iterator&, pointer);
  std::pair<iterator, bool> link_after(const iterator&, pointer);
  std::pair<iterator, bool> link_before(const const_iterator&, pointer);
  std::pair<iterator, bool> link_before(const iterator&, pointer);

  iterator link(const const_iterator&, pointer);
  iterator link(const iterator&, pointer);

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
  mutable ll_list_detail::list data_;
};


template<typename T, typename Tag, typename AcqRel>
class ll_smartptr_list<T, Tag, AcqRel>::iterator
: private ll_list_detail::ll_list_transformations<T, Tag, AcqRel>,
  public std::iterator<
      std::bidirectional_iterator_tag,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::value_type,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::difference_type,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::pointer,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::reference>
{
  friend ll_smartptr_list;

 private:
  using transformations_type =
      ll_list_detail::ll_list_transformations<T, Tag, AcqRel>;

 public:
  using pointer = typename transformations_type::pointer;
  using reference = typename transformations_type::reference;

  iterator() noexcept = default;
  iterator(const iterator&) noexcept = default;
  iterator& operator=(const iterator&) noexcept = default;
  bool operator==(const iterator&) const noexcept;
  bool operator!=(const iterator&) const noexcept;

  pointer get() const noexcept;
  value_type* operator->() const noexcept;
  reference operator*() const noexcept;
  operator pointer() const noexcept;
  pointer release() noexcept;

  iterator& operator++() noexcept;
  iterator operator++(int) noexcept;
  iterator& operator--() noexcept;
  iterator operator--(int) noexcept;

 private:
  ll_list_detail::list::position pos_;
  typename transformations_type::pointer ptr_;
};


template<typename T, typename Tag, typename AcqRel>
class ll_smartptr_list<T, Tag, AcqRel>::const_iterator
: private ll_list_detail::ll_list_transformations<T, Tag, AcqRel>,
  public std::iterator<
      std::bidirectional_iterator_tag,
      const typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::value_type,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::difference_type,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::const_pointer,
      typename ll_list_detail::
          template ll_list_transformations<T, Tag, AcqRel>::const_reference>
{
  friend ll_smartptr_list;

 private:
  using transformations_type =
      ll_list_detail::ll_list_transformations<T, Tag, AcqRel>;

 public:
  using pointer = typename transformations_type::const_pointer;
  using reference = typename transformations_type::const_reference;

  const_iterator() noexcept = default;
  const_iterator(const iterator&) noexcept;
  const_iterator(const const_iterator&) noexcept = default;
  const_iterator& operator=(const const_iterator&) noexcept = default;
  bool operator==(const const_iterator&) const noexcept;
  bool operator!=(const const_iterator&) const noexcept;

  pointer get() const noexcept;
  value_type* operator->() const noexcept;
  reference operator*() const noexcept;
  operator pointer() const noexcept;
  pointer release() noexcept;

  const_iterator& operator++() noexcept;
  const_iterator operator++(int) noexcept;
  const_iterator& operator--() noexcept;
  const_iterator operator--(int) noexcept;

 private:
  ll_list_detail::list::position pos_;
  typename transformations_type::pointer ptr_;
};


template<typename T, typename Tag = void> using ll_list =
    ll_smartptr_list<T, Tag, no_acqrel>;


} /* namespace ilias */

#include "ll_list-inl.h"

#endif /* _ILIAS_ASYNC_LL_H_ */
