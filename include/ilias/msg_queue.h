/*
 * Copyright (c) 2012 Ariane van der Steldt <ariane@stack.nl>
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
#include <ilias/refcnt.h>
#include <ilias/workq.h>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace ilias {
namespace msg_queue_detail {


/* Adapter, to push Type on a ll_list. */
template<typename Type>
class msgq_ll_data :
	public ll_base_hook<>
{
public:
	typedef Type value_type;

private:
	value_type m_value;

public:
	template<typename... Args>
	msgq_ll_data(Args&&... args) noexcept(std::is_nothrow_constructible<value_type, Args...>::value) :
		m_value(std::forward<Args>(args)...)
	{
		/* Empty body. */
	}

	value_type
	move_if_noexcept()
	    noexcept(std::is_nothrow_move_constructible<value_type>::value || std::is_nothrow_copy_constructible<value_type>::value)
	{
		return std::move_if_noexcept(this->m_value);
	}
};


/* Optional element content for message queue (comparable to boost::optional). */
template<typename Type>
class msgq_opt_data
{
public:
	typedef Type value_type;
	typedef value_type& reference;
	typedef const value_type& const_reference;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;

private:
	bool m_has_value;
	union { value_type impl; } m_value;

public:
	msgq_opt_data() noexcept :
		m_has_value(false)
	{
		/* Empty body. */
	}

	msgq_opt_data(const msgq_opt_data& o) noexcept(std::is_nothrow_copy_constructible<value_type>::value) :
		m_has_value(false)
	{
		if (o.m_has_value) {
			::new(&this->m_value.impl) value_type(o.m_value.impl);
			this->m_has_value = true;
		}
	}

	msgq_opt_data(msgq_opt_data&& o)
	    noexcept((std::is_nothrow_move_constructible<value_type>::value || std::is_nothrow_copy_constructible<value_type>::value) && std::is_nothrow_destructible<value_type>::value) :
		m_has_value(false)
	{
		this->swap(o);
	}

	explicit msgq_opt_data(const value_type& v) noexcept(std::is_nothrow_copy_constructible<value_type>::value) :
		m_has_value(true)
	{
		::new(&this->m_value.impl) value_type(v);
	}

	explicit msgq_opt_data(value_type&& v) noexcept(std::is_nothrow_move_constructible<value_type>::value) :
		m_has_value(true)
	{
		::new(&this->m_value.impl) value_type(std::move(v));
	}

	~msgq_opt_data() noexcept(std::is_nothrow_destructible<value_type>::value)
	{
		if (this->m_has_value)
			this->m_value.impl.~value_type();
	}

	msgq_opt_data&
	operator=(msgq_opt_data o)
	    noexcept((std::is_nothrow_move_constructible<value_type>::value || std::is_nothrow_copy_constructible<value_type>::value) && std::is_nothrow_destructible<value_type>::value)
	{
		this->swap(o);
		return *this;
	}

	void
	swap(msgq_opt_data& o)
	    noexcept((std::is_nothrow_move_constructible<value_type>::value || std::is_nothrow_copy_constructible<value_type>::value) && std::is_nothrow_destructible<value_type>::value)
	{
		using std::swap;

		if (this->m_has_value && o.m_has_value)
			swap(this->m_value.impl, o.m_value.impl);
		else if (this->m_has_value) {
			::new(&o.m_value.impl) value_type(std::move_if_noexcept(this->m_value.impl));
			swap(this->m_has_value, o.m_has_value);
			this->m_value.impl.~value_type();
		} else if (o.m_has_value) {
			::new(&this->m_value.impl) value_type(std::move_if_noexcept(o.m_value.impl));
			swap(this->m_has_value, o.m_has_value);
			o.m_value.impl.~value_type();
		}
	}

	friend void
	swap(msgq_opt_data& a, msgq_opt_data& b)
	    noexcept((std::is_nothrow_move_constructible<value_type>::value || std::is_nothrow_copy_constructible<value_type>::value) && std::is_nothrow_destructible<value_type>::value)
	{
		a.swap(b);
	}

	pointer
	get() noexcept
	{
		return (this->m_has_value ? nullptr : &this->m_value.impl);
	}

	const_pointer
	get() const noexcept
	{
		return (this->m_has_value ? nullptr : &this->m_value.impl);
	}

