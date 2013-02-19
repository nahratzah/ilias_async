/*
 * Copyright (c) 2012 - 2013 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef ILIAS_PROMISE_H
#define ILIAS_PROMISE_H

#include <ilias/ilias_async_export.h>
#include <ilias/refcnt.h>
#include <atomic>
#include <cassert>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>


namespace ilias {


template<typename Type> class promise;
template<typename Type> class future;


class ILIAS_ASYNC_EXPORT broken_promise
:	public std::runtime_error
{
public:
	broken_promise();
	~broken_promise() noexcept;
};


class ILIAS_ASYNC_EXPORT uninitialized_promise
:	public std::logic_error
{
public:
	uninitialized_promise();
	~uninitialized_promise();
};


class ILIAS_ASYNC_EXPORT promise_cb_installed
:	public std::logic_error
{
public:
	promise_cb_installed();
	~promise_cb_installed();
};


/*
 * Argument to future callback functions, which can be used to inhibit
 * starting the promise.
 */
enum promise_start
{
	PROM_START,	/* Start promise immediately. */
	PROM_DEFER	/* Don't start promise. */
};


namespace prom_detail {


class base_prom_data
{
public:
	typedef std::function<void(base_prom_data&)> execute_fn;

protected:
	enum class state
	:	int
	{
		S_NIL = 0,
		S_BUSY = 1,
		S_SET = 2,
		S_BROKEN = 3,
		S_EXCEPT = 4
	};

private:
	enum class cb_state
	:	int
	{
		CB_NONE,
		CB_NEED,
		CB_DONE
	};

	std::atomic<state> m_state{ state::S_NIL };
	cb_state m_cbstate{ cb_state::CB_NONE };
	std::atomic<uintptr_t> m_promrefs{ 0 };

	/* Protect callbacks. */
	std::mutex m_cblck;
	/* Promise fullfilling function. */
	execute_fn m_execute;
	/* Callbacks for once the promise completes. */
	std::vector<execute_fn> m_callbacks;

	/*
	 * Wrapper around invoking execute_fn, so it can be put in
	 * non-noexcept functions without messing with the exception paths.
	 * (Callbacks are not allowed to throw!)
	 */
	void
	invoke_execute_fn(const execute_fn& fn) noexcept
	{
		fn(*this);
	}

protected:
	ILIAS_ASYNC_EXPORT ~base_prom_data() noexcept;

	state
	current_state() const noexcept
	{
		return this->m_state.load(std::memory_order_relaxed);
	}

	/*
	 * Change prom_data to busy state.
	 */
	class lock
	{
	private:
		base_prom_data* m_pd;

	public:
		lock(const lock&) = delete;
		lock& operator=(const lock&) = delete;

		lock() noexcept
		:	m_pd(nullptr)
		{
			/* Empty body. */
		}

		lock(lock&& l) noexcept
		:	m_pd(l.m_pd)
		{
			l.m_pd = nullptr;
		}

		lock(base_prom_data& pd) noexcept
		:	lock()
		{
			state s = state::S_NIL;
			if (pd.m_state.compare_exchange_strong(s,
			    state::S_BUSY,
			    std::memory_order_acquire,
			    std::memory_order_relaxed)) {
				this->m_pd = &pd;
			}
		}

		~lock() noexcept
		{
			assert(!this->m_pd);
		}

		explicit operator bool() const noexcept
		{
			return this->m_pd;
		}

