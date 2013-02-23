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
#include <ilias/eventset.h>
#include <ilias/ll.h>
#include <cassert>
#include <algorithm>
#include <atomic>
#include <functional>
#include <type_traits>
#include <memory>
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
	enum event : unsigned int {
		MQ_EV_OUTPUT,
		MQ_EV_EMPTY
	};
	static constexpr unsigned int N_EVENTS = 2;

	eventset<N_EVENTS> ev{ event::MQ_EV_EMPTY };

protected:
	msg_queue_events() = default;

	msg_queue_events(const msg_queue_events&) = delete;
	msg_queue_events& operator=(const msg_queue_events&) = delete;
	msg_queue_events(msg_queue_events&&) = delete;

	ILIAS_ASYNC_EXPORT ~msg_queue_events() noexcept;

	void
	_fire_output() noexcept
	{
		this->ev.fire(event::MQ_EV_OUTPUT);
	}

	void
	_fire_empty() noexcept
	{
		this->ev.fire(event::MQ_EV_EMPTY);
	}

public:
	/* Set output event callback. */
	friend void
	output_callback(msg_queue_events& mqev, std::function<void()> fn)
	    noexcept
	{
		mqev.ev.assign(event::MQ_EV_OUTPUT, std::move(fn));
	}

	/* Set empty event callback. */
	friend void
	empty_callback(msg_queue_events& mqev, std::function<void()> fn)
	    noexcept
	{
		mqev.ev.assign(event::MQ_EV_EMPTY, std::move(fn));
	}

	/* Clear output event callback. */
	friend void
	output_callback(msg_queue_events& mqev, std::nullptr_t) noexcept
	{
		mqev.ev.clear(event::MQ_EV_OUTPUT);
	}

	/* Clear empty event callback. */
	friend void
	empty_callback(msg_queue_events& mqev, std::nullptr_t) noexcept
	{
		mqev.ev.clear(event::MQ_EV_EMPTY);
	}

protected:
	void
	_deactivate() noexcept
	{
		this->ev.deactivate();
	}

	void
	_deactivate_output() noexcept
	{
		this->ev.deactivate(event::MQ_EV_OUTPUT);
	}

	void
	_deactivate_empty() noexcept
	{
		this->ev.deactivate(event::MQ_EV_EMPTY);
	}

public:
	void
	clear_events() noexcept
	{
		this->ev.clear();
	}
};


/* Specialized message queue for untyped messages. */
class void_msg_queue
:	public msg_queue_events
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
		this->clear_events();
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


template<typename MQ> class prepare_enqueue;


