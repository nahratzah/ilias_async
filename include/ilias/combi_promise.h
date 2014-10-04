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
#ifndef ILIAS_COMBI_PROMISE_H
#define ILIAS_COMBI_PROMISE_H

#include <ilias/promise.h>
#include <ilias/workq.h>
#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ilias {
namespace cprom_detail {


/* Visit each element, starting at the given index. */
template<typename Futures, std::size_t IDX = std::tuple_size<Futures>::value>
struct visitor
{
	template<typename Functor>
	static Functor
	action(Futures& f, Functor&& fn)
	{
		Functor fn_after = visitor<Futures, IDX - 1>::action(f,
		    std::forward<Functor>(fn));
		fn_after(std::get<IDX - 1>(f));
		return fn_after;
	}
};

/* Visitor termination condition: empty set needs to be checked. */
template<typename Futures>
struct visitor<Futures, 0U>
{
	template<typename Functor>
	static Functor
	action(Futures&, Functor&& fn)
	{
		return std::forward<Functor>(fn);
	}
};

/* Implementation functor for check_initialized. */
struct check_initialized_impl {
	check_initialized_impl() = default;
	check_initialized_impl(const check_initialized_impl&) = delete;
	check_initialized_impl(check_initialized_impl&&) = default;

	bool rv{ true };

	template<typename T>
	void
	operator()(const future<T>& f) noexcept
	{
		this->rv = this->rv && f.is_initialized();
	}
};

/* Visit each element. */
template<typename Futures, typename Functor>
Functor
visit(Futures& f, Functor&& fn)
{
	return visitor<Futures>::action(f, std::forward<Functor>(fn));
}

/* Check if all futures/promises in the tuple are initialized. */
template<typename Futures>
bool
check_initialized(const Futures& f) noexcept
{
	return visit(f, check_initialized_impl()).rv;
}

/* Functor, starts the argument promise/future. */
class lambda_start
{
public:
	lambda_start() = default;
	lambda_start(const lambda_start&) = default;

