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
#include <cassert>
#include <algorithm>
#include <atomic>
#include <functional>
#include <type_traits>
#include <memory>
#include <mutex>
#include <utility>


namespace ilias {
namespace mq_detail {


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
template<typename MqType>
class msg_queue_events
{
private:
	enum class state : int {
		IDLE,
		BUSY,
		AGAIN
	};

	std::atomic<state> m_state{ state::IDLE };

	std::mutex m_evmtx;
	bool m_ev_restore{ false };		/* Protected by m_evmtx. */
	std::function<void (MqType&)> m_ev;	/* Protected by m_evmtx. */

	/*
	 * Inner function: fires m_ev.
	 * Not safe against concurrent invocations.
	 */
	void
	_fire_event(MqType& ev_arg) noexcept
	{
		std::function<void (MqType&)> tmp;
		std::unique_lock<std::mutex> guard{ this->m_evmtx };

		swap(tmp, this->m_ev);
		if (tmp) {
			this->m_ev_restore = true;
			guard.unlock();

			/*
			 * Invoke function (which is temporarily copied to tmp,
			 * to prevent it from being deleted from within its
			 * invocation.
			 */
			tmp(ev_arg);

			guard.lock();
			if (this->m_ev_restore) {
				swap(this->m_ev, tmp);
				this->m_ev_restore = false;
			} else {
				/* Destroy event outside of event lock. */
				guard.unlock();
				tmp = nullptr;
				guard.lock();
			}
		}
	}

protected:
	msg_queue_events() = default;

	msg_queue_events(const msg_queue_events&) = delete;
	msg_queue_events& operator=(const msg_queue_events&) = delete;

	/* Move constructor. */
	msg_queue_events(msg_queue_events&& mqe) noexcept
	:	m_ev(std::move(mqe.m_ev))
	{
		assert(mqe.m_state == state::IDLE);
	}

	~msg_queue_events() noexcept
	{
		assert(this->m_state == state::IDLE);
	}

	/*
	 * Fire message queue event.
	 *
	 * If the event is already running, make sure it will run again
	 * once it completes (to prevent missed wakeups).
	 */
	void
	_fire() noexcept
	{
		static_assert(std::is_base_of<msg_queue_events, MqType>::value,
		    "msg_queue_events<MQ> must be a base of MQ");

		/* Lacking using statement to import class members... */
		constexpr state IDLE = state::IDLE;
		constexpr state BUSY =  state::BUSY;
		constexpr state AGAIN = state::AGAIN;

		/* Argument on event: derived type of event. */
		MqType& ev_arg = static_cast<MqType&>(*this);

		/*
		 * We always transition to AGAIN.
		 * If it turns out we transitioned from the IDLE state,
		 * we immediately transition to BUSY and
		 * start executing the event.
		 */
		state s = this->m_state.exchange(AGAIN,
		    std::memory_order_acq_rel);
		if (s == IDLE) {
			do {
				/*
				 * Force to BUSY: we are going to execute.
				 * Multiple invocations up to now will result
				 * in a single event.
				 */
				this->m_state.store(BUSY,
				    std::memory_order_release);
				std::atomic_thread_fence(
				    std::memory_order_acquire);

				this->_fire_event(ev_arg);

				/*
				 * Transition from BUSY to IDLE.
				 * Fails if set to AGAIN, in which case we need
				 * to execute the event again.
				 */
				s = BUSY;
			} while (!this->m_state.compare_exchange_strong(s,
			    IDLE,
			    std::memory_order_release,
			    std::memory_order_relaxed));
		}
	}

public:
	/* Set output event callback. */
	friend void
	callback(msg_queue_events& mqev, std::function<void (MqType&)> fn)
	    noexcept
	{
		static_assert(std::is_base_of<msg_queue_events, MqType>::value,
		    "msg_queue_events<MQ> must be a base of MQ");

		std::unique_lock<std::mutex> guard{ mqev.m_evmtx };
		swap(mqev.m_ev, fn);

		if (mqev.m_ev_restore) {
			/* Make new event fire after old event completes. */
			mqev.m_ev_restore = false;
			mqev.m_state.store(state::AGAIN,
			    std::memory_order_release);
		} else {
			/* This message queue is not empty, fire event. */
			guard.unlock();
			if (!static_cast<MqType&>(mqev).empty())
				mqev._fire();
		}
	}

