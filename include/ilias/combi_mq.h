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
#include <ilias/ilias_async_export.h>
#include <ilias/msg_queue.h>
#include <ilias/tuple.h>
#include <functional>
#include <mutex>
#include <tuple>
#include <utility>

namespace ilias {
namespace combiner_detail {


/*
 * Optional value, used by combiner.
 *
 * It is not a pointer, but does implement the behaviour of one.
 */
template<typename Type>
class combiner_opt
{
public:
	using element_type = Type;
	using pointer = element_type*;
	using const_pointer = const element_type*;
	using reference = element_type&;
	using const_reference = const element_type&;

private:
	union impl
	{
		element_type v;

		~impl() noexcept {}
	};

	bool m_isset{ false };
	impl m_impl{};

	static constexpr bool noexcept_copy =
	    std::is_nothrow_copy_constructible<element_type>::value;
	static constexpr bool noexcept_move =
	    std::is_nothrow_move_constructible<element_type>::value;
	static constexpr bool noexcept_move_assign =
	    std::is_nothrow_move_assignable<element_type>::value;
	static constexpr bool noexcept_destroy =
	    std::is_nothrow_destructible<element_type>::value;
	static constexpr bool moveable =
	    std::is_move_constructible<element_type>::value;
	static constexpr bool noexcept_swap =
	    (noexcept_move || noexcept_copy) && noexcept_destroy;

public:
	explicit operator bool() const noexcept
	{
		return this->m_isset;
	}

	void
	assign(element_type v) noexcept(noexcept_move && noexcept_move_assign)
	{
		if (this->m_isset)
			this->m_impl.v = std::move(v);
		else {
			new (&this->m_impl.v) element_type(std::move(v));
			this->m_isset = true;
		}
	}

	pointer
	get() noexcept
	{
		return (*this ? &this->m_impl.v : nullptr);
	}

	const_pointer
	get() const noexcept
	{
		return (*this ? &this->m_impl.v : nullptr);
	}

	pointer
	operator-> () noexcept
	{
		return this->get();
	}

	const_pointer
	operator-> () const noexcept
	{
		return this->get();
	}

	reference
	operator* () noexcept
	{
		return *this->get();
	}

	const_reference
	operator* () const noexcept
	{
		return *this->get();
	}

	void
	reset() noexcept(noexcept_destroy)
	{
		if (this->m_isset) {
			this->m_isset = false;
			this->m_impl.v.~element_type();
		}
	}

	friend void
	swap(combiner_opt& p, combiner_opt& q) noexcept(noexcept_swap)
	{
		if (p && q) {
			swap(p.m_impl.v, q.m_impl.v);
			return;
		}

		if (p) {
			new (&q.m_impl.v) element_type(
			    std::move_if_noexcept(p.m_impl.v));
			q.m_isset = true;
			p.reset();
			return;
		}

		if (q) {
			new (&p.m_impl.v) element_type(
			    std::move_if_noexcept(q.m_impl.v));
			p.m_isset = true;
			q.reset();
			return;
		}
	}

	combiner_opt() = default;

	/* Destructor. */
	~combiner_opt()
	    noexcept(std::is_nothrow_destructible<element_type>::value)
	{
		this->reset();
	}

	/* Copy constructor. */
	combiner_opt(const combiner_opt& o) noexcept(noexcept_copy)
	{
		if (o.m_isset) {
			new (&this->m_impl.v) element_type(o.m_impl.v);
			this->m_isset = true;
		}
	}

	/*
	 * Implement move constructor
	 * iff element_type is move constructible.
	 */
	combiner_opt(const typename std::enable_if<moveable,
	    combiner_opt&>::type o)
	noexcept(noexcept_move && noexcept_destroy)
	{
		if (o.m_isset) {
			new (&this->m_impl.v) element_type(
			    std::move(o.m_impl.v));
			this->m_isset = true;

			o.reset();
		}
	}

	combiner_opt&
	operator= (combiner_opt o) noexcept(noexcept_swap)
	{
		swap(*this, o);
		return *this;
	}

