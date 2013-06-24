#ifndef ILIAS_LL_LIST_H
#define ILIAS_LL_LIST_H

#include <ilias/ilias_async_export.h>
#include <ilias/llptr.h>
#include <ilias/refcnt.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>


namespace ilias {
namespace ll_list_detail {


enum class elem_type : unsigned char {
	HEAD,
	ELEM,
	ITER_FWD,
	ITER_BACK
};

enum class link_result : int {
	SUCCESS = 0x01,
	PRED_DELETED = 0x02,
	SUCC_DELETED = 0x04,
	ALREADY_LINKED = 0x08,
	RETRY = 0x10
};


class simple_elem;
class elem;
class iter;
class head;
class basic_iter;
using elem_refcnt = unsigned short;
using size_type = std::uintptr_t;
using difference_type = std::intptr_t;

struct simple_elem_acqrel
{
	inline void acquire(const simple_elem&, elem_refcnt = 1U) const
	    noexcept;
	ILIAS_ASYNC_EXPORT void release(const simple_elem&, elem_refcnt = 1U)
	    const noexcept;
};

using simple_elem_ptr = llptr<simple_elem, simple_elem_acqrel, 1U>;
using flags_type = typename simple_elem_ptr::flags_type;
using simple_ptr = typename simple_elem_ptr::pointer;
using simple_elem_range = std::tuple<simple_ptr, simple_ptr>;

using elem_ptr = refpointer<elem, simple_elem_acqrel>;
using iter_ptr = refpointer<iter, simple_elem_acqrel>;
using head_ptr = refpointer<head, simple_elem_acqrel>;

namespace {


constexpr flags_type DELETED{ 1U };
constexpr flags_type PRESENT{ 0U };

inline std::tuple<simple_ptr, flags_type>
add_flags(simple_ptr p, flags_type fl) noexcept
{
	return std::make_tuple(std::move(p), fl);
}
inline std::tuple<simple_elem*, flags_type>
add_flags(simple_elem* p, flags_type fl) noexcept
{
	return std::make_tuple(p, fl);
}
inline std::tuple<const simple_elem*, flags_type>
add_flags(const simple_elem* p, flags_type fl) noexcept
{
	return std::make_tuple(p, fl);
}

inline std::tuple<simple_ptr, flags_type>
add_present(simple_ptr p) noexcept
{
	return add_flags(std::move(p), PRESENT);
}
inline std::tuple<simple_elem*, flags_type>
add_present(simple_elem* p) noexcept
{
	return add_flags(std::move(p), PRESENT);
}
inline std::tuple<const simple_elem*, flags_type>
add_present(const simple_elem* p) noexcept
{
	return add_flags(std::move(p), PRESENT);
}

inline std::tuple<simple_ptr, flags_type>
add_delete(simple_ptr p) noexcept
{
	return add_flags(std::move(p), DELETED);
}
inline std::tuple<simple_elem*, flags_type>
add_delete(simple_elem* p) noexcept
{
	return add_flags(std::move(p), DELETED);
}
inline std::tuple<const simple_elem*, flags_type>
add_delete(const simple_elem* p) noexcept
{
	return add_flags(std::move(p), DELETED);
}


} /* namespace ilias::ll_list_detail::<unnamed> */


class simple_elem
{
friend struct simple_elem_acqrel;

protected:
	mutable simple_elem_ptr m_succ{ add_present(this) },
	    m_pred{ add_present(this) };
	mutable std::atomic<elem_refcnt> m_refcnt{ 0U };

private:
	elem_refcnt
	unused_refcnt(std::memory_order mo = std::memory_order_seq_cst) const
	noexcept
	{
		return (std::get<0>(this->m_succ.load_no_acquire(mo)) == this ?
		    2U : 0U);
	}

protected:
	void
	wait_unused() const noexcept
	{
		while (this->m_pred.load(std::memory_order_relaxed) !=
		    add_present(this));
		while (this->m_succ.load(std::memory_order_relaxed) !=
		    add_present(this));
		while (this->m_refcnt.load(std::memory_order_relaxed) != 2U);
		std::atomic_thread_fence(std::memory_order_acquire);
	}

public:
	ILIAS_ASYNC_EXPORT bool wait_unlinked() const noexcept;

