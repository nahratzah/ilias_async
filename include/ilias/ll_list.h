#ifndef ILIAS_LL_LIST_H
#define ILIAS_LL_LIST_H

#include <ilias/ilias_async_export.h>
#include <ilias/llptr.h>
#include <ilias/refcnt.h>


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
using elem_refcnt = unsigned short;

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
		return (std::get<0>(this->m_succ.load(mo)) == this ? 2U : 0U);
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

	ILIAS_ASYNC_EXPORT elem_ptr pop_front() noexcept;
	ILIAS_ASYNC_EXPORT elem_ptr pop_back() noexcept;
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

template<typename Type, typename Tag = void>
class ll_list
{
public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = value_type*;
	using pop_result = pointer;

private:
	ll_list_detail::head m_head;

public:
	ll_list() = default;
	ll_list(const ll_list&) = delete;

	ll_list(ll_list&& o) noexcept
	:	m_head{ std::move(o.m_head) }
	{
		/* Empty body. */
	}

	bool
	empty() const noexcept
	{
		return this->m_head.empty();
	}
};

template<typename Type>
class ll_list<Type, no_intrusive_tag>
{
public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = value_type*;
	using pop_result = opt_data<value_type>;

private:
	class elem
	:	public ll_list_hook<>
	{
	public:
		value_type m_value;

		elem(const_reference v)
		noexcept(std::is_nothrow_copy_constructible<value_type>::value)
		:	m_value{ v }
		{
			/* Empty body. */
		}

		elem(rvalue_reference v)
		noexcept(std::is_nothrow_move_constructible<value_type>::value)
		:	m_value{ std::move(v) }
		{
			/* Empty body. */
		}
	};

	using impl_type = ll_list<elem>;

	impl_type m_impl;

public:
	~ll_list() noexcept
	{
		while (this->pop_front());
	}

	pop_result
	pop_front() noexcept
	{
		pop_result rv;
		std::unique_ptr<elem> p = this->m_impl.pop_front();
		if (p)
			rv = p->m_value;
		return rv;
	}

	pop_result
	pop_back() noexcept
	{
		pop_result rv;
		std::unique_ptr<elem> p = this->m_impl.pop_back();
		if (p)
			rv = p->m_value;
		return rv;
	}
};

template<typename Type, typename AcqRel, typename Tag = void>
class ll_smartptr_list
{
private:
	using impl_type = ll_list<Type, Tag>;

public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = refpointer<Type, AcqRel>;
	using pop_result = pointer;

private:
	impl_type m_impl;

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

	pop_result pop_front() noexcept;
	pop_result pop_back() noexcept;
};

template<typename Type, typename AcqRel>
class ll_smartptr_list<Type, AcqRel, no_intrusive_tag>
{
private:
	using impl_type = ll_list<refpointer<Type, AcqRel>, no_intrusive_tag>;

public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = refpointer<Type, AcqRel>;
	using pop_result = pointer;

private:
	impl_type m_impl;

public:
	bool
	empty() const noexcept
	{
		return this->m_impl.empty();
	}

	pop_result
	pop_front() noexcept
	{
		auto p = this->m_impl.pop_front();
		return (p ? *p : nullptr);
	}

	pop_result
	pop_back() noexcept
	{
		auto p = this->m_impl.pop_back();
		return (p ? *p : nullptr);
	}
};


} /* namespace ilias */


#endif /* ILIAS_LL_LIST_H */
