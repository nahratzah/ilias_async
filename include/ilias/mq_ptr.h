#ifndef ILIAS_MQ_PTR_H
#define ILIAS_MQ_PTR_H

#include <ilias/msg_queue.h>
#include <ilias/refcnt.h>

namespace ilias {


template<typename Type, typename Allocator = std::allocator<Type>>
    class mq_in_ptr;
template<typename Type, typename Allocator = std::allocator<Type>>
    class mq_out_ptr;


namespace mq_ptr_detail {


class refcount
{
private:
	mutable std::atomic<std::uintptr_t> count;
	mutable std::atomic<std::uintptr_t> in;

public:
	bool
	has_input() const noexcept
	{
		return (this->in.load(std::memory_order_relaxed) > 0U);
	}

	refcount() = default;
	refcount(const refcount&) = delete;
	refcount(refcount&&) = delete;
	refcount& operator=(const refcount&) = delete;

protected:
	~refcount() = default;

public:
	class out_refcount_mgr
	{
	public:
		static void
		acquire(const refcount& v_, std::uintptr_t n) noexcept
		{
			auto old = v_.count.fetch_add(n,
			    std::memory_order_acquire);
			assert(old + n > old);
		}

		template<typename Type>
		static void
		release(const Type& v, std::uintptr_t n) noexcept
		{
			const refcount& v_ = v;
			auto old = v_.count.fetch_sub(n,
			    std::memory_order_release);
			assert(old >= n);
			if (old == n)
				delete &v;
		}
	};

	class in_refcount_mgr
	{
	private:
		using base = out_refcount_mgr;

	public:
		static void
		acquire(const refcount& v_, std::uintptr_t n) noexcept
		{
			auto old = v_.in.fetch_add(n,
			    std::memory_order_acquire);
			assert(old + n > old);

			if (old == n)
				base::acquire(v_, 1);
		}

		template<typename Type>
		static void
		release(const Type& v, std::uintptr_t n) noexcept
		{
			const refcount& v_ = v;
			auto old = v_.in.fetch_sub(n,
			    std::memory_order_release);
			assert(old >= n);

			if (old == n) {
				const_cast<Type&>(v)._fire(
				    &const_cast<Type&>(v));

				base::release(v, 1);
			}
		}
	};
};

template<typename Type, typename Allocator = std::allocator<Type>>
class refcounted_mq final
:	public refcount,
	protected mq_detail::data_msg_queue<Type, Allocator>,
	public mq_detail::msg_queue_events<mq_out_ptr<Type, Allocator>>
{
friend class refcount::in_refcount_mgr;

private:
	using data = mq_detail::data_msg_queue<Type, Allocator>;

public:
	using out_pointer = mq_out_ptr<Type, Allocator>;
	using in_pointer = mq_in_ptr<Type, Allocator>;

private:
	using events = mq_detail::msg_queue_events<out_pointer>;

protected:
	using events::_fire;

public:
	refcounted_mq() = default;

	template<typename... Args>
	refcounted_mq(Args&&... args)
	:	data(std::forward<Args>(args)...)
	{
		/* Empty body. */
	}

	refcounted_mq(const refcounted_mq& o) = delete;
	refcounted_mq(refcounted_mq&& o) = delete;
	refcounted_mq& operator=(const refcounted_mq& o) = delete;
	refcounted_mq& operator=(refcounted_mq&& o) = delete;

	using data::empty;
	using data::dequeue;

	template<typename... Args>
	void
	enqueue(Args&&... args)
	noexcept(
		noexcept(std::declval<data>().enqueue(
		    std::forward<Args>(args)...)))
	{
		this->data::enqueue(std::forward<Args>(args)...);
		this->_fire(this);
	}
};

template<typename Allocator>
class refcounted_mq<void, Allocator> final
:	public refcount,
	protected mq_detail::void_msg_queue,
	public mq_detail::msg_queue_events<mq_out_ptr<void, Allocator>>
{
friend class refcount::in_refcount_mgr;

private:
	using data = mq_detail::void_msg_queue;

public:
	using out_pointer = mq_out_ptr<void, Allocator>;
	using in_pointer = mq_in_ptr<void, Allocator>;

private:
	using events = mq_detail::msg_queue_events<out_pointer>;

protected:
	using events::_fire;

public:
	refcounted_mq() = default;
	refcounted_mq(const refcounted_mq& o) = delete;
	refcounted_mq(refcounted_mq&& o) = delete;
	refcounted_mq& operator=(const refcounted_mq& o) = delete;
	refcounted_mq& operator=(refcounted_mq&& o) = delete;

	using data::empty;
	using data::dequeue;

	void
	enqueue_n(size_t n) noexcept
	{
		if (n > 0) {
			this->data::enqueue_n(n);
			this->_fire(this);
		}
	}

	void
	enqueue() noexcept
	{
		this->enqueue_n(1);
	}
};


} /* namespace ilias::mq_ptr_detail */


template<typename Type, typename Allocator>
class mq_in_ptr
{
friend class mq_ptr_detail::refcounted_mq<Type, Allocator>;
friend class mq_out_ptr<Type, Allocator>;

private:
	using pointer = refpointer<
	    mq_ptr_detail::refcounted_mq<Type, Allocator>,
	    typename mq_ptr_detail::refcount::in_refcount_mgr>;