	/* Clear output event callback. */
	friend void
	callback(msg_queue_events& mqev, std::nullptr_t) noexcept
	{
		std::function<void (MqType&)> tmp;
		std::lock_guard<std::mutex> guard{ mqev.m_evmtx };
		swap(mqev.m_ev, tmp);
		mqev.m_ev_restore = false;
	}
};


/*
 * Specialized message queue for untyped messages.
 * Has no events (since that is only kept in the derivation instance).
 */
class void_msg_queue
{
public:
	typedef void element_type;

private:
	std::atomic<uintptr_t> m_size{ 0 };

	/*
	 * Dequeue up to max messages at once and return the number of messages
	 * thus dequeued.
	 */
	ILIAS_ASYNC_EXPORT uintptr_t _dequeue(uintptr_t max) noexcept;

public:
	void_msg_queue() = default;
	void_msg_queue(const void_msg_queue&) = delete;
	void_msg_queue& operator=(const void_msg_queue&) = delete;

	/* Move constructor. */
	void_msg_queue(void_msg_queue&& vmq) noexcept
	:	m_size(vmq.m_size.exchange(0, std::memory_order_relaxed))
	{
		/* Empty body. */
	}

	/* Test if the message queue is empty. */
	bool
	empty() const noexcept
	{
		return (this->m_size.load(std::memory_order_relaxed) == 0);
	}

protected:
	/* Enqueue multiple untyped messages. */
	void
	enqueue_n(size_t n) noexcept
	{
		this->m_size.fetch_add(n, std::memory_order_release);
	}

public:
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
:	public mq_detail::msg_queue_events<msg_queue<Type, Allocator>>
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
		return std::move(ptr);
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
	}

	void
	_enqueue(managed_pointer&& ptr) noexcept
	{
		assert(ptr && ptr.get_deleter().m_call_destructor);
		this->m_list.push_back(*ptr.release());
		this->_fire(*this);
	}

public:
	msg_queue()
	    noexcept(
		std::is_nothrow_constructible<list_type>::value &&
		std::is_nothrow_constructible<allocator_type>::value)
	{
		/* Empty body. */
	}

	template<typename... Args>
	msg_queue(Args&&... args)
	    noexcept(
		std::is_nothrow_constructible<list_type>::value &&
		std::is_nothrow_constructible<allocator_type, Args...>::value)
	:	m_alloc(std::forward<Args>(args)...)
	{
		/* Empty body. */
	}

	msg_queue(const msg_queue&) = delete;
	msg_queue& operator=(const msg_queue&) = delete;

	/* Move constructor. */
	msg_queue(msg_queue&& mq)
	    noexcept(
		std::is_nothrow_constructible<list_type>::value &&
		std::is_nothrow_constructible<allocator_type>::value)
	:	mq_detail::msg_queue_events<msg_queue>(std::move(mq)),
		m_alloc(std::move(mq.m_alloc))
	{
		/* Move elements between queues. */
		bool was_empty = true;
		while (auto elem = this->m_list.pop_front()) {
			this->m_list.push_back(*elem);
			was_empty = false;
		}
		if (!was_empty)
			this->_fire();
	}

	~msg_queue() noexcept
	{
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
:	public mq_detail::void_msg_queue,
	public mq_detail::msg_queue_events<msg_queue<void, Allocator>>
{
	msg_queue() = default;

	msg_queue(msg_queue&& mq) noexcept
	:	mq_detail::void_msg_queue(std::move(mq))
	{
		/* Empty body. */
	}

	/* Ignore allocator arguments, since we don't use one. */
	template<typename... Args>
	msg_queue(Args&&... args) noexcept
	{
		/* Empty body. */
	}

	/* Enqueue multiple untyped messages. */
	void
	enqueue_n(size_t n) noexcept
	{
		if (n > 0) {
			this->mq_detail::void_msg_queue::enqueue_n(n);
			this->_fire(*this);
		}
	}

	/* Enqueue a single untyped message. */
	void
	enqueue() noexcept
	{
		this->enqueue_n(1);
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