	simple_elem() = default;
	simple_elem(const simple_elem&) = delete;
	simple_elem(simple_elem&&) = delete;
	simple_elem& operator=(const simple_elem&) = delete;

	~simple_elem() noexcept
	{
		this->wait_unused();

		/*
		 * Because our member pointers point at this,
		 * their destruction would use the dangling (destructed) llptr
		 * (m_succ, m_pred) to reset them to this.
		 *
		 * Since the release need not execute anyway, we manually
		 * release m_pred and m_succ instead.
		 */
		auto succ = std::get<0>(
		    this->m_succ.exchange(nullptr, std::memory_order_acquire));
		auto pred = std::get<0>(
		    this->m_pred.exchange(nullptr, std::memory_order_acquire));
		assert(succ == this && pred == this);

		assert(this->m_refcnt.load(std::memory_order_relaxed) == 2U);
		succ.release();
		pred.release();
	}

	bool operator==(const simple_elem&) const = delete;

private:
	class prepare_store;

public:
	ILIAS_ASYNC_EXPORT simple_elem_ptr::element_type succ() const noexcept;
	ILIAS_ASYNC_EXPORT simple_elem_ptr::element_type pred() const noexcept;
	ILIAS_ASYNC_EXPORT static link_result link(
	    simple_elem_range ins, simple_elem_range between) noexcept;
	ILIAS_ASYNC_EXPORT static link_result link_after(
	    simple_elem_range ins, simple_ptr pred) noexcept;
	ILIAS_ASYNC_EXPORT static link_result link_before(
	    simple_elem_range ins, simple_ptr succ) noexcept;
	ILIAS_ASYNC_EXPORT bool unlink() noexcept;

	static link_result
	link_after(simple_ptr ins, simple_ptr pred) noexcept
	{
		simple_elem_range r;
		std::get<0>(r) = ins;
		std::get<1>(r) = std::move(ins);
		return link_after(std::move(r), std::move(pred));
	}

	static link_result
	link_before(simple_ptr ins, simple_ptr succ) noexcept
	{
		simple_elem_range r;
		std::get<0>(r) = ins;
		std::get<1>(r) = std::move(ins);
		return link_before(std::move(r), std::move(succ));
	}

	static link_result
	link(simple_ptr ins, simple_elem_range between) noexcept
	{
		simple_elem_range r;
		std::get<0>(r) = ins;
		std::get<1>(r) = std::move(ins);
		return link(std::move(r), std::move(between));
	}

