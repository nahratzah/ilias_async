/*
 * Copyright (c) 2013 Ariane van der Steldt <ariane@stack.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef ILIAS_LL_QUEUE_H
#define ILIAS_LL_QUEUE_H

#include <ilias/ilias_async_export.h>
#include <ilias/hazard.h>
#include <ilias/util.h>
#include <ilias/refcnt.h>
#include <atomic>

namespace ilias {
namespace ll_queue_detail {


/*
 * Lock-free queue implementation.
 *
 * Uses intrusively linked elem objects.
 */
class ll_qhead
{
private:
	struct tag_head {};
	struct alignas(2) token_ {};

	/*
	 * Global address token, to identify all ll_qhead hazard pointers.
	 *
	 * Using a globally shared token, the algorithm can rely on a
	 * delayed hazard-wait function, which is only called when elem
	 * is destroyed.
	 */
	ILIAS_ASYNC_EXPORT static const token_ token;

public:
	class elem
	{
	friend class ll_qhead;

	private:
		mutable std::atomic<elem*> m_succ{ nullptr };

		elem(tag_head) noexcept
		:	m_succ{ this }
		{
			/* Empty body. */
		}

		void ensure_unused() const noexcept;

	public:
		elem() = default;

		elem(const elem&) noexcept
		:	elem{}
		{
			/* Empty body. */
		}

		elem(elem&&) noexcept
		:	elem{}
		{
			/* Empty body. */
		}

		~elem() noexcept
		{
			this->ensure_unused();
		}

		elem&
		operator=(const elem&) noexcept
		{
			return *this;
		}

		elem&
		operator=(elem&&) noexcept
		{
			return *this;
		}

		bool
		operator==(const elem&) const noexcept
		{
			return true;
		}
	};

	using size_type = std::size_t;

private:
	class hazard_t
	:	public hazard<token_, elem>
	{
	public:
		hazard_t() noexcept
		:	hazard<token_, elem>{ token }
		{
			/* Empty body. */
		}
	};

	elem m_head{ tag_head{} };
	std::atomic<elem*> m_tail{ &m_head };
	std::atomic<size_type> m_size{ 0U };

public:
	ll_qhead() = default;
	ll_qhead(const ll_qhead&) = delete;

	/* XXX implement move constructor. */

private:
	ILIAS_ASYNC_EXPORT void push_back_(elem* e) noexcept;
	ILIAS_ASYNC_EXPORT elem* pop_front_() noexcept;
	ILIAS_ASYNC_EXPORT void push_front_(elem* e) noexcept;

public:
	void
	push_back(elem* e)
	{
		if (!e) {
			throw std::invalid_argument("ll_queue: "
			    "cannot push back nil");
		}

		this->push_back_(e);
	}

	elem*
	pop_front() noexcept
	{
		return this->pop_front_();
	}

	void
	push_front(elem* e)
	{
		if (!e) {
			throw std::invalid_argument("ll_queue: "
			    "cannot push back nil");
		}

		this->push_front_(e);
	}

	size_type
	size() const noexcept
	{
		return this->m_size.load(std::memory_order_consume);
	}

	bool
	empty() const noexcept
	{
		return (this->m_head.m_succ.load(std::memory_order_consume) ==
		    &this->m_head);
	}

	bool
	is_lock_free() const noexcept
	{
		hazard_t hz;
		return atomic_is_lock_free(&this->m_head.m_succ) &&
		    atomic_is_lock_free(&this->m_tail) &&
		    atomic_is_lock_free(&hz);
	}

	friend bool
	atomic_is_lock_free(const ll_qhead* h) noexcept
	{
		return h && h->is_lock_free();
	}
};

inline void
ll_qhead::elem::ensure_unused() const noexcept
{
	hazard_t::wait_unused(token, *this);
	std::atomic_thread_fence(std::memory_order_acq_rel);
}


} /* namespace ilias::ll_queue_detail */


template<typename Type, typename Tag = void> class ll_queue;
template<typename Type, typename AcqRel = default_refcount_mgr<Type>,
    typename Tag = void> class ll_smartptr_queue;


namespace {


template<typename Type, typename Tag>
bool
atomic_is_lock_free(const ll_queue<Type, Tag>* q) noexcept
{
	return q && q->is_lock_free();
}

template<typename Type, typename AcqRel, typename Tag>
bool
atomic_is_lock_free(const ll_smartptr_queue<Type, AcqRel, Tag>* q) noexcept
{
	return q && q->is_lock_free();
}


} /* namespace ilias::<unnamed> */


template<typename Tag = void>
class ll_queue_hook
:	protected ll_queue_detail::ll_qhead::elem
{
template<typename FriendType, typename FriendTag> friend class ll_queue;
};

template<typename Type, typename Tag>
class ll_queue
{
private:
	ll_queue_detail::ll_qhead m_impl;

public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = value_type*;
	using size_type = ll_queue_detail::ll_qhead::size_type;

private:
	static ll_queue_hook<Tag>*
	link_convert(pointer p) noexcept
	{
		using rv_type = ll_queue_hook<Tag>*;
		return (p ? rv_type{ p } : nullptr);
	}