		void
		release(state s) noexcept
		{
			assert(s != state::S_BUSY && s != state::S_NIL);
			assert(this->m_pd);
			auto old = this->m_pd->m_state.exchange(s);
			assert(old == state::S_BUSY);
			this->m_pd->_on_complete();
			this->m_pd = nullptr;
		}
	};

public:
	/*
	 * Set execution callback.
	 *
	 * This callback is executed on demand, when the promise detects
	 * it will be required.
	 */
	ILIAS_ASYNC_EXPORT void set_execute_fn(execute_fn fn);
	/*
	 * Start the promise.
	 * If a callback is present, it will be executed immediately.
	 * If a callback is installed later, it will be executed immediately.
	 */
	ILIAS_ASYNC_EXPORT void start() noexcept;

private:
	void _start(std::unique_lock<std::mutex>) noexcept;
	void _on_complete() noexcept;

public:
	/*
	 * Add a callback to be executed once the promise is ready.
	 *
	 * Callback is incoked immediately if the promise is already complete.
	 */
	ILIAS_ASYNC_EXPORT void add_callback(execute_fn,
	    promise_start = PROM_DEFER);

	bool
	has_value() const noexcept
	{
		return this->current_state() == state::S_SET;
	}

	bool
	has_exception() const noexcept
	{
		return this->current_state() == state::S_EXCEPT;
	}

	bool
	is_broken_promise() const noexcept
	{
		return this->current_state() == state::S_BROKEN;
	}

	bool
	ready() const noexcept
	{
		const auto s = this->current_state();
		return s != state::S_NIL && s != state::S_BUSY;
	}

	void
	wait() const noexcept
	{
		while (!this->ready())
			std::this_thread::yield();
	}

	/* Inform promdata that there is one more promise referencing this. */
	void
	increment_promref() noexcept
	{
		this->m_promrefs.fetch_add(1, std::memory_order_acquire);
	}