	bool
	operator== (const combiner_opt& o) const
	    noexcept(noexcept(m_impl.v == o.m_impl.v))
	{
		return (!*this || !o ?
		    *this == o :
		    this->impl.v == o.m_impl.v);
	}

	bool
	operator!= (const combiner_opt& o) const
	    noexcept(noexcept(m_impl.v != o.m_impl.v))
	{
		return (!*this || !o ?
		    *this != o :
		    this->impl.v != o.m_impl.v);
	}
};


/*
 * Optional combiner value, specialization for void.
 */
template<>
class combiner_opt<void>
{
public:
	using element_type = void;

private:
	bool m_isset{ false };

public:
	explicit operator bool() const noexcept
	{
		return this->m_isset;
	}

	void
	assign() noexcept
	{
		this->m_isset = true;
	}

	void
	reset() noexcept
	{
		this->m_isset = false;
	}

	friend void
	swap(combiner_opt& p, combiner_opt& q) noexcept
	{
		using std::swap;

		swap(p.m_isset, q.m_isset);
	}

	combiner_opt() = default;
	combiner_opt(const combiner_opt&) = default;

	combiner_opt(combiner_opt&& o) noexcept
	{
		using std::swap;

		swap(*this, o);
	}

	combiner_opt&
	operator= (const combiner_opt& o) noexcept
	{
		this->m_isset = o.m_isset;
		return *this;
	}

	combiner_opt&
	operator= (combiner_opt&& o) noexcept
	{
		this->m_isset = o.m_isset;
		o.m_isset = false;
		return *this;
	}

	bool
	operator== (const combiner_opt& o) const noexcept
	{
		return this->m_isset == o.m_isset;
	}

	bool
	operator!= (const combiner_opt& o) const noexcept
	{
		return this->m_isset != o.m_isset;
	}
};


/*
 * Return a tuple with the combiner element.
 * Overloaded to support destructible combiner_opt,
 * const combiner_opt and specialization for combiner_opt<void>.
 */
template<typename T>
auto
combi_opt_as_tuple(combiner_opt<T>&& v)
noexcept((std::make_tuple(v.move()))) ->
decltype(std::make_tuple(v.move()))
{
	return std::make_tuple(v.move());
}
template<typename T>
auto
combi_opt_as_tuple(const combiner_opt<T>& v)
noexcept(noexcept(std::make_tuple(*v))) ->
decltype(std::make_tuple(*v))
{
	return std::make_tuple(*v);
}
std::tuple<>
combi_opt_as_tuple(const combiner_opt<void>& v) noexcept
{
	return std::make_tuple();
}
std::tuple<>
combi_opt_as_tuple(combiner_opt<void>& v) noexcept
{
	v.reset();
	return std::make_tuple();
}


/*
 * Support functor for combi_opt_as_tuple:
 * invokes combi_opt_as_tuple on each element and returns the result.
 */
struct combi_opt_as_tuple_support
{
	template<typename... MQ, typename... T>
	auto
	operator()(std::tuple<MQ, T>&&... v) const
	noexcept(noexcept(std::tuple_cat(
		    combi_opt_as_tuple(std::move(std::get<1>(v)))...))) ->
	decltype(std::tuple_cat(
		    combi_opt_as_tuple(std::move(std::get<1>(v)))...))
	{
		return std::tuple_cat(
		    combi_opt_as_tuple(std::move(std::get<1>(v)))...);
	}

