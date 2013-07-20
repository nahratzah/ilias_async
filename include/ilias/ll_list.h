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


namespace {


/*
 * const_pointer_cast for plain old pointers.
 *
 * This really ought to be in the c++ standard,
 * it's kinda hard writing templates without...
 */
template<typename T, typename U>
T*
const_pointer_cast(U* p) noexcept
{
	return const_cast<T*>(p);
}


} /* namespace ilias::ll_list_detail::<unnamed> */


enum class elem_type : unsigned char {
	HEAD,
	ELEM,
	ITER_FWD,
	ITER_BACK
};

enum class link_result : int {
	SUCCESS,
	PRED_DELETED,
	SUCC_DELETED,
	ALREADY_LINKED,
	RETRY,
};

enum class link_ab_result : int {
	SUCCESS,
	INS_DELETED,
	ALREADY_LINKED,
};

template<typename List, typename RefPtr> class list_iterator;
template<typename Type, typename AcqRel, typename Tag> class ll_list;
template<typename Type, typename AcqRel, typename Tag> class ll_list_base;

/* Convert link_result to link_ab_result, undefined for invalid conversions. */
inline link_ab_result
ab_result(const link_result& lr) noexcept
{
	switch (lr) {
	case link_result::SUCCESS:
		return link_ab_result::SUCCESS;
	case link_result::PRED_DELETED:
	case link_result::SUCC_DELETED:
		return link_ab_result::INS_DELETED;
	case link_result::ALREADY_LINKED:
		return link_ab_result::ALREADY_LINKED;
	default:
		assert(false);
	}

	/* UNREACHABLE */
	std::terminate();
}


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
	ILIAS_ASYNC_EXPORT static void acquire(const simple_elem&,
	    elem_refcnt) noexcept;
	ILIAS_ASYNC_EXPORT static void release(const simple_elem&,
	    elem_refcnt) noexcept;
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
	mutable std::atomic<elem_refcnt> m_refcnt{ 0U };
	mutable simple_elem_ptr m_succ{ add_present(this) },
	    m_pred{ add_present(this) };

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
		auto succ =
		    this->m_succ.exchange(nullptr, std::memory_order_acquire);
		auto pred =
		    this->m_pred.exchange(nullptr, std::memory_order_acquire);
		assert(std::get<0>(succ) == this && std::get<0>(pred) == this);

		assert(this->m_refcnt.load(std::memory_order_relaxed) == 2U);
		std::get<0>(succ).release();
		std::get<0>(pred).release();
	}

	bool operator==(const simple_elem&) const = delete;

private:
	class prepare_store;

public:
	ILIAS_ASYNC_EXPORT simple_elem_ptr::element_type succ() const noexcept;
	ILIAS_ASYNC_EXPORT simple_elem_ptr::element_type pred() const noexcept;
	ILIAS_ASYNC_EXPORT static link_result link(
	    simple_elem_range ins, simple_elem_range between) noexcept;
	ILIAS_ASYNC_EXPORT static link_ab_result link_after(
	    simple_elem_range ins, simple_ptr pred) noexcept;
	ILIAS_ASYNC_EXPORT static link_ab_result link_before(
	    simple_elem_range ins, simple_ptr succ) noexcept;
	ILIAS_ASYNC_EXPORT bool unlink() noexcept;

	static link_ab_result
	link_after(simple_ptr ins, simple_ptr pred) noexcept
	{
		simple_elem_range r;
		std::get<0>(r) = ins;
		std::get<1>(r) = std::move(ins);
		return link_after(std::move(r), std::move(pred));
	}

	static link_ab_result
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

	/* For debugging purposes only. */
	elem_refcnt
	get_simple_elem_refcnt() const noexcept
	{
		return this->m_refcnt;
	}
};