	/* Inform promdata that there is one less promise referencing this. */
	void
	decrement_promref() noexcept
	{
		if (this->m_promrefs.fetch_sub(1,
		    std::memory_order_release) == 0 &&
		    !this->m_execute) {
			/*
			 * Promise cannot complete anymore.
			 * Mark as broken.
			 */
			state s = state::S_NIL;
			this->m_state.compare_exchange_strong(s,
			    state::S_BROKEN,
			    std::memory_order_release,
			    std::memory_order_relaxed);
		}
	}
};


template<typename Type>
class prom_data
:	public base_prom_data,
	public refcount_base<prom_data<Type>>
{
public:
	typedef Type value_type;
	typedef value_type& reference;
	typedef const value_type& const_reference;

private:
	union data_type {
		value_type val;
		std::exception_ptr exc;

		~data_type() noexcept {};	/* Handled by prom_data. */
	};

	data_type m_data;

	static constexpr bool noexcept_destroy =
	    std::is_nothrow_destructible<value_type>::value;

	template<typename T>
	static void
	destroy(T&& v) noexcept(std::is_nothrow_destructible<T>::value)
	{
		v.~T();
	}

	/* Assign exception to this promise data. */
	void
	_assign_exception(base_prom_data::lock lck, std::exception_ptr ptr)
	    noexcept
	{
		assert(lck);
		assert(ptr);
		new (&this->m_data.exc) std::exception_ptr(std::move(ptr));
		lck.release(state::S_EXCEPT);
	}

	/*
	 * Construct promise value.
	 * This implementation is for constructors that do not throw.
	 */
	template<typename... Args>
	typename std::enable_if<std::is_nothrow_constructible<value_type,
	    Args...>::value, void>::type
	_assign_value(base_prom_data::lock lck, Args&&... args) noexcept
	{
		assert(lck);
		new (&this->m_data.val)
		    value_type(std::forward<Args>(args)...);
		lck.release(state::S_SET);
	}

	/*
	 * Construct promise value.
	 * This implementation is for constructors that do throw.
	 */
	template<typename... Args>
	typename std::enable_if<!std::is_nothrow_constructible<value_type,
	    Args...>::value, void>::type
	_assign_value(base_prom_data::lock lck, Args&&... args) noexcept
	{
		assert(lck);
		try {
			new (&this->m_data.val)
			    value_type(std::forward<Args>(args)...);
			lck.release(state::S_SET);
		} catch (...) {
			this->_assign_exception(std::move(lck),
			    std::current_exception());
		}
	}

public:
	prom_data() = default;

	~prom_data() noexcept(noexcept_destroy)
	{
		if (this->has_value())
			destroy(std::move(this->m_data.val));
		else if (this->has_exception())
			destroy(std::move(this->m_data.exc));
	}

	prom_data(const prom_data&) = delete;
	prom_data(prom_data&&) = delete;
	prom_data& operator=(const prom_data&) = delete;

	/*
	 * Assign exception to promise data.
	 *
	 * Returns true if the promise transitioned from
	 * unassigned to assigned.
	 */
	bool
	assign_exception(std::exception_ptr exc)
	{
		if (!exc) {
			throw std::invalid_argument("exception pointer "
			    "is a nullptr");
		}

		base_prom_data::lock lck{ *this };
		if (!lck)
			return false;

		_assign_exception(std::move(lck), exc);
		return true;
	}

	/*
	 * Assign value to promise data.
	 *
	 * Returns true if the promise transitioned from
	 * unassigned to assigned.
	 */
	template<typename... Args>
	bool
	assign(Args&&... args)
	    noexcept(std::is_nothrow_constructible<value_type, Args...>::value)
	{
		base_prom_data::lock lck{ *this };
		if (!lck)
			return false;

		this->_assign_value(std::move(lck),
		    std::forward<Args>(args)...);
		return true;
	}

	/*
	 * Block on the promise until it completes.
	 * Returns the value the promise was set to.
	 *
	 * If the promise was assigned an exception,
	 * the exception is thrown.
	 * If the promise was broken, a broken_promise()
	 * exception is thrown.
	 */
	const_reference
	get() const
	{
		this->wait();

		switch (this->current_state()) {
		case state::S_NIL:
		case state::S_BUSY:
			assert(false);
			while (true);	/* Undefined behaviour: spin. */
		case state::S_SET:
			std::atomic_thread_fence(std::memory_order_acquire);
			return this->m_data.val;
		case state::S_BROKEN:
			throw broken_promise();
		case state::S_EXCEPT:
			std::atomic_thread_fence(std::memory_order_acquire);
			std::rethrow_exception(this->m_data.exc);
		}
	}
};


template<>
class prom_data<void>
:	public base_prom_data,
	public refcount_base<prom_data<void>>
{
public:
	typedef void value_type;
	typedef void reference;
	typedef void const_reference;

private:
	std::exception_ptr m_exc;

public:
	prom_data() = default;
	~prom_data() = default;

	prom_data(const prom_data&) = delete;
	prom_data(prom_data&&) = delete;
	prom_data& operator=(const prom_data&) = delete;

	ILIAS_ASYNC_EXPORT bool assign_exception(std::exception_ptr exc);
	ILIAS_ASYNC_EXPORT bool assign() noexcept;
	ILIAS_ASYNC_EXPORT void get() const;
};


} /* namespace ilias::prom_detail */


template<typename Type>
class promise
{
friend class future<Type>;
friend class prom_detail::prom_data<Type>;

public:
	typedef typename prom_detail::prom_data<Type>::value_type value_type;
	typedef typename prom_detail::prom_data<Type>::reference reference;
	typedef typename prom_detail::prom_data<Type>::const_reference
	    const_reference;

private:
	refpointer<prom_detail::prom_data<Type>> m_ptr;

public:
	friend void
	swap(promise& p, promise& q) noexcept
	{
		using std::swap;

		swap(p.m_ptr, q.m_ptr);
	}

	/*
	 * Create a promise.
	 * Note that the default constructed promise is uninitialized.
	 */
	promise() = default;

private:
	promise(refpointer<prom_detail::prom_data<Type>> pd) noexcept
	:	m_ptr(pd)
	{
		if (this->m_ptr)
			this->m_ptr->increment_promref();
	}