	static pointer
	unlink_convert(ll_queue_hook<Tag>* p) noexcept
	{
		return (p ? &static_cast<reference>(*p) : nullptr);
	}

	static pointer
	unlink_convert(ll_queue_detail::ll_qhead::elem* e) noexcept
	{
		return (e ?
		    unlink_convert(&static_cast<ll_queue_hook<Tag>&>(*e)) :
		    nullptr);
	}

public:
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

	void
	push_back(pointer p)
	{
		this->m_impl.push_back(link_convert(p));
	}

	pointer
	pop_front() noexcept
	{
		return unlink_convert(this->m_impl.pop_front());
	}

	void
	push_front(pointer p)
	{
		this->m_impl.push_front(link_convert(p));
	}

	bool
	is_lock_free() const noexcept
	{
		return this->m_impl.is_lock_free();
	}
};

template<typename Type>
class ll_queue<Type, no_intrusive_tag>
{
public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = opt_data<value_type>;

private:
	struct elem
	:	public ll_queue_hook<>
	{
	public:
		using value_t = typename std::remove_const<Type>::type;

		value_t m_value;

		elem(const_reference v)
		noexcept(
			std::is_nothrow_copy_constructible<value_type>::
			    value)
		:	m_value(v)
		{
			/* Empty body. */
		}

		elem(rvalue_reference v)
		noexcept(
			std::is_nothrow_move_constructible<value_type>::
			    value)
		:	m_value(v)
		{
			/* Empty body. */
		}

		elem& operator=(const elem&) = delete;
	};

	using impl_type = ll_queue<elem>;

public:
	using size_type = typename impl_type::size_type;

private:
	impl_type m_impl;

public:
	~ll_queue()
	noexcept(std::is_nothrow_destructible<elem>::value)
	{
		while (this->pop_front());
	}

	pointer
	pop_front()
	noexcept(std::is_nothrow_move_constructible<value_type>::value ||
	    std::is_nothrow_copy_constructible<value_type>::value)
	{
		auto e = this->m_impl.pop_front();
		pointer rv;
		if (e)
			rv.reset(std::move_if_noexcept(e->m_value));
		return rv;
	}

	void
	push_back(const_reference e)
	{
		this->m_impl.push_back(new elem{ e });
	}

	void
	push_back(rvalue_reference e)
	{
		this->m_impl.push_back(new elem{ std::move(e) });
	}

	void
	push_front(const_reference e)
	{
		this->m_impl.push_front(new elem{ e });
	}

	void
	push_front(rvalue_reference e)
	{
		this->m_impl.push_front(new elem{ std::move(e) });
	}

	size_type
	size() const noexcept
	{
		return this->m_impl.size();
	}

	bool
	empty() const noexcept
	{
		return this->m_impl.empty();
	}

	bool
	is_lock_free() const noexcept
	{
		return this->m_impl.is_lock_free();
	}
};

template<typename Type, typename AcqRel, typename Tag>
class ll_smartptr_queue
{
private:
	using impl_type = ll_queue<Type, Tag>;

public:
	using value_type = typename impl_type::value_type;
	using reference = typename impl_type::reference;
	using const_reference = typename impl_type::const_reference;
	using rvalue_reference = typename impl_type::rvalue_reference;
	using pointer = refpointer<Type, AcqRel>;
	using size_type = typename impl_type::size_type;

private:
	impl_type m_impl;

public:
	~ll_smartptr_queue()
	noexcept(std::is_nothrow_destructible<pointer>::value)
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

	void
	push_back(pointer p)
	{
		this->m_impl.push_back(p.release());
	}

	pointer
	pop_front() noexcept
	{
		return this->m_impl.pop_front();
	}

	void
	push_front(pointer p)
	{
		this->m_impl.push_front(p.release());
	}

	bool
	is_lock_free() const noexcept
	{
		return this->m_impl.is_lock_free();
	}
};

template<typename Type, typename AcqRel>
class ll_smartptr_queue<Type, AcqRel, no_intrusive_tag>
{
private:
	using impl_type = ll_queue<refpointer<Type, AcqRel>, no_intrusive_tag>;

public:
	using value_type = Type;
	using reference = value_type&;
	using const_reference = const value_type&;
	using rvalue_reference = value_type&&;
	using pointer = refpointer<Type, AcqRel>;
	using size_type = typename impl_type::size_type;

private:
	impl_type m_impl;

public:
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

	void
	push_back(pointer p)
	{
		this->m_impl.push_back(std::move(p));
	}

	pointer
	pop_front() noexcept
	{
		auto pp = this->m_impl.pop_front();
		return (pp ? *pp : nullptr);
	}

	void
	push_front(pointer p)
	{
		this->m_impl.push_front(std::move(p));
	}

	bool
	is_lock_free() const noexcept
	{
		return this->m_impl.is_lock_free();
	}
};


} /* namespace ilias */

#endif /* ILIAS_LL_QUEUE_H */