	template<typename T>
	void
	operator()(T& f) const noexcept
	{
		f.start();
	}
};


/*
 * Base combiner type.
 * Keeps track of the state of all involved futures and the resultant promise.
 *
 * Calls the callback function once:
 * - all futures are ready
 * - the promise has been started
 */
class base_combiner
:	public std::enable_shared_from_this<base_combiner>
{
protected:
	typedef void (*function)(base_combiner&);

private:
	std::atomic<std::size_t> n_defer;
	std::shared_ptr<void> m_self;
	function m_fn;

public:
	base_combiner() = delete;
	base_combiner(const base_combiner&) = delete;
	base_combiner(base_combiner&&) = delete;
	base_combiner& operator=(const base_combiner&) = delete;

protected:
	base_combiner(std::size_t defer, function fn) noexcept
	:	n_defer(defer + 1),
		m_fn(fn)
	{
		/* Empty body. */
	}

	ILIAS_ASYNC_EXPORT std::shared_ptr<void> complete() noexcept;
	ILIAS_ASYNC_EXPORT std::shared_ptr<void> notify() noexcept;
	ILIAS_ASYNC_EXPORT void enable() noexcept;

	/*
	 * Visitor.
	 * Creates callbacks for each future.
	 */
	struct callback_impl
	{
	private:
		std::weak_ptr<base_combiner> m_self;

	public:
		callback_impl(std::weak_ptr<base_combiner> self) noexcept
		:	m_self(std::move(self))
		{
			/* Empty body. */
		}

		template<typename T>
		void
		operator()(future<T>& f)
		{
			std::weak_ptr<base_combiner> self = this->m_self;
			callback(f, [self](future<T> /*f*/) {
				auto ptr = self.lock();
				if (ptr)
					ptr->notify();
			    }, PROM_DEFER);
		}
	};
};

template<typename Type, typename FN, typename Futures>
class combiner;
template<typename Type, typename FN, typename Futures>
class wq_combiner;

/*
 * Combiner implementation.
 *
 * Functor is run once all futures are ready and the promise is started.
 */
template<typename Type, typename FN, typename... Futures>
class combiner<Type, FN, std::tuple<future<Futures>...>>
:	public base_combiner
{
public:
	typedef FN fn_type;

private:
	std::tuple<future<Futures>...> m_futures;
	promise<Type> m_prom;
	fn_type m_fn;

	template<size_t... Idx>
	static void
	run_invoke(fn_type& fn, promise<Type> prom,
	    std::tuple<future<Futures>...>&& futures,
	    std::index_sequence<Idx...>) {
		assert(prom.is_initialized());
		fn(std::move(prom), std::get<Idx>(std::move(futures))...);
	}

	void
	run() noexcept
	{
		try {
			run_invoke(this->m_fn, this->m_prom,
			    std::move(this->m_futures),
			    std::index_sequence_for<Futures...>());
		} catch (...) {
			this->m_prom.set_exception(std::current_exception());
		}
	}

	static void
	run(base_combiner& bc) noexcept
	{
		static_cast<combiner&>(bc).run();
	}

public:
	template<typename Init>
	combiner(fn_type fn, Init&& futures)
	:	base_combiner(sizeof...(Futures), &combiner::run),
		m_futures(std::forward<Init>(futures)),
		m_fn(std::move(fn))
	{
		if (!check_initialized(this->m_futures)) {
			throw std::invalid_argument("promise combiner "
			    "requires initialzied futures");
		}
	}

	template<typename Init>
	static std::shared_ptr<combiner>
	new_combiner(fn_type fn, Init&& futures)
	{
		std::shared_ptr<combiner> c = std::make_shared<combiner>(
		    std::move(fn), std::forward<Init>(futures));
		visit(c->m_futures, callback_impl(c));
		return c;
	}

	void
	operator()(promise<Type> p) noexcept
	{
		visit(this->m_futures, lambda_start{});
		this->m_prom = std::move(p);
		this->enable();
	}
};

/*
 * Combiner implementation.
 *
 * Invoked function will run as a workq_job (i.e. on its dedicated workq,
 * according to its options).
 */
template<typename Type, typename FN, typename... Futures>
class wq_combiner<Type, FN, std::tuple<future<Futures>...>>
:	public base_combiner,
	public workq_job
{
public:
	typedef FN fn_type;

private:
	std::tuple<future<Futures>...> m_futures;
	promise<Type> m_prom;
	fn_type m_fn;

	virtual void
	run() noexcept override
	{
		assert(this->m_prom.is_initialized());

		/* Move promise out of functor. */
		promise<Type> p = std::move(this->m_prom);
		promise<Type> q = p;

		/*
		 * Run function;
		 * if it fails, assign its exception to the promise.
		 */
		try {
			this->m_fn(std::move(p),
			    std::move(this->m_futures));
		} catch (...) {
			q.set_exception(std::current_exception());
		}
	}

	static void
	run(base_combiner& bc) noexcept
	{
		static_cast<wq_combiner&>(bc).activate();
	}

public:
	template<typename Init>
	wq_combiner(workq_ptr wq, fn_type fn, Init&& futures, unsigned int fl)
	:	base_combiner(sizeof...(Futures), &wq_combiner::run),
		workq_job(std::move(wq), fl | workq_job::TYPE_ONCE),
		m_futures(std::forward<Init>(futures)),
		m_fn(std::move(fn))
	{
		if (!check_initialized(this->m_futures)) {
			throw std::invalid_argument("promise combiner "
			    "requires initialzied futures");
		}
		if (fl & TYPE_PERSIST) {
			throw std::invalid_argument("promise combiner "
			    "cannot be a persistant workq job");
		}
	}

	template<typename Init>
	static std::shared_ptr<wq_combiner>
	new_combiner(workq_ptr wq, fn_type fn, Init&& futures,
	    unsigned int fl = 0)
	{
		std::shared_ptr<wq_combiner> c = new_workq_job<wq_combiner>(
		    std::move(wq), std::move(fn),
		    std::forward<Init>(futures), fl);
		visit(c->m_futures, callback_impl(c));
		return c;
	}

	void
	operator()(promise<Type> p) noexcept
	{
		visit(this->m_futures, lambda_start{});
		this->m_prom = std::move(p);
		this->enable();
	}
};

template<typename Type, typename Future>
struct resolve
{
	void
	operator()(promise<Type> p, std::tuple<future<Future>> f)
	{
		p.set(std::get<0>(f).get());
	}
};

template<typename Future>
struct resolve<void, Future>
{
	void
	operator()(promise<void> p, std::tuple<future<Future>> f)
	{
		std::get<0>(f).get();
		p.set();
	}
};


template<typename Arg, size_t FutCount, bool = is_future<Arg>::value>
struct replacer {
	static constexpr size_t future_count = FutCount;