	/*
	 * Wrap a callback functor so it can be used by base_prom_data.
	 */
	template<typename Fn>
	static prom_detail::base_prom_data::execute_fn
	create_callback(Fn&& fn)
	{
		typedef prom_detail::base_prom_data::execute_fn execute_fn;

		return execute_fn([fn](prom_detail::base_prom_data& bpd) {
			promise p{
			    &static_cast<prom_detail::prom_data<Type>&>(bpd) };
			promise q = p;	/* In case fn decides to modify p. */
			try {
				fn(std::move(p));
			} catch (...) {
				q.set_exception(std::current_exception());
			}
		    });
	}

public:
	promise(const promise& o) noexcept
	:	promise(o.m_ptr)
	{
		/* Empty body. */
	}

	promise(promise&& o) noexcept
	:	m_ptr(std::move(o.m_ptr))
	{
		/* Empty body. */
	}

	~promise() noexcept
	{
		if (this->m_ptr)
			this->m_ptr->decrement_promref();
	}

	/*
	 * Create an initialized promise.
	 */
	static promise
	create()
	{
		return promise(new prom_detail::prom_data<Type>());
	}

	promise&
	operator=(promise o) noexcept
	{
		swap(*this, o);
		return *this;
	}

	bool
	operator==(const promise& o) noexcept
	{
		return this->m_ptr == o.m_ptr;
	}

	/*
	 * Create a callback which will assign to the promise.
	 */
	template<typename Fn>
	friend void
	callback(promise& p, Fn&& fn)
	{
		if (!p.m_ptr)
			throw uninitialized_promise();
		p.m_ptr->set_execute_fn(create_callback(
		    std::forward<Fn>(fn)));
	}

	/* Test if the promise is initialized. */
	bool
	is_initialized() const noexcept
	{
		return bool(this->m_ptr);
	}

	/* True iff the promise holds a value. */
	bool
	has_value() const noexcept
	{
		return this->m_ptr && this->m_ptr->has_value();
	}

	/*
	 * True iff the promise holds an exception.
	 *
	 * Note: false if the promise is a broken promise.
	 */
	bool
	has_exception() const noexcept
	{
		return this->m_ptr && this->m_ptr->has_exception();
	}

	/*
	 * True iff the promise has completed.
	 *
	 * This is the same as:
	 * has_value() || has_exception().
	 *
	 * Since a promise can only be assigned to once,
	 * a ready promise cannot be assigned to.
	 */
	bool
	ready() const noexcept
	{
		return this->m_ptr && this->m_ptr->ready();
	}

	/*
	 * Assign a value to the promise.
	 *
	 * The value_type constructor will be called
	 * with the specified arguments.
	 *
	 * Returns true if the promise was not ready prior to this call.
	 */
	template<typename... Args>
	bool
	set(Args&&... args)
	{
		if (!this->m_ptr)
			throw uninitialized_promise();
		return this->m_ptr->assign(std::forward<Args>(args)...);
	}

	/*
	 * Assign an exception to the promise.
	 *
	 * Returns true if the promise was not ready prior to this call.
	 */
	bool
	set_exception(std::exception_ptr ptr)
	{
		if (!this->m_ptr)
			throw uninitialized_promise();
		return this->m_ptr->assign_exception(std::move(ptr));
	}

	/* Start execution of the promise. */
	void
	start()
	{
		if (!this->m_ptr)
			throw uninitialized_promise();
		return this->m_ptr->start();
	}
};

template<typename Type>
class future
{
public:
	typedef typename prom_detail::prom_data<Type>::value_type value_type;
	typedef typename prom_detail::prom_data<Type>::reference reference;
	typedef typename prom_detail::prom_data<Type>::const_reference
	    const_reference;

private:
	refpointer<prom_detail::prom_data<Type>> m_ptr;

	future(refpointer<prom_detail::prom_data<Type>> pd) noexcept
	:	m_ptr(pd)
	{
		/* Empty body. */
	}