	bool
	is_linked() const noexcept
	{
		auto succ = this->m_succ.load_no_acquire(
		    std::memory_order_consume);
		return !(std::get<1>(succ) == DELETED ||
		    std::get<0>(succ) == this);
	}
};

inline void
simple_elem_acqrel::acquire(const simple_elem& e, elem_refcnt nrefs) const
noexcept
{
	if (nrefs > 0) {
		e.m_refcnt.fetch_add(nrefs,
		    std::memory_order_acquire);
	}
}


class elem
:	public simple_elem
{
friend struct simple_elem_acqrel;
friend class iter;
friend class head;

public:
	const elem_type m_type{ elem_type::ELEM };

private:
	elem(elem_type type) noexcept
	:	m_type{ type }
	{
		/* Empty body. */
	}

public:
	elem() = default;

	elem(const elem&) = delete;
	elem(elem&&) = delete;

	~elem() noexcept
	{
		this->unlink();
		this->wait_unused();
	}

	elem& operator=(const elem&) = delete;
	elem& operator=(elem&&) = delete;
	bool operator==(const elem&) const = delete;

	elem_ptr
	succ() const noexcept
	{
		return static_pointer_cast<elem>(
		    std::get<0>(this->simple_elem::succ()));
	}

	elem_ptr
	pred() const noexcept
	{
		return static_pointer_cast<elem>(
		    std::get<0>(this->simple_elem::pred()));
	}

	bool
	is_iter() const noexcept
	{
		switch (this->m_type) {
		default:
			break;
		case elem_type::ITER_FWD:
		case elem_type::ITER_BACK:
			return true;
		}
		return false;
	}

	bool
	is_back_iter() const noexcept
	{
		return (this->m_type == elem_type::ITER_BACK);
	}

	bool
	is_forw_iter() const noexcept
	{
		return (this->m_type == elem_type::ITER_FWD);
	}

	bool
	is_head() const noexcept
	{
		return (this->m_type == elem_type::HEAD);
	}

	bool
	is_elem() const noexcept
	{
		return (this->m_type == elem_type::ELEM);
	}
};

class iter
:	public elem
{
public:
	iter(elem_type type) noexcept
	:	elem{ type }
	{
		assert(type == elem_type::ITER_FWD ||
		    type == elem_type::ITER_BACK);
	}
};

class head
:	public elem
{
public:
	head() noexcept
	:	elem{ elem_type::HEAD }
	{
		/* Empty body. */
	}

	ILIAS_ASYNC_EXPORT head(head&& o) noexcept;

	~head() noexcept
	{
		this->unlink();
	}

	ILIAS_ASYNC_EXPORT bool empty() const noexcept;
	ILIAS_ASYNC_EXPORT size_type size() const noexcept;

	ILIAS_ASYNC_EXPORT elem_ptr pop_front() noexcept;
	ILIAS_ASYNC_EXPORT elem_ptr pop_back() noexcept;

private:
	ILIAS_ASYNC_EXPORT static bool link_before_(const elem_ptr&, elem*)
	    noexcept;
	ILIAS_ASYNC_EXPORT static bool link_after_(const elem_ptr&, elem*)
	    noexcept;

public:
	bool
	push_back(elem* e)
	{
		if (!e) {
			throw std::invalid_argument("ll_list: "
			    "cannot insert nil");
		}
		if (!e->is_elem()) {
			throw std::invalid_argument("ll_list: "
			    "push_back requires an element");
		}

		return link_before_(elem_ptr{ this }, e);
	}

	bool
	push_front(elem* e)
	{
		if (!e) {
			throw std::invalid_argument("ll_list: "
			    "cannot insert nil");
		}
		if (!e->is_elem()) {
			throw std::invalid_argument("ll_list: "
			    "push_front requires an element");
		}

		return link_after_(elem_ptr{ this }, e);
	}

private:
	bool
	insert_after(iter* i, elem* e)
	{
		if (!e) {
			throw std::invalid_argument("ll_list: "
			    "cannot insert nil");
		}
		if (!e->is_elem()) {
			throw std::invalid_argument("ll_list: "
			    "insert_after requires an element");
		}
		if (!i) {
			throw std::invalid_argument("ll_list: "
			    "nil iterator");
		}
		if (!i->is_iter()) {
			throw std::invalid_argument("ll_list: "
			    "insert_after requires an iterator");
		}
		if (!i->is_linked()) {
			throw std::invalid_argument("ll_list: "
			    "invalid iterator");
		}

		return link_after_(i, e);
	}

	bool
	insert_before(iter* i, elem* e)
	{
		if (!e) {
			throw std::invalid_argument("ll_list: "
			    "cannot insert nil");
		}
		if (!e->is_elem()) {
			throw std::invalid_argument("ll_list: "
			    "insert_before requires an element");
		}
		if (!i) {
			throw std::invalid_argument("ll_list: "
			    "nil iterator");
		}
		if (!i->is_iter()) {
			throw std::invalid_argument("ll_list: "
			    "insert_before requires an iterator");
		}
		if (!i->is_linked()) {
			throw std::invalid_argument("ll_list: "
			    "invalid iterator");
		}

		return link_before_(i, e);
	}

public:
	bool insert_after(const basic_iter&, elem*);
	bool insert_before(const basic_iter&, elem*);
};

class basic_iter
{
friend class head;

private:
	mutable iter m_forw{ elem_type::ITER_FWD };
	mutable iter m_back{ elem_type::ITER_BACK };

public:
	basic_iter() = default;

