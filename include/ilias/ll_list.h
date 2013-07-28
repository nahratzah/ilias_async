#ifndef ILIAS_LL_LIST_H
#define ILIAS_LL_LIST_H

#include <ilias/ilias_async_export.h>
#include <ilias/refcnt.h>
#include <ilias/detail/ll_simple_list.h>
#include <array>
#include <type_traits>
#include <utility>


namespace ilias {
namespace ll_list_detail {


enum class elem_type : unsigned char {
	HEAD,
	ELEM,
	ITER_FORW,
	ITER_BACK
};


struct tag_first {};
struct tag_last {};
struct tag_head {};


class elem;
class basic_list;
class basic_iter;
using elem_ptr = refpointer<elem, ll_simple_list::elem_refcnt_mgr>;
using const_elem_ptr = refpointer<const elem, ll_simple_list::elem_refcnt_mgr>;


class elem
:	public ll_simple_list::elem
{
public:
	inline elem(elem_type) noexcept;
	inline ~elem() noexcept;

	inline elem_ptr succ() const noexcept;
	inline elem_ptr pred() const noexcept;

	template<std::size_t N> elem_ptr succ(const std::array<elem_type, N>&)
	    const noexcept;
	template<std::size_t N> elem_ptr pred(const std::array<elem_type, N>&)
	    const noexcept;

	inline bool is_head() const noexcept;
	inline bool is_elem() const noexcept;
	inline bool is_iter() const noexcept;
	inline bool is_forw_iter() const noexcept;
	inline bool is_back_iter() const noexcept;

	inline elem_type get_type() const noexcept;

private:
	elem_type type_;
};


class basic_list
{
friend class basic_iter;

public:
	using size_type = std::uintptr_t;
	using difference_type = std::intptr_t;

	inline basic_list() noexcept;
	ILIAS_ASYNC_EXPORT size_type size() const noexcept;
	ILIAS_ASYNC_EXPORT bool empty() const noexcept;

	inline bool push_front(elem* e);
	inline bool push_back(elem* e);

	ILIAS_ASYNC_EXPORT elem_ptr pop_front() noexcept;
	ILIAS_ASYNC_EXPORT elem_ptr pop_back() noexcept;

private:
	elem head_;
};


class basic_iter
{
public:
	enum class tag : int {
		first,
		last,
		head
	};

	inline basic_iter() noexcept;
	inline basic_iter(const basic_iter&) noexcept;
	inline basic_iter(basic_iter&&) noexcept;
	~basic_iter() = default;

	inline basic_iter& operator=(const basic_iter&) noexcept;
	inline basic_iter& operator=(basic_iter&&) noexcept;

	ILIAS_ASYNC_EXPORT elem_ptr next(basic_list::size_type n = 1) noexcept;
	ILIAS_ASYNC_EXPORT elem_ptr prev(basic_list::size_type n = 1) noexcept;

	inline elem_ptr link_at(basic_list*, tag);
	inline bool link_at(basic_list*, elem_ptr);
	inline bool unlink() noexcept;

	ILIAS_ASYNC_EXPORT friend bool forw_equal(const basic_iter&,
	    const basic_iter&) noexcept;
	ILIAS_ASYNC_EXPORT friend bool back_equal(const basic_iter&,
	    const basic_iter&) noexcept;

	bool operator==(const basic_iter&) const = delete;

private:
	ILIAS_ASYNC_EXPORT bool link_at_(basic_list*, elem_ptr) noexcept;