	template<typename T>
	static auto handle(T&& v) -> decltype(std::forward<T>(v))
	{
		return std::forward<T>(v);
	}

	template<typename T>
	static auto future_tuple(T&&) -> std::tuple<> {
		return std::tuple<>();
	}
};

template<size_t Idx> struct bound_future {};

template<typename Arg, size_t FutCount>
struct replacer<Arg, FutCount, true> {
	static constexpr size_t future_count = FutCount + 1U;

	template<typename T>
	static auto handle(T&&) ->
		decltype(std::bind(&Arg::get, bound_future<FutCount>()))
	{
		return std::bind(&Arg::get, bound_future<FutCount>());
	}

	template<typename T>
	static auto future_tuple(T&& v) ->
	    decltype(std::make_tuple(std::forward<T>(v))) {
		return std::make_tuple(std::forward<T>(v));
	}
};

template<size_t, typename...> struct arg_or_fut_placeholder;

template<size_t FutCount>
struct arg_or_fut_placeholder<FutCount> {
};

template<size_t FutCount, typename Arg0, typename... Args>
struct arg_or_fut_placeholder<FutCount, Arg0, Args...> {
	using replacer_type = replacer<Arg0, FutCount>;
	using successor_type =
	    arg_or_fut_placeholder<replacer_type::future_count, Args...>;
};

template<size_t, typename> struct replacer_for;

template<size_t Idx, size_t FutCount, typename... Args>
struct replacer_for<Idx, arg_or_fut_placeholder<FutCount, Args...>> {
	using type = typename replacer_for<Idx - 1U,
	    typename arg_or_fut_placeholder<FutCount, Args...>::successor_type>::type;
};

template<size_t FutCount, typename... Args>
struct replacer_for<size_t(0), arg_or_fut_placeholder<FutCount, Args...>> {
	using type =
	    typename arg_or_fut_placeholder<FutCount, Args...>::replacer_type;
};


template<typename T>
auto unbind(T&& v) ->
	std::enable_if_t<!std::is_bind_expression<std::remove_cv_t<std::remove_reference_t<T>>>::value, decltype(std::forward<T>(v))>
{
	return std::forward<T>(v);
}

template<typename T>
struct unbound_ {
	using v_type = std::remove_cv_t<std::remove_reference_t<T>>;

	template<typename U> unbound_(U&& v) : v_(std::forward<U>(v)) {}

	template<typename... Args>
	auto operator()(Args&&... args) -> decltype(std::declval<v_type&>()(std::forward<Args>(args)...)) {
		return v_(std::forward<Args>(args)...);
	}