	reference
	operator*() noexcept
	{
		return *this->get();
	}

	const_reference
	operator*() const noexcept
	{
		return *this->get();
	}

	pointer
	operator->() noexcept
	{
		return this->get();
	}

	const_pointer
	operator->() const noexcept
	{
		return this->get();
	}

	value_type
	move() noexcept(std::is_nothrow_move_constructible<value_type>::value)
	{
		assert(this->m_has_value);
		return std::move(this->m_value.impl);
	}

	explicit operator bool() const noexcept
	{
		return this->m_has_value;
	}
};


/* Allocator adapter, to abstract Alloc away. */
template<typename DataType, typename Alloc>
class msg_queue_alloc
{
public:
	typedef DataType* alloc_pointer;
	typedef typename std::allocator_traits<Alloc>::template rebind_alloc<DataType> allocator_type;
	typedef std::allocator_traits<allocator_type> allocator_traits;

private:
	allocator_type m_alloc;

public:
	msg_queue_alloc() noexcept(std::is_nothrow_default_constructible<allocator_type>::value) :
		m_alloc()
	{
		/* Empty body. */
	}

	msg_queue_alloc(Alloc alloc) noexcept(std::is_nothrow_constructible<allocator_type, Alloc>::value) :
		m_alloc(alloc)
	{
		/* Empty body. */
	}

	template<typename... Args>
	alloc_pointer
	create(Args&&... args)
	{
		alloc_pointer rv;

		const auto p = allocator_traits::allocate(this->m_alloc, 1);
		try {
			allocator_traits::construct(this->m_alloc, p, std::forward<Args>(args)...);
			rv = alloc_pointer(p);
		} catch (...) {
			allocator_traits::deallocate(this->m_alloc, p, 1);
			throw;
		}
		assert(rv);
		return rv;
	}

	void
	destroy(alloc_pointer p)
	{
		if (p) {
			allocator_traits::destroy(this->m_alloc, p);
			allocator_traits::deallocate(this->m_alloc, p, 1);
		}
	}

	typename allocator_traits::size_type
	max_size() const noexcept
	{
		return allocator_traits::max_size(this->m_alloc);
	}


	msg_queue_alloc(const msg_queue_alloc&) = delete;
	msg_queue_alloc& operator=(const msg_queue_alloc&) = delete;
};


/* Reference counting for message queue. */
class msg_queue_ref
{
public:
	enum ref_side {
		REF_IN,
		REF_OUT
	};

private:
	static const unsigned int MQ_HAS_IN = 0x01;
	static const unsigned int MQ_HAS_OUT = 0x02;

	mutable std::atomic<unsigned int> m_in_ref, m_out_ref;
	mutable std::atomic<unsigned int> m_state;

public:
	msg_queue_ref() noexcept :
		m_in_ref(0),
		m_out_ref(0),
		m_state(MQ_HAS_IN | MQ_HAS_OUT)
	{
		/* Empty body. */
	}

	msg_queue_ref(const msg_queue_ref&) noexcept :
		msg_queue_ref()
	{
		/* Empty body. */
	}

	/* Test if the input side is connected. */
	bool
	has_input_conn() const noexcept
	{
		return (this->m_in_ref.load(std::memory_order_relaxed) > 0);
	}

	/* Test if the output side is connected. */
	bool
	has_output_conn() const noexcept
	{
		return (this->m_out_ref.load(std::memory_order_relaxed) > 0);
	}

	template<typename Derived, ref_side Side>
	struct refman;
};

/* Refcount management, input side. */
template<typename Derived>
struct msg_queue_ref::refman<Derived, msg_queue_ref::REF_IN>
{
	void
	acquire(const Derived& v) noexcept
	{
		const msg_queue_ref& v_ = v;
		v_.m_in_ref.fetch_add(1, std::memory_order_acquire);
	}

	void
	release(const Derived& v) noexcept
	{
		const msg_queue_ref& v_ = v;
		const auto p = v_.m_in_ref.fetch_sub(1, std::memory_order_release);
		assert(p > 0);
		if (p == 1 &&
		    v_.m_state.fetch_and(~MQ_HAS_IN, std::memory_order_acquire) == MQ_HAS_IN)
			delete &v;
	}
};
/* Refcount management, output side. */
template<typename Derived>
struct msg_queue_ref::refman<Derived, msg_queue_ref::REF_OUT>
{
	void
	acquire(const Derived& v) noexcept
	{
		const msg_queue_ref& v_ = v;
		v_.m_out_ref.fetch_add(1, std::memory_order_acquire);
	}

