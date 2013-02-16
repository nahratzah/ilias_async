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
#ifndef ILIAS_MSG_QUEUE_H
#define ILIAS_MSG_QUEUE_H

#include <ilias/ilias_async_export.h>
#include <ilias/ll.h>
#include <ilias/workq.h>
#include <cassert>
#include <atomic>
#include <functional>
#include <type_traits>
#include <memory>
#include <string>
#include <utility>


namespace ilias {
namespace mq_detail {


/* Input or output side of message queue. */
enum mq_side
{
	MQ_IN = 0,
	MQ_OUT = 1
};
const unsigned int MQ_NSIDES = 2;


template<typename Type>
class mq_elem
:	public ll_base_hook<>
{
public:
	typedef Type element_type;

private:
	element_type m_val;

public:
	mq_elem() = default;
	mq_elem(const mq_elem&) = default;

	mq_elem(mq_elem&& o)
	    noexcept(
		std::is_nothrow_move_constructible<element_type>::value)
	:	m_val(o.move())
	{
		/* Empty body. */
	}

	template<typename... Args>
	mq_elem(Args&&... args)
	    noexcept(
		std::is_nothrow_constructible<element_type, Args...>::value)
	:	m_val(std::forward<Args>(args)...)
	{
		/* Empty body. */
	}

	mq_elem& operator=(const mq_elem&) = default;

	mq_elem&
	operator=(mq_elem&& o)
	    noexcept(
		std::is_nothrow_move_assignable<element_type>::value)
	{
		this->m_val = o.move();
		return *this;
	}

	mq_elem&
	operator=(const element_type& v)
	    noexcept(
		std::is_nothrow_copy_assignable<element_type>::value)
	{
		this->m_val = v;
		return *this;
	}

	mq_elem&
	operator=(element_type&& v)
	    noexcept(
		std::is_nothrow_move_assignable<element_type>::value)
	{
		this->m_val = std::move(v);
		return *this;
	}

	element_type&
	operator*() noexcept
	{
		return this->m_val;
	}

	const element_type&
	operator*() const noexcept
	{
		return this->m_val;
	}

	element_type*
	operator->() noexcept
	{
		return &this->m_val;
	}

	const element_type*
	operator->() const noexcept
	{
		return &this->m_val;
	}

	element_type&&
	move() noexcept
	{
		return std::move(this->m_val);
	}
};


/*
 * Message queue events.
 */
class msg_queue_events
{
private:
	workq_job_ptr ev_output;
	workq_job_ptr ev_empty;

protected:
	msg_queue_events() = default;

	msg_queue_events(const msg_queue_events&) = delete;
	msg_queue_events& operator=(const msg_queue_events&) = delete;
	msg_queue_events(msg_queue_events&&) = delete;

	ILIAS_ASYNC_EXPORT ~msg_queue_events() noexcept;

	ILIAS_ASYNC_EXPORT void _fire_output() noexcept;
	ILIAS_ASYNC_EXPORT void _fire_empty() noexcept;

	ILIAS_ASYNC_EXPORT void _assign_output(workq_job_ptr ptr,
	    bool fire = true) noexcept;
	ILIAS_ASYNC_EXPORT void _assign_empty(workq_job_ptr ptr,
	    bool fire = true) noexcept;
	ILIAS_ASYNC_EXPORT void _clear_events() noexcept;

	void
	_clear_output() noexcept
	{
		this->_assign_output(nullptr, false);
	}

