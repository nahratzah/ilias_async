#include <ilias/ilias_async_export.h>
#include <ilias/refcnt.h>
#include <ilias/detail/ll_simple_list.h>


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
using elem_ptr = refpointer<elem, ll_simple_list::elem_acqrel_mgr>;
using const_elem_ptr = refpointer<const elem, ll_simple_list::elem_acqrel_mgr>;


class elem
:	public ll_simple_list::elem
{
public:
	inline elem(elem_type) noexcept;
	inline ~elem() noexcept;

	inline elem_ptr succ() const noexcept;
	inline elem_ptr pred() const noexcept;

	template<unsigned int N> elem_ptr succ(std::array<elem_type, N>)
	    const noexcept;
	template<unsigned int N> elem_ptr pred(std::array<elem_type, N>)
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
public:
	using size_type = std::uintptr_t;
	using difference_type = std::intptr_t;

	inline basic_list() noexcept;
	inline size_type size() const noexcept;
	inline bool empty() const noexcept;

	push_front(elem* e)

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
template<typename Type, typename Tag, typename AcqRel> friend
    class ll_smartptr_list;

private:
	ll_list_detail::ll_simple_list::elem elem_;
};


template<typename Type, typename Tag = void,
    typename AcqRel = default_refcount_mgr<Type>>
class ll_smartptr_list
{
public:
	using value_type = Type;
	using pointer = refpointer<value_type, AcqRel>;
	using const_pointer = refpointer<const value_type, AcqRel>;
	using reference = value_type&;
	using const_reference = const value_type&;
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

	/* Iterators. */

	inline iterator iterator_to(reference);
	inline const_iterator iterator_to(const_reference) const;
	inline iterator begin() noexcept;
	inline iterator end() noexcept;

	/* Insertion routines. */

	inline bool push_back(pointer p);
	inline bool push_front(pointer p);

	/* Erase routines. */

	template<typename Disposer> inline iterator erase_and_dispose(
	    const const_iterator&, Disposer)
		noexcept(noexcept(std::declval<Disposer>(
		    std::declval<pointer&&>())));
	template<typename Disposer> inline iterator erase_and_dispose(
	    const iterator&, Disposer)
		noexcept(noexcept(std::declval<Disposer>(
		    std::declval<pointer&&>())));

	/* XXX Figure out how to erase an iterator range properly. */

private:
	using hook_type = ll_list_hook<Tag>;
	using hazard_t = hazard<const ll_list_detail::basic_list,
	    const value_type>;

	static inline ll_list_detail::elem_ptr as_elem_(
	    const pointer&) noexcept;
	static inline ll_list_detail::const_elem_ptr as_elem_(
	    const const_pointer&) noexcept;
	inline pointer as_type_(const elem_ptr& p) noexcept;
	inline const_pointer as_type_(const const_elem_ptr& p) noexcept;
	inline bool unlink(const pointer& p) noexcept;
	inline bool unlink(const const_pointer& p) noexcept;

	ll_list_detail::basic_list impl_;
};


namespace ll_list_iter_detail {


template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
class ll_smartptr_list_iterator
:	public typename std::conditional<IsConstIter,
		std::iterator<
		    std::bidirectional_iterator_tag,
		    typename const ll_smartptr_list<Type, Tag, AcqRel>::
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
		    typename const ll_smartptr_list<Type, Tag, AcqRel>::
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
	using list_t = typename ll_smartptr_list<Type, Tag, AcqRel>;

public:
	iterator() = default;
	iterator(const iterator&) = default;
	iterator(iterator&&) = default;

	iterator& operator=(const iterator&) = default;
	iterator& operator=(iterator&&) = default;

	inline typename parent_t::pointer get() const
	    noexcept;
	inline typename parent_t::pointer operator->() const
	    noexcept;
	inline typename parent_t::reference operator*() const
	    noexcept;

	inline operator typename parent_t::pointer() const
	    noexcept;

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


template<typename Derived, typename Iter>
class iter_direction<Derived, Iter, forward>
:	public Iter
{
private:
	using derived_t = iter_direction;
	using parent_t = Iter;

public:
	using parent_t::parent_t;

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

template<typename Derived, typename Iter>
class iter_direction<Iter, reverse>
:	public Iter
{
private:
	using derived_t = iter_direction;
	using parent_t = Iter;

public:
	using parent_t::parent_t;

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