	basic_list* owner_;
	elem forw_, back_;
};


template<typename Type, typename Tag, typename AcqRel>
    class ll_list_transformations;


} /* namespace ilias::ll_list_detail */


namespace ll_list_iter_detail {


enum direction {
	forward,
	reverse
};

template<typename List, bool IsConstIter> class ll_smartptr_list_iterator;
template<typename Iter, direction Dir> class iter_direction;


} /* namespace ilias::ll_list_iter_detail */


template<typename Tag = void>
class ll_list_hook
{
template<typename FType, typename FTag, typename FAcqRel> friend
    class ll_list_detail::ll_list_transformations;
template<typename T, bool>
    friend class ll_list_iter_detail::ll_smartptr_list_iterator;

public:
	ll_list_hook() = default;
	ll_list_hook(const ll_list_hook&) = default;
	ll_list_hook(ll_list_hook&&) = default;
	~ll_list_hook() = default;
	ll_list_hook& operator=(const ll_list_hook&) = default;
	ll_list_hook& operator=(ll_list_hook&&) = default;

private:
	ll_list_detail::elem elem_{ ll_list_detail::elem_type::ELEM };
};


namespace ll_list_detail {


struct no_acqrel {};

template<typename Type, typename Tag, typename AcqRel>
class alignas(2) ll_list_transformations
{
public:
	using value_type = Type;
	using pointer = refpointer<value_type, AcqRel>;
	using const_pointer = refpointer<const value_type, AcqRel>;
	using reference = value_type&;
	using const_reference = const value_type&;

protected:
	ll_list_transformations() = default;
	ll_list_transformations(const ll_list_transformations&) = default;
	ll_list_transformations(ll_list_transformations&&) = default;

	ll_list_transformations& operator=(const ll_list_transformations&) =
	    default;
	ll_list_transformations& operator=(ll_list_transformations&&) =
	    default;

	static inline elem_ptr as_elem_(const pointer&) noexcept;
	static inline const_elem_ptr as_elem_(const const_pointer&) noexcept;
	inline pointer as_type_(const elem_ptr&) noexcept;
	inline const_pointer as_type_(const const_elem_ptr&) noexcept;
	inline void post_unlink_(const_reference, std::size_t) const noexcept;

private:
	using hook_type = ll_list_hook<Tag>;
	using hazard_t = hazard<
	    const ll_list_transformations<Type, Tag, AcqRel>,
	    const value_type>;
};

template<typename Type, typename Tag>
class alignas(2) ll_list_transformations<Type, Tag, no_acqrel>
{
public:
	using value_type = Type;
	using pointer = value_type*;
	using const_pointer = const value_type*;
	using reference = value_type&;
	using const_reference = const value_type&;

protected:
	ll_list_transformations() = default;
	ll_list_transformations(const ll_list_transformations&) = default;
	ll_list_transformations(ll_list_transformations&&) = default;

	ll_list_transformations& operator=(const ll_list_transformations&) =
	    default;
	ll_list_transformations& operator=(ll_list_transformations&&) =
	    default;