	/*
	 * Wrap a callback functor so it can be used by base_prom_data.
	 */
	template<typename Fn>
	static prom_detail::base_prom_data::execute_fn
	create_callback(Fn&& fn)
	{
		typedef prom_detail::base_prom_data::execute_fn execute_fn;

		return execute_fn([fn](prom_detail::base_prom_data& bpd) {
			future p{
			    &static_cast<prom_detail::prom_data<Type>&>(bpd) };
			fn(std::move(p));
		    });
	}

public:
	friend void
	swap(future& p, future& q) noexcept
	{
		using std::swap;
		swap(p.m_ptr, q.m_ptr);
	}

	future() = default;
	future(const future&) = default;
	future(future&&) = default;

	future(const promise<Type>& p)
	:	m_ptr(p.m_ptr)
	{
		if (!this->m_ptr)
			throw uninitialized_promise();
	}

	future&
	operator=(future f) noexcept
	{
		swap(*this, f);
		return *this;
	}

	future&
	operator=(const promise<Type>& p)
	{
		return *this = future(p);
	}

	bool
	operator==(const future& o) noexcept
	{
		return this->m_ptr == o.m_ptr;
	}

	bool
	operator==(const promise<Type>& p) noexcept
	{
		return (this->m_ptr == p.m_ptr);
	}

	/*
	 * Add a callback which will be called once the promise is complete.
	 */
	template<typename Fn>
	friend void
	callback(future& f, Fn&& fn, promise_start ps = PROM_START)
	{
		if (!f.m_ptr)
			throw uninitialized_promise();
		f.m_ptr->add_callback(create_callback(
		    std::forward<Fn>(fn)), ps);
	}

	/* Test if the future is initialized. */
	bool
	is_initialized() const noexcept
	{
		return bool(this->m_ptr);
	}

	/* True iff the future holds a value. */
	bool
	has_value() const noexcept
	{
		return this->m_ptr && this->m_ptr->has_value();
	}

	/*
	 * True iff the future holds an exception.
	 *
	 * Note: false if the future is a broken promise.
	 */
	bool
	has_exception() const noexcept
	{
		return this->m_ptr && this->m_ptr->has_exception();
	}

	/* True iff the future is a broken promise. */
	bool
	is_broken_promise() const noexcept
	{
		return this->m_ptr && this->m_ptr->is_broken_promise();
	}

	/*
	 * True iff the promise has completed.
	 *
	 * This is the same as:
	 * has_value() || has_exception() || is_broken_promise()
	 *
	 * This call guarantees that wait() and get() will not block.
	 */
	bool
	ready() const noexcept
	{
		return this->m_ptr && this->m_ptr->ready();
	}

	/*
	 * Wait for the promise to become ready.
	 * After this call, the promise will be ready,
	 * unless this future was uninitialized.
	 */
	void
	wait() const noexcept
	{
		if (this->m_ptr)
			this->m_ptr->wait();
	}

	/*
	 * Return the result of the promise.
	 *
	 * If the future holds an exception, the exception is thrown.
	 * If the future is broken, a broken_promise exception is thrown.
	 * If the future is uninitialized, a uninitialized_promise exception
	 * is thrown.
	 */
	const_reference
	get() const
	{
		if (!this->m_ptr)
			throw uninitialized_promise();
		return this->m_ptr->get();
	}

	/* Start execution of the promise. */
	void
	start()
	{
		if (!this->m_ptr)
			throw uninitialized_promise();
		return this->m_ptr->start();
	}
};


template<typename Type>
bool
operator==(const future<Type>& p, const promise<Type>& q) noexcept
{
	return (q == p);
}


/* Create a new promise with the given result type. */
template<typename Type>
promise<Type>
new_promise()
{
	return promise<Type>::create();
}

/* Create a new promise with an associated callback. */
template<typename Type, typename... Args>
promise<Type>
new_promise(Args&&... args)
{
	auto rv = new_promise<Type>();
	callback(rv, std::forward<Args>(args)...);
	return rv;
}


} /* namespace ilias */

#endif /* ILIAS_PROMISE_H */