	void
	release(const Derived& v) noexcept
	{
		const msg_queue_ref& v_ = v;
		const auto p = v_.m_out_ref.fetch_sub(1, std::memory_order_release);
		assert(p > 0);
		if (p == 1 &&
		    v_.m_state.fetch_and(~MQ_HAS_OUT, std::memory_order_acquire) == MQ_HAS_OUT)
			delete &v;
	}
};


/* Limitations for message queue. */
class msg_queue_size
{
public:
	typedef std::size_t size_type;

private:
	std::atomic<size_type> m_eff_size;
	std::atomic<size_type> m_eff_avail;
	std::atomic<size_type> m_overflow;
	size_type m_max_size;
	mutable std::mutex m_setsz_mtx;	/* Protect modification of max_size. */

	ILIAS_ASYNC_EXPORT bool begin_insert() noexcept;
	ILIAS_ASYNC_EXPORT void commit_insert() noexcept;
	ILIAS_ASYNC_EXPORT void cancel_insert() noexcept;
	void avail_inc() noexcept;

protected:
	ILIAS_ASYNC_EXPORT void commit_remove() noexcept;

	class insert_lock
	{
	private:
		msg_queue_size* self;

	public:
		insert_lock() noexcept :
			self(nullptr)
		{
			/* Empty body. */
		}

		insert_lock(msg_queue_size& self) :
			self(&self)
		{
			if (!self.begin_insert())
				throw std::length_error("msg_queue: full");
		}

		insert_lock(insert_lock&& il) noexcept :
			self(nullptr)
		{
			this->swap(il);
		}

		~insert_lock() noexcept
		{
			if (this->self)
				self->cancel_insert();
		}

		insert_lock&
		operator=(insert_lock&& il) noexcept
		{
			insert_lock(std::move(il)).swap(*this);
			return *this;
		}

		void
		commit() noexcept
		{
			assert(self);
			self->commit_insert();
			self = nullptr;
		}

		msg_queue_size*
		get_lockable() const noexcept
		{
			return this->self;
		}

		void
		swap(insert_lock& il) noexcept
		{
			using std::swap;

			swap(this->self, il.self);
		}

		friend void
		swap(insert_lock& a, insert_lock& b) noexcept
		{
			a.swap(b);
		}


		insert_lock(const insert_lock&) = delete;
		insert_lock& operator=(const insert_lock&) = delete;
	};

	bool
	eff_empty() const noexcept
	{
		return (this->m_eff_size.load(std::memory_order_relaxed) == 0);
	}

	ILIAS_ASYNC_EXPORT bool eff_attempt_remove() noexcept;

public:
	msg_queue_size(size_type maxsz = SIZE_MAX) noexcept :
		m_eff_size(0U),
		m_eff_avail(maxsz),
		m_overflow(0U),
		m_max_size(maxsz),
		m_setsz_mtx()
	{
		/* Empty body. */
	}

	bool
	full() const noexcept
	{
		return (this->m_eff_avail.load(std::memory_order_relaxed) == 0);
	}

	ILIAS_ASYNC_EXPORT size_type get_max_size() const noexcept;
	ILIAS_ASYNC_EXPORT void set_max_size(size_type newsz) noexcept;
};


/*
 * Message queue data.
 *
 * Hold elements in the message queue, provide push/pop operations.
 */