	template<typename... MQ, typename... T>
	auto
	operator()(const std::tuple<MQ, T>&... v) const
	noexcept(noexcept(std::tuple_cat(
		    combi_opt_as_tuple(std::get<1>(v))...))) ->
	decltype(std::tuple_cat(
		    combi_opt_as_tuple(std::get<1>(v))...))
	{
		return std::tuple_cat(
		    combi_opt_as_tuple(std::get<1>(v))...);
	}
};


/* Convert a tuple of combiner_opt elements to their deferenced values. */
template<typename... MQ, typename... Types>
auto
combi_opt_as_tuple(std::tuple<std::tuple<MQ, combiner_opt<Types>>...>&& t)
noexcept(noexcept(unpack(std::move(t), combi_opt_as_tuple_support()))) ->
decltype(unpack(std::move(t), combi_opt_as_tuple_support()))
{
	return unpack(std::move(t), combi_opt_as_tuple_support());
}
/* Convert a tuple of combiner_opt elements to their deferenced values. */
template<typename... MQ, typename... Types>
auto
combi_opt_as_tuple(const std::tuple<std::tuple<MQ, combiner_opt<Types>>...>& t)
noexcept(noexcept(unpack(t, combi_opt_as_tuple_support()))) ->
decltype(unpack(t, combi_opt_as_tuple_support()))
{
	return unpack(t, combi_opt_as_tuple_support());
}


/*
 * Test if all optional values in the tuple are set.
 */
template<typename Tuple>
bool
ready(const Tuple& t) noexcept
{
	struct impl
	{
		bool rv{ true };

		template<typename MQ, typename T>
		void
		operator()(const std::tuple<MQ, combiner_opt<T>>& v) const
		noexcept
		{
			rv = rv && std::get<1>(v);
		}
	};

	return visit(t, impl()).rv;
}


/*
 * Test if all optional values in the tuple are set.
 */
template<typename Tuple>
bool
none(const Tuple& t) noexcept
{
	struct impl
	{
		bool rv{ false };

		template<typename MQ, typename T>
		void
		operator()(const std::tuple<MQ, combiner_opt<T>>& v) const
		noexcept
		{
			rv = rv || std::get<1>(v);
		}
	};

	return visit(t, impl()).rv;
}


namespace {

/* Simple functor for declval declaring a no-except functor. */
template<typename T>
struct noexcept_functor
{
	void operator()(T v) const noexcept {};
};

/*
 * Simple functor for declval declaring a no-except functor
 * without arguments.
 */
template<>
struct noexcept_functor<void>
{
	void operator()() const noexcept {};
};

} /* namespace ilias::combiner_detail::<unnamed> */


/*
 * Support function for combiner.
 * Given a tuple<mq, combiner_opt>, it will fetch a value from mq and
 * assign it to combiner_opt, unless combiner_opt already holds a value.
 *
 * Functor returns true iff the element changes from unset to ready.
 */
struct fetcher
{
private:
	template<typename T, typename U>
	bool
	operator()(std::tuple<T, combiner_opt<U>>& v) const
	noexcept(
		noexcept(std::get<0>(v).dequeue(
		    std::declval<noexcept_functor<typename T::element_type>>()
		    )) &&
		noexcept(std::get<1>(v).assign(
		    std::declval<typename T::element_type&&>())) &&
		std::is_nothrow_destructible<typename T::element_type>::value)
	{
		bool rv = false;
		if (!std::get<1>(v)) {
			std::get<0>(v).dequeue(
			    [&v, &rv](typename T::element_type value) {
				std::get<1>(v).assign(std::move(value));
				rv = true;
			    }, 1);
		}
		return rv;
	}