	void
	_clear_empty() noexcept
	{
		this->_assign_output(nullptr, false);
	}
};


/* Specialized message queue for untyped messages. */
class void_msg_queue
:	private msg_queue_events
{
public:
	typedef void element_type;

private:
	std::atomic<uintptr_t> m_size;

	/*
	 * Dequeue up to max messages at once and return the number of messages
	 * thus dequeued.
	 */
	ILIAS_ASYNC_EXPORT uintptr_t _dequeue(uintptr_t max) noexcept;

public:
	void_msg_queue() noexcept
	:	m_size(0)
	{
		/* Empty body. */
	}

	void_msg_queue(const void_msg_queue&) = delete;
	void_msg_queue& operator=(const void_msg_queue&) = delete;
	void_msg_queue(void_msg_queue&&) = delete;

	~void_msg_queue() noexcept
	{
		this->_clear_events();
	}

	/* Test if the message queue is empty. */
	bool
	empty() const noexcept
	{
		return (this->m_size.load(std::memory_order_relaxed) == 0);
	}

	/* Enqueue multiple untyped messages. */
	void
	enqueue_n(size_t n) noexcept
	{
		if (this->m_size.fetch_add(n, std::memory_order_relaxed) == 0)
			this->_fire_output();
	}

	/* Enqueue an untyped message. */
	void
	enqueue() noexcept
	{
		this->enqueue_n(1);
	}

	/*
	 * Dequeue up to N messages and apply functor on each.
	 *
	 * SFINAE noexcept variant.
	 */
	template<typename Functor>
	auto
	dequeue(Functor f, size_t n = 1) noexcept ->
	    typename std::enable_if<noexcept(f()), Functor>::type
	{
		for (auto i = this->_dequeue(n); i != 0; --i)
			f();
		return f;
	}

	/*
	 * Dequeue up to N messages and apply functor on each.
	 *
	 * SFINAE variant for exception throwing functor.
	 *
	 * If the functor throws an exception, the message is still consumed.
	 */
	template<typename Functor>
	auto
	dequeue(Functor f, size_t n = 1) ->
	    typename std::enable_if<!noexcept(f()), Functor>::type
	{
		auto i = this->_dequeue(n);
		while (i > 0) {
			--i;
			try {
				f();
			} catch (...) {
				/*
				 * Recover from exception:
				 * pretend we never dequeued the elements
				 * that were not fed to the functor.
				 */
				this->enqueue_n(i);
				throw;
			}
		}
		return f;
	}
};


} /* namespace ilias::mq_detail */


template<typename Type, typename Allocator = std::allocator<Type>>
class msg_queue
:	private mq_detail::msg_queue_events
{
private:
	typedef ll_list<ll_base<mq_detail::mq_elem<Type>>> list_type;

public:
	typedef typename std::allocator_traits<Allocator>::
	    template rebind_alloc<typename list_type::value_type>
	    allocator_type;
	typedef Type element_type;

private:
	typedef std::allocator_traits<allocator_type> alloc_traits;

	allocator_type m_alloc;
	list_type m_list;

	static constexpr bool noexcept_destructible =
	    noexcept(alloc_traits::destroy(m_alloc,
	      typename alloc_traits::pointer())) &&
	    noexcept(alloc_traits::deallocate(m_alloc,
	      typename alloc_traits::pointer(),
	      typename alloc_traits::size_type()));

	/* Destructor implementation. */
	struct _destroy
	{
		allocator_type& m_alloc;
		bool m_call_destructor;

		_destroy(allocator_type& alloc, bool call_destructor) noexcept
		:	m_alloc(alloc),
			m_call_destructor(call_destructor)
		{
			/* Empty body. */
		}

		void
		operator()(typename list_type::value_type* ptr) const
		    noexcept(noexcept_destructible)
		{
			if (ptr) {
				if (this->m_call_destructor) {
					alloc_traits::destroy(this->m_alloc,
					    ptr);
				}
				alloc_traits::deallocate(this->m_alloc,
				    ptr, 1);
			}
		}
	};

	/* Declare managed pointer type. */
	typedef std::unique_ptr<typename list_type::value_type, _destroy>
	    managed_pointer;

	/* Allocate storage for managed pointer. */
	managed_pointer
	_allocate()
	{
		return managed_pointer(
		    alloc_traits::allocate(this->m_alloc, 1),
		    _destroy(this->m_alloc, false));
	}

	/* Instantiate element. */
	template<typename... Args>
	managed_pointer
	_create(managed_pointer&& ptr, Args&&... args)
	    noexcept(
		noexcept(alloc_traits::construct(m_alloc, ptr.get(),
		    std::forward<Args>(args)...)))
	{
		if (ptr) {
			assert(!ptr.get_deleter().m_call_destructor);
			alloc_traits::construct(this->m_alloc, ptr.get(),
			    std::forward<Args>(args)...);
			ptr.get_deleter().m_call_destructor = true;
		}
		return ptr;
	}

	/* Returns a managed pointer. */
	managed_pointer
	make_pointer(typename list_type::value_type* ptr) noexcept
	{
		return managed_pointer(ptr, _destroy(this->m_alloc, true));
	}

	/* Clear all messages. */
	void
	_clear()
	    noexcept(
		std::is_nothrow_destructible<
		  typename list_type::value_type>::value)
	{
		this->m_list.clear_and_dispose(_destroy(this->m_alloc, true));
		this->_fire_empty();
	}

	void
	_enqueue(managed_pointer&& ptr) noexcept
	{
		assert(ptr);
		const bool was_empty = this->m_list.empty();
		this->m_list.push_back(*ptr.release());
		if (was_empty)
			this->_fire_output();
	}

public:
	msg_queue()
	    noexcept(
		std::is_nothrow_constructible<list_type>::value &&
		std::is_nothrow_constructible<allocator_type>::value)
	:	m_alloc(),
		m_list()
	{
		/* Empty body. */
	}

	template<typename... Args>
	msg_queue(Args&&... args)
	    noexcept(
		std::is_nothrow_constructible<list_type>::value &&
		std::is_nothrow_constructible<allocator_type, Args...>::value)
	:	m_alloc(std::forward<Args>(args)...),
		m_list()
	{
		/* Empty body. */
	}

	msg_queue(const msg_queue&) = delete;
	msg_queue& operator=(const msg_queue&) = delete;

	~msg_queue() noexcept
	{
		this->_clear_events();
		this->_clear();
	}

	/* Test if the message queue is empty. */
	bool
	empty() const noexcept
	{
		return this->m_list.empty();
	}

	/* Enqueue message. */
	template<typename... Args>
	void
	enqueue(Args&&... args)
	{
		this->_enqueue(_create(_allocate(),
		    std::forward<Args>(args)...));
	}

	/*
	 * Apply functor on at most N elements in the message queue.
	 *
	 * If the functor throws an exception, the message is still consumed.
	 */
	template<typename Functor>
	Functor
	dequeue(Functor f, size_t n = 1)
	    noexcept(
		noexcept(f(*(element_type)nullptr)) &&
		noexcept_destructible)
	{
		for (size_t i = n; i != 0; --i) {
			auto elem = make_pointer(this->m_list.pop_front());
			if (!elem)
				break;
			f(elem->move());
		}
		if (this->empty())
			this->_fire_empty();
		return f;
	}
};

/*
 * Message queue specialization for untyped messages.
 *
 * The allocator is irrelevant, code between each untyped message queue
 * is shared.
 */
template<typename Allocator>
class msg_queue<void, Allocator>
:	public mq_detail::void_msg_queue
{
	msg_queue() = default;

	template<typename... Args>
	msg_queue(Args&&... args) noexcept
	{
		/* Empty body. */
	}
};


extern template class msg_queue<int>;
extern template class msg_queue<std::string>;


} /* namespace ilias */


#endif /* ILIAS_MSG_QUEUE_H */