template<typename Type, typename Alloc>
class msg_queue_data :
	protected msg_queue_alloc<msgq_ll_data<Type>, Alloc>,
	public msg_queue_size
{
public:
	typedef Type element_type;
	typedef msgq_opt_data<element_type> opt_element_type;
	typedef std::size_t size_type;

protected:
	typedef msgq_ll_data<Type> ll_data_type;
	typedef ilias::ll_list<ll_base<ll_data_type> > list_type;

private:
	list_type m_list;

public:
	msg_queue_data() :
		m_list()
	{
		/* Empty body. */
	}

	msg_queue_data(size_type maxsize) :
		msg_queue_size(std::min(maxsize, this->max_size())),
		m_list()
	{
		/* Empty body. */
	}

	~msg_queue_data() noexcept
	{
		using namespace std::placeholders;

		this->m_list.clear_and_dispose(std::bind(&msg_queue_alloc<ll_data_type, Alloc>::destroy, this, _1));
	}

	bool
	empty() const noexcept
	{
		return this->m_list.empty();
	}

protected:
	void
	push(element_type v)
	{
		this->push(this->create(std::move(v)));
	}

	template<typename... Args>
	void
	emplace(Args&&... args)
	{
		this->push(this->create(std::forward<Args>(args)...));
	}

	void
	push(ll_data_type* ld)
	{
		this->push(insert_lock(*this), ld);
	}

	void
	push(insert_lock&& lck, ll_data_type* ld) noexcept
	{
		assert(lck.get_lockable() == this);
		assert(ld);
		auto rv = this->m_list.push_back(*ld);
		assert(rv);
		lck.commit();
	}

	opt_element_type
	pop() noexcept(noexcept(((ll_data_type*)0)->move_if_noexcept()))
	{
		opt_element_type rv;

		ll_data_type* p = this->m_list.pop_front();
		if (p) {
			this->commit_remove();

			try {
				rv = opt_element_type(p->move_if_noexcept());
			} catch (...) {
				this->destroy(p);
				throw;
			}
			this->destroy(p);
		}
		return rv;
	}
};


/*
 * Message queue specialization for void.
 *
 * Since void can hold no data, no list is required to keep track of the data either.
 */
template<>
class msg_queue_data<void, void> :
	public msg_queue_size
{
public:
	typedef void element_type;
	typedef bool opt_element_type;

	msg_queue_data() noexcept :
		msg_queue_size()
	{
		/* Empty body. */
	}

	msg_queue_data(size_type maxsz) noexcept :
		msg_queue_size(maxsz)
	{
		/* Empty body. */
	}

	bool
	empty() const noexcept
	{
		return this->eff_empty();
	}

protected:
	void
	push()
	{
		this->push(insert_lock(*this));
	}

	void
	push(insert_lock&& lck) noexcept
	{
		assert(lck.get_lockable() == this);
		lck.commit();
	}

	bool
	pop() noexcept
	{
		return this->eff_attempt_remove();
	}
};


/* Hang on to a pointer in the given msg_queue_alloc. */
template<typename DataType, typename Alloc>
class prepare_hold
{
public:
	typedef msg_queue_alloc<DataType, Alloc> alloc_type;
	typedef typename DataType::value_type value_type;

private:
	alloc_type* m_alloc;
	typename alloc_type::alloc_pointer m_ptr;

public:
	prepare_hold() noexcept :
		m_alloc(nullptr),
		m_ptr(nullptr)
	{
		return;
	}

	template<typename... Args>
	prepare_hold(alloc_type& alloc, Args&&... args) noexcept :
		prepare_hold()
	{
		this->m_alloc = &alloc;
		this->m_ptr = this->m_alloc->create(std::forward<Args>(args)...);
	}

	prepare_hold(prepare_hold&& o) noexcept :
		m_alloc(o.alloc),
		m_ptr(o.m_ptr)
	{
		o.m_ptr = nullptr;
	}

	~prepare_hold() noexcept(noexcept(alloc_type::destroy(alloc_type::alloc_pointer)))
	{
		if (this->m_ptr) {
			assert(this->m_alloc);
			this->m_alloc->destroy(this->m_ptr);
		}
	}

	prepare_hold&
	operator=(prepare_hold&& o)
	{
		this->swap(o);
		return *this;
	}

	void
	assign(const value_type& v)
	{
		if (!this->m_alloc)
			throw std::invalid_argument("prepare: assign called without allocator");
		if (this->m_ptr)
			**this->m_ptr = v;
		else
			this->m_ptr = this->m_alloc->create(v);
	}

	void
	assign(value_type&& v)
	{
		if (!this->m_alloc)
			throw std::invalid_argument("prepare: assign called without allocator");
		if (this->m_ptr)
			**this->m_ptr = std::move(v);
		else
			this->m_ptr = this->m_alloc->create(std::move(v));
	}