	pointer m_ptr;

	explicit mq_in_ptr(typename pointer::pointer p) noexcept
	:	m_ptr(p)
	{
		/* Empty body. */
	}

public:
	mq_in_ptr() = default;
	mq_in_ptr(const mq_in_ptr&) = default;
	mq_in_ptr(mq_in_ptr&&) = default;
	mq_in_ptr& operator=(const mq_in_ptr&) = default;
	mq_in_ptr& operator=(mq_in_ptr&&) = default;

	void
	swap(mq_in_ptr& other) noexcept
	{
		using std::swap;

		swap(this->m_ptr, other.m_ptr);
	}

	friend void
	swap(mq_in_ptr& a, mq_in_ptr& b) noexcept
	{
		a.swap(b);
	}

	template<typename... Args>
	void
	enqueue(Args&&... args)
	{
		if (!this->m_ptr)
			throw std::runtime_error("mq_in_ptr: null");

		this->m_ptr->enqueue(std::forward<Args>(args)...);
	}

	template<typename... Args>
	static mq_in_ptr
	create(Args&&... args)
	{
		return mq_in_ptr{ new typename pointer::element_type(
		    std::forward<Args>(args)...) };
	}
};

template<typename Allocator>
class mq_in_ptr<void, Allocator>
{
friend class mq_ptr_detail::refcounted_mq<void, Allocator>;
friend class mq_out_ptr<void, Allocator>;

private:
	using pointer = refpointer<
	    mq_ptr_detail::refcounted_mq<void, Allocator>,
	    typename mq_ptr_detail::refcount::in_refcount_mgr>;

	pointer m_ptr;

	explicit mq_in_ptr(typename pointer::pointer p) noexcept
	:	m_ptr(p)
	{
		/* Empty body. */
	}

public:
	mq_in_ptr() = default;
	mq_in_ptr(const mq_in_ptr&) = default;
	mq_in_ptr(mq_in_ptr&&) = default;
	mq_in_ptr& operator=(const mq_in_ptr&) = default;
	mq_in_ptr& operator=(mq_in_ptr&&) = default;

	void
	swap(mq_in_ptr& other) noexcept
	{
		using std::swap;

		swap(this->m_ptr, other.m_ptr);
	}

	friend void
	swap(mq_in_ptr& a, mq_in_ptr& b) noexcept
	{
		a.swap(b);
	}

	template<typename... Args>
	void
	enqueue()
	{
		if (!this->m_ptr)
			throw std::runtime_error("mq_in_ptr: null");

		this->m_ptr->enqueue();
	}

	void
	enqueue_n(size_t n)
	{
		if (!this->m_ptr)
			throw std::runtime_error("mq_in_ptr: null");

		this->m_ptr->enqueue_n(n);
	}

	explicit operator bool() const noexcept
	{
		return bool(this->m_ptr);
	}