	template<typename T>
	bool
	operator()(std::tuple<T, combiner_opt<void>>& v) const
	noexcept(
		noexcept(std::get<0>(v).dequeue(
		    std::declval<noexcept_functor<void>>())) &&
		noexcept(std::get<1>(v).assign()))
	{
		bool rv = false;
		if (!std::get<1>(v)) {
			std::get<0>(v).dequeue([&v, &rv]() {
				std::get<1>(v).assign();
				rv = true;
			    }, 1);
		}
		return rv;
	}
};


/* Clear output callbacks in tuple. */
struct clear_output_callback
{
	template<typename MQ, typename... U>
	void
	operator()(std::tuple<MQ, U...>& mq_record) const noexcept
	{
		output_callback(std::get<0>(mq_record), nullptr);
	}
};


} /* namespace ilias::combiner_detail */


template<typename... MQ>
class combiner
:	public msg_queue_events
{
static_assert(sizeof...(MQ) > 0,
    "Cannot combine messages without input queues.");

private:
	using opts = std::tuple<std::tuple<MQ&,
	    combiner_detail::combiner_opt<typename MQ::element_type>>...>;

public:
	using element_type = decltype(
	    combiner_detail::combi_opt_as_tuple(std::declval<opts>()));

private:
	std::mutex m_mtx;
	opts m_opts;

	/* Attempt to fetch new values for m_opts. */
	void
	_try_fetch()
	noexcept(noexcept(visit(this->m_opts, combiner_detail::fetcher())))
	{
		visit(this->m_opts, combiner_detail::fetcher());
	}

	/* Test if the combiner is ready for reading. */
	bool
	_empty() const noexcept
	{
		return !combiner_detail::ready(this->m_opts);
	}

	/* Internal dequeue logic. */
	template<typename Functor>
	Functor
	_dequeue(std::unique_lock<std::mutex>& lck, Functor f, std::size_t n)
	noexcept(
		noexcept(combi_opt_as_tuple(std::move(this->m_opts))) &&
		noexcept(f(*(element_type)nullptr)))
	{
		while (n > 0 && !this->_empty()) {
			auto argument =
			    combi_opt_as_tuple(std::move(this->m_opts));
			assert(combiner_detail::none(this->m_opts));

			/* Fetch new value prior to unlock. */
			this->_try_fetch();

			lck.unlock();
			f(argument);
			lck.lock();

			--n;
		}
		return f;
	}

public:
	combiner(MQ&... mq)
	:	m_opts{ std::tuple<MQ&,
		    combiner_detail::combiner_opt<typename MQ::element_type>>(
		    mq, combiner_detail::combiner_opt<
		      typename MQ::element_type>())... }
	{
		/*
		 * Unpack functor for tuple.
		 *
		 * Uses prepare-commit semantics to install callbacks on each
		 * element.
		 */
		struct install_callback
		{
			combiner& self;

			install_callback(combiner& self) noexcept
			:	self(self)
			{
				/* Empty body. */
			}

			void
			operator()() const noexcept
			{
				/* Empty body. */
			}

			template<typename T0, typename... Types>
			void
			operator()(T0& v0, Types&&... tail) const
			{
				using combiner_detail::fetcher;
				using combiner_detail::ready;

				/*
				 * Prepare stage:
				 * Create functor callback
				 * for message queue in v0.
				 */
				combiner* self = &this->self;
				T0* element = &v0;
				std::function<void()> callback{
				    [self, element]() {
					bool fire = false;
					{
						std::lock_guard<std::mutex>
						    guard{ self->m_mtx };
						fire = fetcher()(*element) &&
						    ready(self->m_opts);
					}
					if (fire)
						self->_fire_output();
				    }};

				/* Recurse into tail. */
				(*this)(std::forward<Types>(tail)...);

				/*
				 * Commit stage:
				 * Install callback (this is a no-except
				 * function).
				 */
				output_callback(std::get<0>(v0),
				    std::move(callback));
			}
		};


		/*
		 * This combiner is currently empty
		 * (installed callbacks may change this state).
		 */
		this->_fire_empty();

		/*
		 * Install events on each mq.
		 *
		 * The functor for installing callbacks is using prepare-commit
		 * staging to ensure no callbacks are installed if not all can
		 * be allocated.
		 *
		 * The output callbacks will fire immediately.
		 */
		unpack(this->m_opts, install_callback(*this));
	}

	combiner(const combiner&) = delete;
	combiner(combiner&&) = delete;
	combiner& operator=(const combiner&) = delete;

	~combiner() noexcept
	{
		this->clear_events();
		visit(this->m_opts, combiner_detail::clear_output_callback());
	}

	/* Test if the combiner is empty. */
	bool
	empty() const noexcept
	{
		std::lock_guard<std::mutex> lck(this->m_mtx);
		return _empty();
	}

	template<typename Functor>
	Functor
	dequeue(Functor&& f, std::size_t n = 1)
	noexcept(
		noexcept(this->_dequeue(
		    std::declval<std::unique_lock<std::mutex>>(),
		    std::forward<Functor>(f), n)))
	{
		std::unique_lock<std::mutex> lck(this->m_mtx);
		auto rv = this->_dequeue(lck, std::forward<Functor>(f), n);

		/* Fire empty event with race-condition resolution. */
		const bool fire = this->_empty();
		lck.unlock();
		if (fire) {
			_fire_empty();
			if (!this->empty())
				_fire_output();
		}
		return rv;
	}
};


} /* namespace ilias */