	ILIAS_ASYNC_EXPORT basic_iter(head& h) noexcept;
	ILIAS_ASYNC_EXPORT basic_iter(const basic_iter& i) noexcept;
	ILIAS_ASYNC_EXPORT basic_iter(basic_iter&& i) noexcept;
	ILIAS_ASYNC_EXPORT ~basic_iter() noexcept;

	ILIAS_ASYNC_EXPORT basic_iter& operator=(const basic_iter&) noexcept;
	ILIAS_ASYNC_EXPORT basic_iter& operator=(basic_iter&&) noexcept;

	bool
	is_linked() const noexcept
	{
		return this->m_forw.is_linked() && this->m_back.is_linked();
	}

	ILIAS_ASYNC_EXPORT friend difference_type distance(
	    const basic_iter&, const basic_iter&) noexcept;
};

inline bool
head::insert_after(const basic_iter& i, elem* e)
{
	return this->insert_after(&i.m_forw, e);
}

inline bool
head::insert_before(const basic_iter& i, elem* e)
{
	return this->insert_before(&i.m_back, e);
}


template<typename Base>
class reverse_iterator_tmpl
:	public Base
{
private:
	Base m_base;

	static constexpr bool
	nothrow_move_copy_destroy()
	{
		return (std::is_nothrow_move_constructible<Base>::value ||
		     !std::is_move_constructible<Base>::value) &&
		    std::is_nothrow_copy_constructible<Base>::value &&
		    std::is_nothrow_destructible<Base>::value;
	}

public:
	reverse_iterator_tmpl() = default;
	reverse_iterator_tmpl(const reverse_iterator_tmpl&) = default;
	reverse_iterator_tmpl(reverse_iterator_tmpl&&) = default;
	reverse_iterator_tmpl& operator=(const reverse_iterator_tmpl&) =
	    default;
	reverse_iterator_tmpl& operator=(reverse_iterator_tmpl&&) = default;

	reverse_iterator_tmpl(const Base& b)
	noexcept(std::is_nothrow_copy_constructible<Base>::value)
	:	m_base{ b }
	{
		/* Empty body. */
	}

	reverse_iterator_tmpl(
	    typename std::enable_if<std::is_move_assignable<Base>::value,
	      Base&&>::type b)
	noexcept(std::is_nothrow_move_constructible<Base>::value)
	:	m_base{ std::move(b) }
	{
		/* Empty body. */
	}

	template<typename U>
	reverse_iterator_tmpl(
	    const reverse_iterator_tmpl<U>& o,
	    typename std::enable_if<
	      std::is_constructible<Base, const U&>::value, int>::type = 0)
	noexcept(
		std::is_nothrow_constructible<Base, const U&>::value)
	:	m_base{ o.base() }
	{
		/* Empty body. */
	}

	const Base&
	base() const noexcept
	{
		return this->m_base;
	}

	reverse_iterator_tmpl&
	operator=(const Base& b)
	noexcept(std::is_nothrow_assignable<Base, const Base&>::value)
	{
		this->m_base = b;
		return *this;
	}

	typename std::enable_if<std::is_move_assignable<Base>::value,
	    reverse_iterator_tmpl&>::type
	operator=(Base&& b)
	noexcept(std::is_nothrow_move_assignable<Base>::value)
	{
		this->m_base = std::move(b);
		return *this;
	}

	bool
	operator==(const reverse_iterator_tmpl& i) const noexcept
	{
		return this->m_base == i.m_base;
	}

	bool
	operator==(const Base& i) const noexcept
	{
		return this->m_base == i;
	}

	friend bool
	operator==(const Base& a, const reverse_iterator_tmpl& b) noexcept
	{
		return b == a;
	}

	reverse_iterator_tmpl
	operator++(int) noexcept
	{
		return this->m_base--;
	}

	reverse_iterator_tmpl&
	operator++() noexcept
	{
		--this->m_base;
		return *this;
	}

	reverse_iterator_tmpl
	operator--(int) noexcept
	{
		return this->m_base++;
	}

	reverse_iterator_tmpl&
	operator--() noexcept
	{
		++this->m_base;
		return *this;
	}

	friend reverse_iterator_tmpl
	prev(const reverse_iterator_tmpl& i, difference_type n = 1)
	noexcept(
		nothrow_move_copy_destroy() &&
		noexcept(next(std::declval<Base>(), n)))
	{
		return next(i.m_base, n);
	}

	friend reverse_iterator_tmpl
	next(const reverse_iterator_tmpl& i, difference_type n = 1)
	noexcept(
		nothrow_move_copy_destroy() &&
		noexcept(prev(std::declval<Base>(), n)))
	{
		return prev(i.m_base, n);
	}
};


} /* namespace ilias::ll_list_detail */


template<typename Tag = void>
class ll_list_hook
:	private ll_list_detail::elem
{
template<typename FriendType, typename FriendTag> friend class ll_list;

public:
	ll_list_hook() = default;

	ll_list_hook(const ll_list_hook&) noexcept
	:	ll_list_hook{}
	{
		/* Empty body. */
	}

	ll_list_hook(ll_list_hook&&) noexcept
	:	ll_list_hook{}
	{
		/* Empty body. */
	}

	ll_list_hook&
	operator=(const ll_list_hook&) noexcept
	{
		return *this;
	}

	ll_list_hook&
	operator=(ll_list_hook&&) noexcept
	{
		return *this;
	}

	bool
	operator==(const ll_list_hook&) const noexcept
	{
		return true;
	}

	friend void
	swap(ll_list_hook&, ll_list_hook&) noexcept
	{
		return;
	}
};

template<typename Type, typename AcqRel, typename Tag = void>
class ll_smartptr_list
{
public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = refpointer<value_type, AcqRel>;
	using const_pointer = refpointer<const value_type, AcqRel>;
	using pop_result = pointer;
	using difference_type = ll_list_detail::difference_type;
	using size_type = ll_list_detail::size_type;

private:
	class iterator;
	class const_iterator;