class elem
:	public simple_elem
{
friend struct simple_elem_acqrel;
friend class iter;
friend class head;

public:
	const elem_type m_elemtype{ elem_type::ELEM };

private:
	elem(elem_type type) noexcept
	:	m_elemtype{ type }
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

	elem_ptr
	succ_elemhead() const noexcept
	{
		elem_ptr rv;
		for (rv = this->succ();
		    !rv->is_head() && !rv->is_elem();
		    rv = rv->succ());
		return rv;
	}

	elem_ptr
	pred_elemhead() const noexcept
	{
		elem_ptr rv;
		for (rv = this->pred();
		    !rv->is_head() && !rv->is_elem();
		    rv = rv->pred());
		return rv;
	}

	bool
	is_iter() const noexcept
	{
		switch (this->m_elemtype) {
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
		return (this->m_elemtype == elem_type::ITER_BACK);
	}

	bool
	is_forw_iter() const noexcept
	{
		return (this->m_elemtype == elem_type::ITER_FWD);
	}

	bool
	is_head() const noexcept
	{
		return (this->m_elemtype == elem_type::HEAD);
	}

	bool
	is_elem() const noexcept
	{
		return (this->m_elemtype == elem_type::ELEM);
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
	inline bool insert_after(const basic_iter&, elem*);
	inline bool insert_before(const basic_iter&, elem*);
};

class basic_iter
{
friend class head;
template<typename List, typename RefPtr> friend class list_iterator;

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
	ILIAS_ASYNC_EXPORT bool operator==(const basic_iter&) const noexcept;

	bool
	operator!=(const basic_iter& o) const noexcept
	{
		return !(*this == o);
	}

	bool
	is_linked() const noexcept
	{
		return this->m_forw.is_linked() && this->m_back.is_linked();
	}

	ILIAS_ASYNC_EXPORT friend difference_type distance(
	    const basic_iter&, const basic_iter&) noexcept;

	ILIAS_ASYNC_EXPORT elem_ptr pred(size_type n = 1) noexcept;
	ILIAS_ASYNC_EXPORT elem_ptr succ(size_type n = 1) noexcept;
	ILIAS_ASYNC_EXPORT bool link_at(elem_ptr e) noexcept;

	ILIAS_ASYNC_EXPORT static elem_ptr succ_until(elem_ptr,
	    const basic_iter&) noexcept;
	ILIAS_ASYNC_EXPORT static elem_ptr pred_until(elem_ptr,
	    const basic_iter&) noexcept;
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


struct convert_tag {};


template<typename List, typename RefPtr>
class list_iterator
:	public std::iterator<
	    std::bidirectional_iterator_tag,
	    typename std::pointer_traits<RefPtr>::element_type,
	    difference_type,
	    typename std::pointer_traits<RefPtr>::element_type*,
	    typename std::pointer_traits<RefPtr>::element_type&>
{
template<typename OL, typename ORP> friend class list_iterator;
template<typename Type, typename AR, typename Tag> friend class ll_list;

private:
	using list_type = List;

	basic_iter m_iter;
	const list_type* m_list{ nullptr };
	RefPtr m_value;

	void
	pred(size_type n = 1) noexcept
	{
		if (n == 0)
			return;

		elem_ptr e;
		RefPtr v;

		for (;;) {
			e = this->m_iter.pred(n);
			if (!e)
				break;
			else if (!e->is_head()) {
				v = this->m_list->cast(e);
				break;
			}
			n = 1;
		}

		this->m_value = std::move(v);
	}

	void
	succ(size_type n = 1) noexcept
	{
		if (n == 0)
			return;

		elem_ptr e;
		RefPtr v;

		for (;;) {
			e = this->m_iter.succ(n);
			if (!e)
				break;
			else if (!e->is_head()) {
				v = this->m_list->cast(e);
				break;
			}
			n = 1;
		}

		this->m_value = std::move(v);
	}

public:
	list_iterator() = default;
	list_iterator(const list_iterator&) = default;
	list_iterator(list_iterator&&) = default;

	list_iterator& operator=(const list_iterator&) = default;
	list_iterator& operator=(list_iterator&&) = default;

	bool
	operator==(const list_iterator& o) const noexcept
	{
		if (this->m_list == nullptr)
			return o.m_list == nullptr;

		return this->m_list == o.m_list &&
		    this->m_iter == o.m_iter;
	}

	bool
	operator!=(const list_iterator& o) const noexcept
	{
		return !(*this == o);
	}

	template<typename ORefPtr>
	list_iterator(const list_iterator<List, ORefPtr>& o, convert_tag)
	noexcept
	:	m_iter{ o.m_iter },
		m_list{ o.m_list }
	{
		using cast_type = typename list_iterator::value_type;

		this->m_value = const_pointer_cast<cast_type>(o.get());
	}

	template<typename ORefPtr>
	list_iterator(const list_iterator<List, ORefPtr>& o) noexcept
	:	m_iter{ o.m_iter },
		m_list{ o.m_list },
		m_value{ o.m_value }
	{
		/* Empty body. */
	}

	list_iterator(const list_type& list, const head& h) noexcept
	:	m_iter{ const_cast<head&>(h) },
		m_list{ &list }
	{
		/* Empty body. */
	}

	list_iterator(const list_type& list, RefPtr e)
	{
		this->link_at(list, e);
	}

	bool
	link_at(const list_type& list, RefPtr e)
	{
		bool rv;
		elem* e_ = list.as_elem(e);
		if (!e_) {
			throw std::invalid_argument("ll_list iterator: "
			    "cannot point at nil");
		}

		return do_noexcept([&]() -> bool {
			rv = this->m_iter.link_at(e_);
			this->m_list = (rv ? &list : nullptr);
			this->m_value = e;
			return rv;
		    });
	}

	list_iterator&
	operator++() noexcept
	{
		this->succ();
		return *this;
	}

	list_iterator&
	operator--() noexcept
	{
		this->pred();
		return *this;
	}

	list_iterator
	operator++(int) const noexcept
	{
		list_iterator clone = *this;
		++clone;
		return clone;
	}

	list_iterator
	operator--(int) const noexcept
	{
		list_iterator clone = *this;
		--clone;
		return clone;
	}

	typename list_iterator::reference
	operator*() const noexcept
	{
		return *this->m_value;
	}

	typename list_iterator::pointer
	operator->() const noexcept
	{
		return this->m_value.get();
	}

	const RefPtr&
	get() const noexcept
	{
		return this->m_value;
	}

	operator RefPtr() const noexcept
	{
		return this->get();
	}

	RefPtr
	release() const noexcept
	{
		return std::move(this->m_value);
	}

	friend list_iterator
	advance(list_iterator i, difference_type n) noexcept
	{
		if (n > 0)
			i.succ(n);
		if (n < 0)
			i.pred(-n);
		return i;
	}

	friend list_iterator
	next(list_iterator i, difference_type n = 1) noexcept
	{
		if (n >= 0)
			i.succ(n);
		else
			i.pred(-n);
		return i;
	}

	friend list_iterator
	prev(list_iterator i, difference_type n = 1) noexcept
	{
		if (n >= 0)
			i.pred(n);
		else
			i.succ(-n);
		return i;
	}

	/* Check if the iterator is associated with the given list. */
	friend bool
	check_validity(const list_iterator& i, const List& l) noexcept
	{
		return i.m_list == &l;
	}

	/*
	 * Iterate over elements until the iterator 'e' is reached.
	 * 'e' does not have to be properly linked for the post-condition.
	 */
	template<typename Fn>
	friend Fn
	for_each_iterator(list_iterator b, const list_iterator& e, Fn fn)
	noexcept(
		noexcept(fn(b)))
	{
		elem_ptr i = basic_iter::succ_until(
		    &b.m_iter.m_forw, e.m_iter);
		while (i) {
			auto p = b.m_list->cast(i);
			if (p) {
				b.link_at(*b.m_list, p);
				fn(b);
			}

			i = basic_iter::succ_until(i, e.m_iter);
		}
		return fn;
	}
};

template<typename List, typename RefPtr>
void
throw_validity(const list_iterator<List, RefPtr>& i, const List& l,
    bool must_have_value)
{
	if (!check_validity(i, l) &&
	    (!must_have_value || i.get() != nullptr)) {
		throw std::invalid_argument("ll_list: "
		    "invalid iterator");
	}
}


/*
 * Reverse iterator template for list types.
 */
template<typename Base>
class reverse_iterator_tmpl
:	public std::iterator<
	    typename std::iterator_traits<Base>::iterator_category,
	    typename std::iterator_traits<Base>::value_type,
	    typename std::iterator_traits<Base>::difference_type,
	    typename std::iterator_traits<Base>::pointer,
	    typename std::iterator_traits<Base>::reference>
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

private:
	struct enabler {};

public:
	template<typename... Args>
	reverse_iterator_tmpl(Args&&... args,
	    typename std::enable_if<
	      std::is_constructible<Base, Args...>::value, enabler>::type =
	      enabler{})
	noexcept(
		std::is_nothrow_constructible<Base, Args...>::value)
	:	m_base{ std::forward<Args>(args)... }
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

	auto
	release() const
	noexcept(noexcept(std::declval<Base>().release()))
	->	decltype(std::declval<Base>().release())
	{
		return this->m_base.release();
	}

	typename reverse_iterator_tmpl::reference
	operator*() const noexcept
	{
		return this->m_base.operator*();
	}

	typename reverse_iterator_tmpl::pointer
	operator->() const noexcept
	{
		return this->m_base.operator->();
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

	friend reverse_iterator_tmpl
	advance(const reverse_iterator_tmpl& i, difference_type n)
	noexcept(
		nothrow_move_copy_destroy() &&
		noexcept(advance(std::declval<Base>(), n)))
	{
		return advance(i.m_base, n);
	}
};


} /* namespace ilias::ll_list_detail */


template<typename Tag = void>
class ll_list_hook
:	private ll_list_detail::elem
{
template<typename FriendType, typename FriendAcqRel, typename FriendTag>
    friend class ll_list_detail::ll_list_base;

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

#ifndef NDEBUG
	friend ll_list_detail::elem_refcnt
	get_simple_elem_refcnt(const ll_list_hook& self) noexcept
	{
		return self.get_simple_elem_refcnt();
	}
#endif
};


namespace ll_list_detail {


template<typename Type, typename AcqRel, typename Tag>
class ll_list_base
{
template<typename List, typename RefPtr>
    friend class ll_list_detail::list_iterator;

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

protected:
	ll_list_detail::head m_impl;

private:
	using hook_type = ll_list_hook<Tag>;
	using hazard_t = hazard<ll_list_detail::head, ll_list_detail::elem>;

protected:
	/* Cast pointer to elem_ptr. */
	ll_list_detail::elem*
	as_elem(const pointer& p) const noexcept
	{
		if (!p)
			return nullptr;

		return do_noexcept([](pointer p) -> ll_list_detail::elem* {
			hook_type& hook = *p;
			return &hook;
		    }, p);
	}

	/* Cast pointer to elem_ptr. */
	ll_list_detail::elem*
	as_elem(const const_pointer& p) const noexcept
	{
		return as_elem(const_pointer_cast<value_type>(p));
	}

	/* Cast operation, to dereference an element back to the value_type. */
	pointer
	cast(const ll_list_detail::elem_ptr& hook) const noexcept
	{
		pointer rv;
		if (hook) {
			reference ref = static_cast<reference>(
			    static_cast<hook_type&>(*hook));
			hazard_t hz{ this->m_impl };

			hz.do_hazard(*hook,
			    [&]() {
				if (hook->is_linked())
					rv = pointer{ &ref };
			    },
			    [&]() {
				rv = pointer{ &ref, false };
			    });
		}
		return rv;
	}

	/* Hazard grant operation after unlinking an element. */
	void
	unlink_post(const_pointer p, unsigned int nrefs = 0)
	    const noexcept
	{
		if (p) {
			const hook_type& hook = *p.release();
							/* Cast to hook. */
			const ll_list_detail::elem& e = hook;
							/* Cast to elem. */
			AcqRel acqrel;

			hazard_t::grant([&acqrel, p](unsigned int n) {
				acqrel.acquire(*p, n);
			    },
			    [&acqrel, p](unsigned int n) {
				acqrel.release(*p, n);
			    },
			    this->m_impl, e, nrefs);
		}
	}

	static inline void
	ptr_release(pointer p) noexcept
	{
		p.release();
	}
};

template<typename Type, typename Tag>
class ll_list_base<Type, void, Tag>
{
template<typename List, typename RefPtr>
    friend class ll_list_detail::list_iterator;

public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = value_type*;
	using const_pointer = const value_type*;
	using pop_result = pointer;
	using difference_type = ll_list_detail::difference_type;
	using size_type = ll_list_detail::size_type;

protected:
	ll_list_detail::head m_impl;

private:
	using hook_type = ll_list_hook<Tag>;
	using hazard_t = hazard<ll_list_detail::head, ll_list_detail::elem>;

protected:
	/* Cast pointer to elem_ptr. */
	ll_list_detail::elem*
	as_elem(const pointer& p) const noexcept
	{
		if (!p)
			return nullptr;

		return do_noexcept([](pointer p) -> ll_list_detail::elem* {
			hook_type& hook = *p;
			return &hook;
		    }, p);
	}

	/* Cast pointer to elem_ptr. */
	ll_list_detail::elem*
	as_elem(const const_pointer& p) const noexcept
	{
		return as_elem(const_pointer_cast<value_type>(p));
	}

	/* Cast operation, to dereference an element back to the value_type. */
	pointer
	cast(const ll_list_detail::elem_ptr& hook) const noexcept
	{
		pointer rv = nullptr;
		if (hook) {
			reference ref = static_cast<reference>(
			    static_cast<hook_type&>(*hook));
			hazard_t hz{ this->m_impl };

			hz.do_hazard(*hook,
			    [&]() {
				if (hook->is_linked())
					rv = pointer{ &ref };
			    },
			    [&]() {
				assert(false);
			    });
		}
		return rv;
	}

	/* Hazard grant operation after unlinking an element. */
	void
	unlink_post(const value_type* p, unsigned int = 0) const noexcept
	{
		if (p) {
			const hook_type& hook = *p;	/* Cast to hook. */
			const ll_list_detail::elem& e = hook;
							/* Cast to elem. */
			hazard_t::wait_unused(this->m_impl, e);
		}
	}

	static inline void
	ptr_release(pointer) noexcept
	{
		return;
	}
};

template<typename Type, typename AcqRel, typename Tag>
class ll_list
:	public ll_list_base<Type, AcqRel, Tag>
{
private:
	using base = ll_list_base<Type, AcqRel, Tag>;

public:
	using iterator = ll_list_detail::list_iterator<ll_list,
	    typename base::pointer>;
	using const_iterator = ll_list_detail::list_iterator<ll_list,
	    typename base::const_pointer>;

	using reverse_iterator =
	    reverse_iterator_tmpl<iterator>;
	using const_reverse_iterator =
	    reverse_iterator_tmpl<const_iterator>;

	ll_list() = default;
	ll_list(const ll_list&) = delete;
	ll_list(ll_list&&) = default;
	ll_list& operator=(const ll_list&) = delete;
	ll_list& operator=(ll_list&&) = default;

	template<typename Iter>
	ll_list(Iter b, Iter e)
	noexcept(
		noexcept(std::declval<ll_list>().push_back(*b)) &&
		noexcept(++b))
	{
		while (b != e) {
			this->push_back(*b);
			++b;
		}
	}

	~ll_list() noexcept
	{
		while (this->pop_front());
	}

	bool
	empty() const noexcept
	{
		return this->m_impl.empty();
	}

	typename base::size_type
	size() const noexcept
	{
		return this->m_impl.size();
	}

	/*
	 * Iterators.
	 */

	iterator
	begin() noexcept
	{
		return ++iterator{ *this, this->m_impl };
	}
	iterator
	end() noexcept
	{
		return iterator{ *this, this->m_impl };
	}

	const_iterator
	cbegin() const noexcept
	{
		return ++const_iterator{ *this, this->m_impl };
	}
	const_iterator
	cend() const noexcept
	{
		return const_iterator{ *this, this->m_impl };
	}

	reverse_iterator
	rbegin() noexcept
	{
		return ++reverse_iterator{ *this, this->m_impl };
	}
	reverse_iterator
	rend() noexcept
	{
		return reverse_iterator{ *this, this->m_impl };
	}

	const_reverse_iterator
	crbegin() const noexcept
	{
		return ++const_reverse_iterator{ *this, this->m_impl };
	}
	const_reverse_iterator
	crend() const noexcept
	{
		return const_reverse_iterator{ *this, this->m_impl };
	}

	const_iterator
	begin() const noexcept
	{
		return ++const_iterator{ *this, this->m_impl };
	}
	const_iterator
	end() const noexcept
	{
		return const_iterator{ *this, this->m_impl };
	}

	const_reverse_iterator
	rbegin() const noexcept
	{
		return ++const_reverse_iterator{ *this, this->m_impl };
	}
	const_reverse_iterator
	rend() const noexcept
	{
		return const_reverse_iterator{ *this, this->m_impl };
	}

	/*
	 * Iterator creation.
	 */

	iterator
	iterator_to(const typename base::pointer& e) const
	{
		return iterator{ *this, e };
	}

	const_iterator
	iterator_to(const typename base::const_pointer& e) const
	{
		return const_iterator{ *this, e };
	}

	iterator
	iterator_to(typename base::reference e) const
	{
		return this->iterator_to(typename base::pointer{ &e });
	}

	const_iterator
	iterator_to(typename base::const_reference e) const
	{
		return this->iterator_to(typename base::const_pointer{ &e });
	}

	/*
	 * Pop operation.
	 */

	typename base::pop_result
	pop_front() noexcept
	{
		auto rv = this->cast(this->m_impl.pop_front());
		this->unlink_post(rv, 1);
		return rv;
	}

	typename base::pop_result
	pop_back() noexcept
	{
		auto rv = this->cast(this->m_impl.pop_back());
		this->unlink_post(rv, 1);
		return rv;
	}

	/*
	 * Push operation.
	 */

	bool
	push_back(typename base::pointer p)
	{
		auto rv = this->m_impl.push_back(this->as_elem(p));
		if (rv)
			base::ptr_release(p);
		return rv;
	}

	bool
	push_front(typename base::pointer p)
	{
		auto rv = this->m_impl.push_front(this->as_elem(p));
		if (rv)
			base::ptr_release(p);
		return rv;
	}

	/*
	 * Insert operation.
	 */
	iterator
	insert(const const_iterator& i, typename base::pointer p)
	{
		throw_validity(i, *this, false);
		iterator rv{ i, ll_list_detail::convert_tag{} };

		if (this->m_impl.insert_before(i.m_iter, this->as_elem(p)))
			base::ptr_release(p);
		return rv;
	}

	/*
	 * Erase operation.
	 */

	template<typename Disposer>
	iterator
	erase_and_dispose(const const_iterator& i,
	    Disposer disposer)
	{
		throw_validity(i, *this, true);
		iterator rv{ i, ll_list_detail::convert_tag{} };
		++rv;

		do_noexcept([&]() {
			if (this->as_elem(i.get())->unlink()) {
				this->unlink_post(i.get(), 1);
				disposer(*i.get());
			}
		    });

		return rv;
	}
	template<typename Disposer>
	iterator
	erase_and_dispose(const const_iterator& b,
	    const const_iterator& e,
	    Disposer disposer)
	{
		throw_validity(b, *this, true);
		throw_validity(e, *this, false);
		iterator rv{ e, ll_list_detail::convert_tag{} };

		for_each_iterator(b, e, [this](const const_iterator& i) {
			if (this->as_elem(i.get())->unlink()) {
				this->unlink_post(i.get(), 1);
				disposer(i.get());
			}
		    });
		return rv;
	}

	iterator
	erase(const_iterator i)
	{
		return this->erase_and_dispose(i,
		    [](typename base::const_reference) {
			return;
		    });
	}
	iterator
	erase(const const_iterator& b,
	    const const_iterator& e)
	{
		return this->erase_and_dispose(b, e,
		    [](typename base::const_reference) {
			return;
		    });
	}

	/*
	 * Remove operation.
	 */

	template<typename Predicate, typename Disposer>
	void
	remove_and_dispose_if(Predicate predicate, Disposer disposer)
	{
		for (auto i = this->m_impl.succ_elemhead();
		    i != &this->m_impl;
		    i = i->succ_elemhead()) {
			auto p = this->cast(i);
			if (p && predicate(*p) && i->unlink())
				disposer(*p);
		}
	}
	template<typename Predicate>
	void
	remove_if(Predicate predicate)
	{
		this->remove_and_dispose_if(std::move(predicate),
		    [](typename base::const_reference) {
			return;
		    });
	}
	template<typename Disposer>
	void
	remove_and_dispose(typename base::const_reference v, Disposer disposer)
	{
		this->remove_and_dispose_if(
		    [&v](const typename base::pointer p) {
			return v == *p;
		    },
		    std::move(disposer));
	}
	void
	remove(typename base::const_reference v)
	{
		this->remove_if([&v](const typename base::pointer p) {
			return v == *p;
		    });
	}

	/*
	 * Element iteration.
	 */

	template<typename FN>
	FN
	for_each(FN fn)
	noexcept(
		noexcept(fn(std::declval<typename base::reference>())))
	{
		for (auto i = this->m_impl.succ_elemhead();
		    i != &this->m_impl;
		    i = i->succ_elemhead()) {
			auto p = this->cast(i);
			if (p)
				fn(*p);
		}

		return fn;
	}

	template<typename FN>
	FN
	for_each_reverse(FN fn)
	noexcept(
		noexcept(fn(std::declval<typename base::reference>())))
	{
		for (auto i = this->m_impl.pred_elemhead();
		    i != &this->m_impl;
		    i = i->pred_elemhead()) {
			auto p = this->cast(i);
			if (p)
				fn(*p);
		}

		return fn;
	}

	/*
	 * Clear operation.
	 */

	template<typename Disposer>
	Disposer
	clear_and_dispose(Disposer disposer)
	noexcept(
		noexcept(disposer(std::declval<typename base::reference>())))
	{
		for (auto i = this->m_impl.pred_elemhead();
		    i != &this->m_impl;
		    i = i->pred_elemhead()) {
			if (!i->unlink())
				continue;
			auto p = this->cast(i);
			if (p)
				disposer(*p);
		}

		return disposer;
	}

	void
	clear() noexcept
	{
		this->clear_and_dispose([](typename base::reference) {
			/* Do nothing. */
		    });
	}
};


} /* namespace ll_list_detail */


template<typename Type, typename AcqRel = default_refcount_mgr<Type>,
    typename Tag = void> using ll_smartptr_list =
    ll_list_detail::ll_list<Type, AcqRel, Tag>;

template<typename Type, typename Tag = void> using ll_list =
    ll_list_detail::ll_list<Type, void, Tag>;


/* Execute functor on each element in the range. */
template<typename List, typename Ptr, typename Fn>
Fn
for_each(ll_list_detail::list_iterator<List, Ptr> b,
    ll_list_detail::list_iterator<List, Ptr> e, Fn fn)
noexcept(
	noexcept(fn(
	    std::declval<typename std::iterator_traits<
	      ll_list_detail::list_iterator<List, Ptr>>::reference>())))
{
	using iter_type = ll_list_detail::list_iterator<List, Ptr>;

	for_each_iterator(b, e, [&fn](const iter_type& i) {
		fn(*i);
	    });
	return fn;
}


} /* namespace ilias */


#endif /* ILIAS_LL_LIST_H */
