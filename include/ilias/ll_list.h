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
	inline basic_iter() noexcept;
	inline basic_iter(const basic_iter&) noexcept;
	inline basic_iter(basic_iter&&) noexcept;
	/* XXX maybe these need to be member functions... */
	inline basic_iter(basic_list*, tag_first) noexcept;
	inline basic_iter(basic_list*, tag_last) noexcept;
	inline basic_iter(basic_list*, tag_head) noexcept;
	~basic_iter() = default;

	ILIAS_ASYNC_EXPORT elem_ptr next() noexcept;
	ILIAS_ASYNC_EXPORT elem_ptr prev() noexcept;

	inline bool link_at(elem_ptr);

private:
	ILIAS_ASYNC_EXPORT bool link_at_(elem_ptr) noexcept;

	basic_list* owner_;
	elem forw_, back_;
};


} /* namespace ilias::ll_list_detail */


template<typename Tag = void>
class ll_list_hook
{
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
	using reference = value_type&;
	using size_type = ll_list_detail::basic_list::size_type;
	using difference_type = ll_list_detail::basic_list::difference_type;

	class iterator
	:	public std::iterator<
		    std::bidirectional_iterator_tag,
		    typename ll_smartptr_list::value_type,
		    typename ll_smartptr_list::difference_type,
		    typename ll_smartptr_list::pointer,
		    typename ll_smartptr_list::reference>
	{
	friend class ll_smartptr_list<Type, Tag, AcqRel>;

	public:
		iterator() = default;
		iterator(const iterator&) = default;
		iterator(iterator&&) = default;

		iterator& operator=(const iterator&) = default;
		iterator& operator=(iterator&&) = default;

		inline const ll_smartptr_list::pointer& get() const noexcept;
		inline const typename iterator::pointer& operator->() const
		    noexcept;
		inline typename iterator::reference operator*() const
		    noexcept;

		inline operator ll_smartptr_list::pointer() const
		    noexcept;

	private:
		bool link_at_(typename ll_smartptr_list::pointer);

		ll_smartptr_list::pointer val_;
		ll_list_detail::basic_iter impl_;
	};

	ll_smartptr_list() = default;

	/* Iterators. */

	inline iterator iterator_to(reference);
	inline iterator begin() noexcept;
	inline iterator end() noexcept;

	/* Insertion routines. */

	inline bool push_back(pointer p);
	inline bool push_front(pointer p);

private:
	static inline ll_list_detail::elem_ptr as_elem_(
	    const pointer&) noexcept;
	static inline ll_list_detail::const_elem_ptr as_elem_(
	    const const_pointer&) noexcept;

	ll_list_detail::basic_list impl_;
};


} /* namespace ilias */