	using reverse_iterator =
	    ll_list_detail::reverse_iterator_tmpl<iterator>;
	using const_reverse_iterator =
	    ll_list_detail::reverse_iterator_tmpl<const_iterator>;

private:
	ll_list_detail::head m_impl;

public:
	~ll_smartptr_list() noexcept
	{
		while (this->pop_front());
	}

	bool
	empty() const noexcept
	{
		return this->m_impl.empty();
	}

	size_type
	size() const noexcept
	{
		return this->m_impl.size();
	}

	iterator
	begin() noexcept
	{
		return ++iterator{ this->m_impl };
	}
	iterator
	end() noexcept
	{
		return iterator{ this->m_impl };
	}

	const_iterator
	cbegin() const noexcept
	{
		return ++const_iterator{ this->m_impl };
	}
	const_iterator
	cend() const noexcept
	{
		return const_iterator{ this->m_impl };
	}

	reverse_iterator
	rbegin() noexcept
	{
		return ++reverse_iterator{ this->m_impl };
	}
	reverse_iterator
	rend() noexcept
	{
		return reverse_iterator{ this->m_impl };
	}

	const_reverse_iterator
	crbegin() const noexcept
	{
		return ++const_reverse_iterator{ this->m_impl };
	}
	const_reverse_iterator
	crend() const noexcept
	{
		return const_reverse_iterator{ this->m_impl };
	}

	const_iterator
	begin() const noexcept
	{
		return this->cbegin();
	}
	const_iterator
	end() const noexcept
	{
		return this->cend();
	}

	const_reverse_iterator
	rbegin() const noexcept
	{
		return this->crbegin();
	}
	const_reverse_iterator
	rend() const noexcept
	{
		return this->crend();
	}