	template<typename... Args>
	static mq_in_ptr
	create(Args&&... args)
	{
		return mq_in_ptr{ new typename pointer::element_type(
		    std::forward<Args>(args)...) };
	}
};

template<typename Type, typename Allocator>
class mq_out_ptr
{
friend class mq_ptr_detail::refcounted_mq<Type, Allocator>;
friend class mq_ptr_detail::refcount::in_refcount_mgr;

public:
	using callback_arg_type =
	    typename mq_ptr_detail::refcounted_mq<Type, Allocator>::callback_arg_type;

private:
	using pointer = refpointer<
	    mq_ptr_detail::refcounted_mq<Type, Allocator>,
	    typename mq_ptr_detail::refcount::in_refcount_mgr>;

	pointer m_ptr;

	/*
	 * Kill callback once message queue is permanently empty.
	 *
	 * Note that this is only done via the dequeue function,
	 * which is activated when the input closes.
	 */
	class post_check
	{
	private:
		mq_out_ptr& self;

	public:
		post_check(mq_out_ptr& self) noexcept
		:	self(self)
		{
			/* Empty body. */
		}

		post_check(const post_check&) = delete;
		post_check(post_check&&) = delete;

		~post_check() noexcept
		{
			if (self.m_ptr &&
			    !self.m_ptr->has_input() && self.m_ptr->empty())
				callback(self, nullptr);
		}
	};

	mq_out_ptr(typename pointer::pointer p) noexcept
	:	m_ptr(p)
	{
		/* Empty body. */
	}

public:
	mq_out_ptr() = default;
	mq_out_ptr(const mq_out_ptr&) = default;
	mq_out_ptr(mq_out_ptr&&) = default;
	mq_out_ptr& operator=(const mq_out_ptr&) = default;
	mq_out_ptr& operator=(mq_out_ptr&&) = default;

	mq_out_ptr(const mq_in_ptr<Type, Allocator>& in) noexcept
	:	m_ptr(in.m_ptr)
	{
		/* Empty body. */
	}

	void
	swap(mq_out_ptr& other) noexcept
	{
		using std::swap;

		swap(this->m_ptr, other.m_ptr);
	}

	friend void
	swap(mq_out_ptr& a, mq_out_ptr& b) noexcept
	{
		a.swap(b);
	}

	mq_out_ptr&
	operator=(const mq_in_ptr<Type, Allocator>& in) noexcept
	{
		mq_out_ptr temporary{ in };
		this->swap(temporary);
		return *this;
	}

	template<typename Functor, typename... Args>
	auto
	dequeue(Functor f, size_t n = 1)
	->	decltype(
		    std::declval<typename pointer::element_type>().dequeue(
		    std::move(f), n))
	{
		if (!this->m_ptr)
			throw std::runtime_error("mq_out_ptr: uninitialized");

		post_check pc{ *this };
		return this->m_ptr->dequeue(std::move(f), n);
	}

	bool
	empty() const noexcept
	{
		post_check pc{ const_cast<mq_out_ptr&>(*this) };
		return (!this->m_ptr || this->m_ptr->empty());
	}

	explicit operator bool() const noexcept
	{
		return bool(this->m_ptr);
	}

	friend void
	callback(mq_out_ptr& self, std::function<void (callback_arg_type)> fn)
	{
		if (!self.m_ptr)
			throw std::runtime_error("mq_out_ptr: uninitialized");

		post_check pc{ self };
		callback(*self.m_ptr, std::move(fn));
	}

	friend void
	callback(mq_out_ptr& self, std::nullptr_t)
	{
		if (!self.m_ptr)
			throw std::runtime_error("mq_out_ptr: uninitialized");
		callback(*self.m_ptr, nullptr);
	}
};


template<typename Type, typename Allocator, typename... Args>
mq_in_ptr<Type, Allocator>
new_mq_ptr(Args&&... args)
{
	return mq_in_ptr<Type, Allocator>::create(std::forward<Args>(args)...);
}

template<typename Type, typename... Args>
mq_in_ptr<Type>
new_mq_ptr(Args&&... args)
{
	return mq_in_ptr<Type>::create(std::forward<Args>(args)...);
}

extern template ILIAS_ASYNC_EXPORT
	mq_in_ptr<void, std::allocator<void>>
	new_mq_ptr<void, std::allocator<void>>();
extern template ILIAS_ASYNC_EXPORT
	mq_in_ptr<int, std::allocator<int>>
	new_mq_ptr<int, std::allocator<int>>();


} /* namespace ilias */

#endif /* ILIAS_MQ_PTR_H */