	static inline elem_ptr as_elem_(pointer) noexcept;
	static inline const_elem_ptr as_elem_(const_pointer) noexcept;
	inline pointer as_type_(const elem_ptr&) noexcept;
	inline const_pointer as_type_(const const_elem_ptr&) noexcept;
	inline void post_unlink_(const_reference, std::size_t) const noexcept;

private:
	using hook_type = ll_list_hook<Tag>;
	using hazard_t = hazard<
	    const ll_list_transformations<Type, Tag, no_acqrel>,
	    const value_type>;
};


} /* namespace ilias::ll_list_detail */


template<typename Type, typename Tag = void,
    typename AcqRel = default_refcount_mgr<Type>>
class ll_smartptr_list
:	private ll_list_detail::ll_list_transformations<Type, Tag, AcqRel>
{
friend class ll_list_iter_detail::ll_smartptr_list_iterator<
    ll_smartptr_list, true>;
friend class ll_list_iter_detail::ll_smartptr_list_iterator<
    ll_smartptr_list, false>;

private:
	using parent_t = ll_list_detail::ll_list_transformations<Type,
	    Tag, AcqRel>;

public:
	using value_type = typename parent_t::value_type;
	using pointer = typename parent_t::pointer;
	using const_pointer = typename parent_t::const_pointer;
	using reference = typename parent_t::reference;
	using const_reference = typename parent_t::const_reference;

	using size_type = ll_list_detail::basic_list::size_type;
	using difference_type = ll_list_detail::basic_list::difference_type;

	using iterator = ll_list_iter_detail::iter_direction<
	    ll_list_iter_detail::ll_smartptr_list_iterator<
	     ll_smartptr_list, false>,
	    ll_list_iter_detail::forward>;
	using const_iterator = ll_list_iter_detail::iter_direction<
	    ll_list_iter_detail::ll_smartptr_list_iterator<
	     ll_smartptr_list, true>,
	    ll_list_iter_detail::forward>;
	using reverse_iterator = ll_list_iter_detail::iter_direction<
	    ll_list_iter_detail::ll_smartptr_list_iterator<
	     ll_smartptr_list, false>,
	    ll_list_iter_detail::reverse>;
	using const_reverse_iterator = ll_list_iter_detail::iter_direction<
	    ll_list_iter_detail::ll_smartptr_list_iterator<
	     ll_smartptr_list, true>,
	    ll_list_iter_detail::reverse>;

	ll_smartptr_list() = default;
	inline ~ll_smartptr_list() noexcept;

	/* Empty/size functions. */

	inline bool empty() const noexcept;
	inline size_type size() const noexcept;

	/* Pop operations. */

	inline pointer pop_front() noexcept;
	inline pointer pop_back() noexcept;

	/* Iterators. */

	inline iterator iterator_to(reference) noexcept;
	inline const_iterator iterator_to(const_reference) const noexcept;
	inline iterator begin() noexcept;
	inline iterator end() noexcept;

	/* Insertion routines. */

	inline bool push_back(pointer p);
	inline bool push_front(pointer p);

	/* Erase routines. */

	template<typename Disposer> inline void clear_and_dispose(Disposer)
		noexcept(noexcept(std::declval<Disposer>(
		    std::declval<pointer&&>())));
	inline void clear() noexcept;

	template<typename Disposer> inline iterator erase_and_dispose(
	    const const_iterator&, Disposer)
		noexcept(noexcept(std::declval<Disposer>(
		    std::declval<pointer&&>())));
	template<typename Disposer> inline iterator erase_and_dispose(
	    const iterator&, Disposer)
		noexcept(noexcept(std::declval<Disposer>(
		    std::declval<pointer&&>())));
	inline iterator erase(const const_iterator&) noexcept;
	inline iterator erase(const iterator&) noexcept;

	/* XXX Figure out how to erase an iterator range properly. */

	/* Remove routines. */

	template<typename Predicate, typename Disposer>
	 void remove_and_dispose_if(Predicate, Disposer)
	    noexcept(
		noexcept(std::declval<Predicate>()(
		    std::declval<const_reference>())) &&
		noexcept(std::declval<Disposer>()(std::declval<pointer&&>())));
	template<typename Disposer>
	 void remove_and_dispose(const_reference, Disposer)
	    noexcept(
		noexcept(std::declval<const_reference>() ==
		    std::declval<const_reference>()) &&
		noexcept(std::declval<Disposer>()(std::declval<pointer&&>())));
	template<typename Predicate, typename Disposer>
	 void remove_if(Predicate)
	    noexcept(
		noexcept(std::declval<Predicate>()(
		    std::declval<const_reference>())));
	void remove(const_reference)
	    noexcept(
		noexcept(std::declval<const_reference>() ==
		    std::declval<const_reference>()));

private:
	inline bool unlink(const pointer&) noexcept;
	inline bool unlink(const const_pointer&) noexcept;

	using parent_t::as_elem_;
	using parent_t::as_type_;
	using parent_t::post_unlink_;

	ll_list_detail::basic_list impl_;
};

template<typename Type, typename Tag = void> using ll_list =
    ll_smartptr_list<Type, Tag, ll_list_detail::no_acqrel>;


namespace ll_list_iter_detail {


template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
class ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
    IsConstIter>
:	public std::conditional<IsConstIter,
		std::iterator<
		    std::bidirectional_iterator_tag,
		    const typename ll_smartptr_list<Type, Tag, AcqRel>::
		     value_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     difference_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     const_pointer,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     const_reference>,
		std::iterator<
		    std::bidirectional_iterator_tag,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     value_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     difference_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     pointer,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     reference>
		>::type
{
friend class ll_smartptr_list<Type, Tag, AcqRel>;

private:
	using parent_t = typename std::conditional<IsConstIter,
		std::iterator<
		    std::bidirectional_iterator_tag,
		    const typename ll_smartptr_list<Type, Tag, AcqRel>::
		     value_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     difference_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     const_pointer,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     const_reference>,
		std::iterator<
		    std::bidirectional_iterator_tag,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     value_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     difference_type,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     pointer,
		    typename ll_smartptr_list<Type, Tag, AcqRel>::
		     reference>
		>::type;
	using list_t = ll_smartptr_list<Type, Tag, AcqRel>;

public:
	ll_smartptr_list_iterator() = default;
	ll_smartptr_list_iterator(const ll_smartptr_list_iterator&) = default;
	ll_smartptr_list_iterator(ll_smartptr_list_iterator&&) = default;

	ll_smartptr_list_iterator& operator=(
	    const ll_smartptr_list_iterator&) = default;
	ll_smartptr_list_iterator& operator=(
	    ll_smartptr_list_iterator&&) = default;

	inline typename parent_t::pointer get() const
	    noexcept;
	inline typename parent_t::pointer operator->() const
	    noexcept;
	inline typename parent_t::reference operator*() const
	    noexcept;

	inline operator typename parent_t::pointer() const
	    noexcept;

	inline typename parent_t::pointer release() noexcept;

protected:
	inline void next(ll_list_detail::basic_list::size_type = 1) noexcept;
	inline void prev(ll_list_detail::basic_list::size_type = 1) noexcept;

private:
	inline bool link_at_(list_t*,
	    typename parent_t::pointer);
	inline void link_at_(list_t*,
	    typename ll_list_detail::basic_iter::tag);

	typename parent_t::pointer val_;
	ll_list_detail::basic_iter impl_;
};


template<typename Iter>
class iter_direction<Iter, forward>
:	public Iter
{
private:
	using derived_t = iter_direction;
	using parent_t = Iter;

public:
	iter_direction() = default;
	iter_direction(const iter_direction&) = default;
	iter_direction(iter_direction&&) = default;

	iter_direction(const parent_t&)
	    noexcept(std::is_nothrow_copy_constructible<parent_t>::value);
	iter_direction(parent_t&&)
	    noexcept(std::is_nothrow_move_constructible<parent_t>::value);

	iter_direction& operator=(const iter_direction&) = default;
	iter_direction& operator=(iter_direction&&) = default;

	derived_t& operator++() noexcept;
	derived_t& operator--() noexcept;
	derived_t operator++(int) const noexcept;
	derived_t operator--(int) const noexcept;

	friend derived_t next(derived_t,
	    ll_list_detail::basic_list::difference_type = 1) noexcept;
	friend derived_t prev(derived_t,
	    ll_list_detail::basic_list::difference_type = 1) noexcept;
	friend derived_t advance(derived_t,
	    ll_list_detail::basic_list::difference_type = 1) noexcept;
};

template<typename Iter>
class iter_direction<Iter, reverse>
:	public Iter
{
private:
	using derived_t = iter_direction;
	using parent_t = Iter;

public:
	iter_direction() = default;
	iter_direction(const iter_direction&) = default;
	iter_direction(iter_direction&&) = default;

	inline iter_direction(const parent_t&)
	    noexcept(std::is_nothrow_copy_constructible<parent_t>::value);
	inline iter_direction(parent_t&&)
	    noexcept(std::is_nothrow_move_constructible<parent_t>::value);

	iter_direction& operator=(const iter_direction&) = default;
	iter_direction& operator=(iter_direction&&) = default;

	derived_t& operator++() noexcept;
	derived_t& operator--() noexcept;
	derived_t operator++(int) const noexcept;
	derived_t operator--(int) const noexcept;

	friend derived_t next(derived_t,
	    ll_list_detail::basic_list::difference_type = 1) noexcept;
	friend derived_t prev(derived_t,
	    ll_list_detail::basic_list::difference_type = 1) noexcept;
	friend derived_t advance(derived_t,
	    ll_list_detail::basic_list::difference_type = 1) noexcept;
};


}} /* namespace ilias::ll_list_iter_detail */

#include <ilias/ll_list-inl.h>

#endif /* ILIAS_LL_LIST_H */