template<typename Type, typename Allocator = std::allocator<Type>>
class msg_queue
:	public mq_detail::msg_queue_events
{
friend class prepare_enqueue<msg_queue<Type, Allocator>>;

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
		allocator_type* m_alloc;
		bool m_call_destructor;

		_destroy() noexcept
		:	m_alloc(nullptr),
			m_call_destructor(false)
		{
			/* Empty body. */
		}

		_destroy(allocator_type& alloc, bool call_destructor) noexcept
		:	m_alloc(&alloc),
			m_call_destructor(call_destructor)
		{
			/* Empty body. */
		}

		void
		operator()(typename list_type::value_type* ptr) const
		    noexcept(noexcept_destructible)
		{
			if (ptr) {
				assert(this->m_alloc);
				if (this->m_call_destructor) {
					alloc_traits::destroy(*this->m_alloc,
					    ptr);
				}
				alloc_traits::deallocate(*this->m_alloc,
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
		assert(ptr && ptr.get_deleter().m_call_destructor);
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
		this->clear_events();
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


using mq_detail::msg_queue_events;


/*
 * Prepared statement enqueue.
 *
 * Allows specifying an enqueue operation, with the guarantee that it will
 * complete succesfully at commit (nothrow specification, iff correctly used).
 *
 * commit() will throw iff commit() is called on an unprepared message queue.
 */
template<typename MQ>
class prepare_enqueue
{
public:
	typedef MQ msg_queue;
	typedef typename msg_queue::element_type element_type;

private:
	typedef typename msg_queue::managed_pointer pointer;

	msg_queue *m_mq;
	pointer m_ptr;

public:
	friend void
	swap(prepare_enqueue& p, prepare_enqueue& q) noexcept
	{
		using std::swap;

		swap(p.m_mq, q.m_mq);
		swap(p.m_ptr, q.m_ptr);
	}

	/* Reset the prepared_enqueue to the uninitialized state. */
	void
	reset() noexcept
	{
		this->m_mq = nullptr;
		this->m_ptr = nullptr;
	}

	/* Create an uninitialized prepared statement. */
	prepare_enqueue()
	:	m_mq(nullptr),
		m_ptr(nullptr)
	{
		/* Empty body. */
	}

	/*
	 * Create a prepared statement and pre-allocate storage for the
	 * element type.
	 */
	prepare_enqueue(msg_queue& mq)
	:	m_mq(&mq),
		m_ptr(mq._allocate())
	{
		/* Empty body. */
	}

	/* Cannot copy a prepared statement. */
	prepare_enqueue(const prepare_enqueue&) = delete;

	/* Move constructor. */
	prepare_enqueue(prepare_enqueue&& o) noexcept
	:	prepare_enqueue()
	{
		using std::swap;

		swap(*this, o);
	}

	/* Not copyable. */
	prepare_enqueue& operator=(const prepare_enqueue&) = delete;

	/*
	 * Assign a prepared statement to this.
	 */
	prepare_enqueue&
	operator=(prepare_enqueue&& o) noexcept
	{
		this->reset();
		swap(*this, o);
		return *this;
	}

	/*
	 * Assign a value to a prepared statement.
	 *
	 * Overwrites a previous value assigned to the prepared statement.
	 */
	template<typename... Args>
	void
	assign(Args&&... args)
	{
		if (!this->m_ptr) {
			throw std::logic_error("cannot assign "
			    "to uninitialized prepared enqueue");
		}

		if (this->m_ptr.get_deleter().m_call_destructor) {
			*this->m_ptr =
			    element_type(std::forward<Args>(args)...);
		} else {
			this->m_ptr = this->m_mq->_create(
			    std::move(this->m_ptr),
			    std::forward<Args>(args)...);
		}
	}

	/*
	 * Create a prepared statement and assign it a value for commit.
	 */
	template<typename... Args>
	prepare_enqueue(msg_queue& mq, Args&&... args)
	:	prepare_enqueue(mq)
	{
		this->assign(std::forward<Args>(args)...);
	}

	/*
	 * Commit the prepared statement.
	 *
	 * This adds the value to the message queue.
	 * This function will not throw, unless:
	 * - the prepared_enqueue is uninitialized,
	 * - the value that is to be inserted is uninitialized.
	 */
	void
	commit()
	{
		if (!this->m_ptr ||
		    !this->m_ptr.get_deleter().m_call_destructor) {
			throw std::logic_error("commit called on "
			    "unintialized prepared enqueue");
		}

		assert(this->m_mq);
		this->m_mq->_enqueue(std::move(this->m_ptr));
		this->reset();
	}
};

/*
 * Prepare_enqueue specialization for void message queues.
 *
 * Note that unlike the typed prepare_enqueue, this has no distinction
 * between allocation-only and initialized values.
 */
template<typename Allocator>
class prepare_enqueue<msg_queue<void, Allocator>>
{
public:
	typedef ilias::msg_queue<void, Allocator> msg_queue;
	typedef typename msg_queue::element_type element_type;

private:
	msg_queue *m_mq;

public:
	friend void
	swap(prepare_enqueue& p, prepare_enqueue& q) noexcept
	{
		using std::swap;

		swap(p.m_mq, q.m_mq);
	}

	void
	reset() noexcept
	{
		this->m_mq = nullptr;
	}

	constexpr prepare_enqueue()
	:	m_mq(nullptr)
	{
		/* Empty body. */
	}

	prepare_enqueue(msg_queue& mq)
	:	m_mq(&mq)
	{
		/* Empty body. */
	}

	prepare_enqueue(const prepare_enqueue&) = delete;

	prepare_enqueue(prepare_enqueue&& o) noexcept
	:	prepare_enqueue()
	{
		swap(*this, o);
	}

	prepare_enqueue& operator=(const prepare_enqueue&) = delete;

	prepare_enqueue&
	operator=(prepare_enqueue&& o) noexcept
	{
		this->reset();
		swap(*this, o);
		return *this;
	}

	void
	assign()
	{
		if (!this->m_mq) {
			throw std::logic_error("cannot assign "
			    "to uninitialized prepared enqueue");
		}
	}

	void
	commit()
	{
		if (!this->m_mq) {
			throw std::logic_error("commit called on "
			    "uninitialized prepared enqueue");
		}

		this->m_mq->enqueue();
		this->reset();
	}
};


} /* namespace ilias */


#endif /* ILIAS_MSG_QUEUE_H */