	template<typename... Args>
	void
	assign(Args&&... args)
	{
		if (!this->m_alloc)
			throw std::invalid_argument("prepare: assign called without allocator");
		if (this->m_ptr)
			**this->m_ptr = value_type(std::forward<Args>(args)...);
		else
			this->m_ptr = this->m_alloc->create(std::forward<Args>(args)...);
	}

protected:
	void
	swap(prepare_hold& o) noexcept
	{
		using std::swap;

		swap(this->m_alloc, o.m_alloc);
		swap(this->m_ptr, o.m_ptr);
	}

	typename alloc_type::alloc_pointer
	get_ptr() const noexcept
	{
		return this->m_ptr;
	}

	typename alloc_type::alloc_pointer
	release_ptr() noexcept
	{
		auto rv = this->m_ptr;
		this->m_ptr = nullptr;
		return rv;
	}


	prepare_hold(const prepare_hold&) = delete;
	prepare_hold& operator=(const prepare_hold&) = delete;
};


/*
 * Prepared push operation.
 *
 * Allows preparation of a push, with guaranteed succes on commit.
 */
template<typename MQ, typename ElemType = typename MQ::element_type>
class prepared_push :
	private MQ::in_refpointer,
	private prepare_hold<typename MQ::ll_data_type, typename MQ::allocator_type>
{
private:
	typedef prepare_hold<typename MQ::ll_data_type, typename MQ::allocator_type> parent_type;
	typedef MQ msgq_type;

public:
	typedef typename msgq_type::element_type value_type;

private:
	bool m_assigned;
	typename msgq_type::insert_lock m_lck;

public:
	prepared_push() noexcept :
		MQ::in_refpointer(),
		parent_type(),
		m_assigned(false),
		m_lck()
	{
		/* Empty body. */
	}

	prepared_push(prepared_push&& pp) noexcept :
		prepared_push()
	{
		this->swap(pp);
	}

	template<typename... Args>
	prepared_push(const typename msgq_type::in_refpointer& self, Args&&... args) :
		prepared_push(),
		msgq_type::in_refpointer(&self),
		parent_type(self, std::forward<Args>(args)...),
		m_assigned(sizeof...(args) > 0),
		m_lck(self)
	{
		/* Empty body. */
	}

	prepared_push&
	operator=(prepared_push&& pp) noexcept
	{
		prepared_push(std::move(pp)).swap(*this);
		return *this;
	}

	template<typename... Args>
	void
	assign(Args&&... args)
	{
		this->parent_type::assign(std::forward<Args>(args)...);
		this->m_assigned = true;
	}

	/*
	 * Commit prepared insert.
	 *
	 * Only throws (std::runtime_error) if no value was assigned.
	 */
	void
	commit() noexcept
	{
		assert(this->m_assigned);
		assert(this->get_ptr() && this->m_lck.get_lockable());	/* Implied by above. */

		msgq_type& self = **this;
		self.push(std::move(this->m_lck), this->get_ptr());
		this->release_ptr();
	}

	void
	swap(prepared_push& pp) noexcept
	{
		using std::swap;

		this->msgq_type::in_refpointer::swap(pp);
		this->parent_type::swap(pp);
		this->m_lck.swap(pp.m_lck);
		swap(this->m_assigned, pp.m_assigned);
	}

	friend void
	swap(prepared_push& a, prepared_push& b) noexcept
	{
		a.swap(b);
	}


	prepared_push(const prepared_push&) = delete;
	prepared_push& operator=(const prepared_push&) = delete;
};


/*
 * Prepared push, specialized for void element type.
 */
template<typename MQ>
class prepared_push<MQ, void>
{
private:
	typedef MQ msgq_type;

public:
	typedef void value_type;

private:
	typename msgq_type::in_refpointer m_self;
	typename msgq_type::insert_lock m_lck;

public:
	prepared_push() noexcept :
		m_self(nullptr),
		m_lck()
	{
		/* Empty body. */
	}

	prepared_push(prepared_push&& pp) noexcept :
		prepared_push()
	{
		this->swap(pp);
	}

	prepared_push(const typename msgq_type::in_refpointer& self) :
		m_self(self),
		m_lck(*self)
	{
		/* Empty body. */
	}

	prepared_push&
	operator=(prepared_push&& pp) noexcept
	{
		prepared_push(std::move(pp)).swap(*this);
		return *this;
	}