	pop_result pop_front() noexcept;
	pop_result pop_back() noexcept;
};


template<typename Type, typename AcqRel, typename Tag>
class ll_smartptr_list<Type, AcqRel, Tag>::iterator
:	public std::iterator<
	    std::bidirectional_iterator_tag,
	    value_type,
	    difference_type,
	    pointer,
	    reference>
{
friend class ll_smartptr_list::const_iterator;
friend class ll_smartptr_list<Type, AcqRel, Tag>;

private:
	ll_list_detail::basic_iter m_impl;
	ll_smartptr_list::pointer m_value;

	iterator(ll_list_detail::head& h) noexcept
	:	m_impl{ h }
	{
		/* Empty body. */
	}

public:
	iterator() = default;
	iterator(const iterator&) = default;
	iterator(iterator&&) = default;
	iterator& operator=(const iterator&) = default;
	iterator& operator=(iterator&&) = default;

	friend iterator
	prev(iterator i, difference_type n)
	noexcept
	{
		i.m_value = static_pointer_cast<value_type>(i.m_impl.succ(n));
		return i;
	}

	friend iterator
	next(iterator i, difference_type n)
	noexcept
	{
		i.m_value = static_pointer_cast<value_type>(i.m_impl.pred(n));
		return i;
	}

	iterator&
	operator++() noexcept
	{
		this->m_value = static_pointer_cast<value_type>(
		    this->m_impl.succ());
		return *this;
	}

	iterator
	operator++(int) noexcept
	{
		iterator clone = *this;
		++*this;
		return clone;
	}

	iterator&
	operator--() noexcept
	{
		this->m_value = static_pointer_cast<value_type>(
		    this->m_impl.pred());
		return *this;
	}

	iterator
	operator--(int) noexcept
	{
		iterator clone = *this;
		--*this;
		return clone;
	}

	pointer
	operator->() const noexcept
	{
		return this->m_value;
	}

	reference
	operator*() const noexcept
	{
		return *this->m_value;
	}
};

template<typename Type, typename AcqRel, typename Tag>
class ll_smartptr_list<Type, AcqRel, Tag>::const_iterator
:	public std::iterator<
	    std::bidirectional_iterator_tag,
	    value_type,
	    difference_type,
	    pointer,
	    reference>
{
friend class ll_smartptr_list<Type, AcqRel, Tag>;

private:
	ll_list_detail::basic_iter m_impl;
	ll_smartptr_list::const_pointer m_value;

	const_iterator(const ll_list_detail::head& h) noexcept
	:	m_impl{ const_cast<ll_list_detail::head&>(h) }
	{
		/* Empty body. */
	}

public:
	const_iterator() = default;
	const_iterator(const const_iterator&) = default;
	const_iterator(const_iterator&&) = default;
	const_iterator& operator=(const const_iterator&) = default;
	const_iterator& operator=(const_iterator&&) = default;

	const_iterator(const iterator& i)
	noexcept(
		std::is_nothrow_copy_constructible<
		    const_pointer>::value)
	:	m_impl{ i.m_impl },
		m_value{ i.m_value }
	{
		/* Empty body. */
	}

	const_iterator(iterator&& i)
	noexcept(
		std::is_nothrow_move_constructible<
		    const_pointer>::value)
	:	m_impl{ std::move(i.m_impl) },
		m_value{ std::move(i.m_value) }
	{
		/* Empty body. */
	}

	friend const_iterator
	prev(const_iterator i, difference_type n)
	noexcept
	{
		i.m_value = static_pointer_cast<value_type>(i.m_impl.succ(n));
		return i;
	}

	friend const_iterator
	next(const_iterator i, difference_type n)
	noexcept
	{
		i.m_value = static_pointer_cast<value_type>(i.m_impl.pred(n));
		return i;
	}

	const_iterator&
	operator++() noexcept
	{
		this->m_value = static_pointer_cast<value_type>(
		    this->m_impl.succ());
		return *this;
	}

	const_iterator
	operator++(int) noexcept
	{
		const_iterator clone = *this;
		++*this;
		return clone;
	}

	const_iterator&
	operator--() noexcept
	{
		this->m_value = static_pointer_cast<value_type>(
		    this->m_impl.pred());
		return *this;
	}

	const_iterator
	operator--(int) noexcept
	{
		const_iterator clone = *this;
		--*this;
		return clone;
	}

	pointer
	operator->() const noexcept
	{
		return this->m_value;
	}

	reference
	operator*() const noexcept
	{
		return *this->m_value;
	}
};


} /* namespace ilias */


#endif /* ILIAS_LL_LIST_H */