	v_type v_;
};

template<typename T>
auto unbind(T&& v) ->
	std::enable_if_t<std::is_bind_expression<std::remove_cv_t<std::remove_reference_t<T>>>::value, unbound_<T>>
{
	return std::forward<T>(v);
}


template<typename Type, typename FN, typename... Args, size_t... Idx>
future<Type>
combine(FN&& fn, std::index_sequence<Idx...>, Args&&... args) {
	using arg_transform = arg_or_fut_placeholder<size_t(0),
	    std::remove_cv_t<std::remove_reference_t<Args>>...>;

	auto transformed_fn = std::bind(unbind(std::forward<FN>(fn)), std::placeholders::_1, replacer_for<Idx, arg_transform>::type::handle(std::forward<Args>(args))...);
	auto future_tuple = std::tuple_cat(replacer_for<Idx, arg_transform>::type::future_tuple(std::forward<Args>(args))...);

	using combi =
	    combiner<Type, decltype(transformed_fn), decltype(future_tuple)>;

	return new_promise<Type>(std::bind(&combi::operator(),
	    combi::new_combiner(std::move(transformed_fn), std::move(future_tuple)),
	    std::placeholders::_1));
}

template<typename Type, typename FN, typename... Args, size_t... Idx>
future<Type>
combine(workq_ptr wq, unsigned int fl,
    FN&& fn, std::index_sequence<Idx...>, Args&&... args) {
	using arg_transform = arg_or_fut_placeholder<size_t(0),
	    std::remove_cv_t<std::remove_reference_t<Args>>...>;

	auto transformed_fn = std::bind(unbind(std::forward<FN>(fn)), std::placeholders::_1, replacer_for<Idx, arg_transform>::type::handle(std::forward<Args>(args))...);
	auto future_tuple = std::tuple_cat(replacer_for<Idx, arg_transform>::type::future_tuple(std::forward<Args>(args))...);

	using combi =
	    wq_combiner<Type, decltype(transformed_fn), decltype(future_tuple)>;

	return new_promise<Type>(std::bind(&combi::operator(),
	    combi::new_combiner(std::move(wq), std::move(transformed_fn), std::move(future_tuple), fl),
	    std::placeholders::_1));
}


} /* namespace ilias::cprom_detail */


/*
 * Create a promise that fires once all its futures are complete.
 *
 * Function prototype must be such that it will accept resolved futures.
 *
 * Starting this promise will also start all its dependant promises.
 *
 * Because the created promise runs asynchronous, it is ill advised to
 * wait() on the promise.
 */
template<typename Type, typename FN, typename... Args>
future<Type>
combine(FN&& fn, Args&&... args) {
	return cprom_detail::combine<Type>(std::forward<FN>(fn),
	    std::index_sequence_for<Args...>(),
	    std::forward<Args>(args)...);
}

/*
 * Create a promise that combines the result of multiple futures.
 * The functor combining the futures will run as a workq job.
 *
 * The functor prototype must be:
 * void (promise<Type>, std::tuple<std::future<...>, ...>);
 *
 * Starting this promise will also start all its dependant promises.
 *
 * Because the created promise runs asynchronous, it is ill advised to
 * wait() on the promise.
 */
template<typename Type, typename FN, typename... Args>
future<Type>
combine(workq_ptr wq, unsigned int fl,
    FN&& fn, Args&&... args)
{
	return cprom_detail::combine<Type>(std::move(wq), fl,
	    std::forward<FN>(fn),
	    std::index_sequence_for<Args...>(),
	    std::forward<Args>(args)...);
}


#if 0  // Old code.
/*
 * Create a promise that combines the result of multiple futures.
 *
 * The functor prototype must be:
 * void (promise<Type>, std::tuple<std::future<...>, ...>);
 *
 * Starting this promise will also start all its dependant promises.
 *
 * Because the created promise runs asynchronous, it is ill advised to
 * wait() on the promise.
 */
template<typename Type, typename FN, typename... Futures>
future<Type>
combine(FN&& fn, std::tuple<future<Futures>...> f)
{
	using namespace std::placeholders;
	typedef cprom_detail::combiner<Type, typename std::decay<FN>::type,
	    std::tuple<future<Futures>...>>
	    combi;

	return new_promise<Type>(std::bind(&combi::operator(),
	    combi::new_combiner(std::forward<FN>(fn), std::move(f)),
	    _1));
}