	/* Nop operation, provided for symmetry with typed prepared push. */
	void
	assign() noexcept
	{
		return;
	}

	void
	commit() noexcept
	{
		assert(this->m_self);

		this->m_self->push(std::move(this->m_lck));
		this->m_self = nullptr;
	}

	void
	swap(prepared_push& pp) noexcept
	{
		using std::swap;

		swap(this->m_self, pp.m_self);
		swap(this->m_lck, pp.m_lck);
	}

	friend void
	swap(prepared_push& a, prepared_push& b) noexcept
	{
		a.swap(b);
	}


	prepared_push(const prepared_push&) = delete;
	prepared_push& operator=(const prepared_push&) = delete;
};


/*
 * Eliminate allocator for void type.
 */
template<typename Type, typename Alloc>
struct select_allocator
{
	typedef Alloc type;
};
template<typename Alloc>
struct select_allocator<void, Alloc>
{
	typedef void type;
};

/*
 * Select default allocator for type.
 */
template<typename Type>
struct default_allocator : public select_allocator<Type, std::allocator<Type> > {};


} /* namespace ilias::msg_queue_detail */


template<typename Type, typename Alloc = typename msg_queue_detail::default_allocator<Type>::type >
class msg_queue :
	public msg_queue_detail::msg_queue_data<Type, typename msg_queue_detail::select_allocator<Type, Alloc>::type>,
	public msg_queue_detail::msg_queue_ref
{
friend class msg_queue_detail::prepared_push<msg_queue>;

public:
	typedef msg_queue_detail::msg_queue_data<Type, typename msg_queue_detail::select_allocator<Type, Alloc>::type> parent_type;
	typedef typename parent_type::size_type size_type;
	typedef typename parent_type::opt_element_type opt_element_type;
	typedef msg_queue_detail::prepared_push<msg_queue> prepared_push;

	typedef refpointer<msg_queue, msg_queue_detail::msg_queue_ref::refman<msg_queue, REF_IN> >
	    in_refpointer;
	typedef refpointer<msg_queue, msg_queue_detail::msg_queue_ref::refman<msg_queue, REF_OUT> >
	    out_refpointer;

	msg_queue() :
		parent_type(),
		msg_queue_detail::msg_queue_ref()
	{
		/* Empty body. */
	}

	msg_queue(size_type maxsize) :
		parent_type(maxsize),
		msg_queue_detail::msg_queue_ref()
	{
		/* Empty body. */
	}

	template<typename... Args>
	void
	push(Args&&... args) noexcept(noexcept(parent_type::push(Args()...)))
	{
		this->parent_type::push(std::forward<Args>(args)...);
		/* XXX read-event */
		if (!this->full()) {
			/* XXX write-event */
		}
	}

	template<typename... Args>
	opt_element_type
	pop(Args&&... args) noexcept(noexcept(parent_type::pop(Args()...)))
	{
		auto rv = this->parent_type::pop(std::forward<Args>(args)...);
		if (!this->full()) {
			/* XXX write-event */
		}
		if (!this->empty()) {
			/* XXX read-event */
		}
		return rv;
	}
};


namespace mqtf_detail {


template<typename InType, typename OutType, typename Transform>
class msg_queue_tf_impl
{
private:
	Transform m_fn;

public:
	msg_queue_tf_impl(Transform fn) noexcept(std::is_nothrow_move_constructible<Transform>::value) :
		m_fn(fn)
	{
		/* Empty body. */
	}

	template<typename PP, typename Opt>
	void
	tf(PP& out, Opt&& v)
	{
		assert(v);
		out.assign(m_fn(v.move()));
	}
};
template<typename InType, typename Transform>
class msg_queue_tf_impl<InType, void, Transform>
{
private:
	Transform m_fn;

public:
	msg_queue_tf_impl(Transform fn) noexcept(std::is_nothrow_move_constructible<Transform>::value) :
		m_fn(fn)
	{
		/* Empty body. */
	}

	template<typename PP, typename Opt>
	void
	tf(PP& out, Opt&& v)
	{
		assert(v);
		m_fn(v.move());
	}
};
template<typename OutType, typename Transform>
class msg_queue_tf_impl<void, OutType, Transform>
{
private:
	Transform m_fn;

public:
	msg_queue_tf_impl(Transform fn) noexcept(std::is_nothrow_move_constructible<Transform>::value) :
		m_fn(fn)
	{
		/* Empty body. */
	}

	template<typename PP>
	void
	tf(PP& out, const bool& v)
	{
		assert(v);
		out.assign(m_fn());
	}
};
template<typename Transform>
class msg_queue_tf_impl<void, void, Transform>
{
private:
	Transform m_fn;

public:
	msg_queue_tf_impl(Transform fn) noexcept(std::is_nothrow_move_constructible<Transform>::value) :
		m_fn(fn)
	{
		/* Empty body. */
	}

	template<typename PP>
	void
	tf(PP& out, const bool& v)
	{
		assert(v);
		m_fn();
	}
};


/*
 * Simple transformer.
 *
 * Takes messages from MQ_in, runs a conversion routine and places the result in MQ_out.
 * Transform must be a functor that will not throw.
 */
template<typename MQ_in, typename MQ_out, typename Transform>
class msg_queue_transform :
	public workq_job,
	private msg_queue_tf_impl<typename MQ_in::element_type, typename MQ_out::element_type, Transform>
{
private:
	static const unsigned int wqj_type_mask = ~(workq_job::TYPE_ONCE | workq_job::TYPE_PERSIST);

	typename MQ_in::out_refpointer m_input;
	typename MQ_out::in_refpointer m_output;

	void
	validate() throw (std::invalid_argument)
	{
		if (!this->m_input)
			throw std::invalid_argument("msg_queue_transform: no input queue supplied");
		if (!this->m_output)
			throw std::invalid_argument("msg_queue_transform: no output queue supplied");
	}

public:
	msg_queue_transform(workq_ptr wq,
	    typename MQ_in::out_refpointer input, typename MQ_out::in_refpointer output,
	    Transform tf, unsigned int type = 0) :
		workq_job(std::move(wq), type & wqj_type_mask),
		msg_queue_tf_impl<typename MQ_in::element_type, typename MQ_out::element_type, Transform>(std::move(tf)),
		m_input(std::move(input)),
		m_output(std::move(output))
	{
		this->validate();
	}

	virtual void
	run() noexcept override
	{
		/*
		 * Deactivate self:
		 * we need to deactivate before we have a message,
		 * since otherwise we could test and fail the message,
		 * but one could become available before we deactivate.
		 *
		 * Workq is designed that deactivation is cheap and
		 * immediate re-activation is extremely cheap (essentially
		 * free while running).
		 */
		this->deactivate();

		/*
		 * Spurious activation while we were disconnected:
		 * the (still working) message queue may have invoked us.
		 */
		if (!this->m_input || !this->m_output)
			return;

		typename MQ_out::prepared_push pp = { this->m_output };
		auto v = this->m_input->pop();

		if (v) {
			this->activate();
			this->tf(pp, v);
			pp.commit();
		} else if (!this->m_input->has_input_conn()) {
			/*
			 * Release msg queues, which in turn will release
			 * this job, once they are fully unreferenced.
			 */
			this->m_input = nullptr;
			this->m_output = nullptr;
		}
	}
};


} /* namespace mqtf_detail */


template<typename MQ_in, typename MQ_out, typename Transform>
void
mqtf_transform(workq_ptr wq, const MQ_in& in_ptr, const MQ_out& out_ptr, Transform&& tf, unsigned int wqfl = 0)
{
	typedef mqtf_detail::msg_queue_transform<typename std::remove_reference<MQ_in>::type::element_type,
	    typename std::remove_reference<MQ_out>::type::element_type,
	    typename std::remove_cv<Transform>::type> impl_type;

	auto impl = new_workq_job<impl_type>(std::move(wq),
	    in_ptr, out_ptr, std::forward<Transform>(tf), wqfl);

	std::function<void()> ev = std::bind(&workq_job::activate, impl, workq_job::ACT_IMMED);
#if 0
	if (in_ptr->push_ev || out_ptr->pop_ev)
		throw std::invalid_argument("msg_queue_transform: IO must not have events");
	in_ptr->push_ev = ev;
	out_ptr->pop_ev = ev;

	if (!in_ptr->empty())
		ev->activate();
#endif
}


} /* namespace ilias */

#endif /* ILIAS_MSG_QUEUE_H */