/*
 * Create a promise that combines the result of multiple futures.
 *
 * The functor prototype must be:
 * void (promise<Type>, std::tuple<std::future<...>, ...>);
 *
 * Starting this promise will also start all its dependant promises.
 *
 * Because the created promise runs asynchronous, it is ill advised to
 * wait() on the promise.
 */
template<typename Type, typename FN, typename... Futures>
future<Type>
combine(FN&& fn, future<Futures>... f)
{
	return combine<Type>(std::forward<FN>(fn),
	    std::make_tuple(std::forward<future<Futures>>(f)...));
}


/*
 * Create a promise that combines the result of multiple futures.
 * The functor combining the futures will run as a workq job.
 *
 * The functor prototype must be:
 * void (promise<Type>, std::tuple<std::future<...>, ...>);
 *
 * Starting this promise will also start all its dependant promises.
 *
 * Because the created promise runs asynchronous, it is ill advised to
 * wait() on the promise.
 */
template<typename Type, typename FN, typename... Futures>
future<Type>
combine(workq_ptr wq, unsigned int fl,
    FN&& fn, std::tuple<future<Futures>...> f)
{
	using namespace std::placeholders;
	typedef cprom_detail::wq_combiner<Type, typename std::decay<FN>::type,
	    std::tuple<future<Futures>...>>
	    combi;

	return new_promise<Type>(std::bind(&combi::operator(),
	    combi::new_combiner(std::move(wq), std::forward<FN>(fn),
	      std::move(f), fl),
	    _1));
}
#endif // 0
template<typename Type, typename FN, typename... Futures>
future<Type>
combine(workq_ptr wq, FN&& fn, std::tuple<future<Futures>...> f)
{
	return combine<Type>(wq, 0U, fn, std::move(f));
}

/*
 * Create a promise that combines the result of multiple futures.
 * The functor combining the futures will run as a workq job.
 *
 * The functor prototype must be:
 * void (promise<Type>, std::tuple<std::future<...>, ...>);
 *
 * Starting this promise will also start all its dependant promises.
 *
 * Because the created promise runs asynchronous, it is ill advised to
 * wait() on the promise.
 */
template<typename Type, typename FN, typename... Futures>
future<Type>
combine(workq_ptr wq, unsigned int fl,
    FN&& fn, future<Futures>... f)
{
	return combine<Type>(wq, fl, std::forward<FN>(fn),
	    std::make_tuple(std::forward<future<Futures>>(f)...));
}
template<typename Type, typename FN, typename... Futures>
future<Type>
combine(workq_ptr wq, FN&& fn, future<Futures>... f)
{
	return combine<Type>(wq, 0U, fn, std::move(f)...);
}


/*
 * Construct a combine promise that passes on the input type.
 * (Useful for implicit conversions.)
 */
template<typename Type, typename Future>
future<Type>
passthrough(future<Future> f)
{
	typedef promise<Type> prom;
	typedef future<Future> fut;
	typedef std::tuple<fut> arg_type;

	return combine<Type>(cprom_detail::resolve<Type, Future>(),
	    f);
}

/*
 * Passthrough unmodified.
 *
 * This function skips the combine operation.
 * (Useful in templates.)
 */
template<typename Type>
inline future<Type>
passthrough(future<Type> f) noexcept
{
	return f;
}


} /* namespace ilias */


/* Enable placeholder semantics for bound futures. */
namespace std {

template<size_t FutCount>
struct is_placeholder<ilias::cprom_detail::arg_or_fut_placeholder<FutCount>>
: integral_constant<int, FutCount + 2U> {};

} /* namespace std */


#endif /* ILIAS_COMBI_PROMISE_H */
