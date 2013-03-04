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
#ifndef ILIAS_TUPLE_H
#define ILIAS_TUPLE_H

#include <tuple>

namespace ilias {
namespace tuple_detail {

/* Implementation details, ugly, complicated... */


/*
 * Invoke a functor on a tuple, unpacking each tuple element as an argument.
 *
 * If the tuple is passed as an rvalue-ref, each of its elements will be passed
 * as an rvalue-ref.
 */
template<typename Tuple, std::size_t N = 0,
    bool Fin = (N == std::tuple_size<Tuple>::value)>
struct unpack
{
	template<typename Functor, typename... Args>
	static auto
	invoke(const Tuple& t, Functor&& f, Args&&... args)
	noexcept(
		noexcept(unpack<Tuple, N + 1>::invoke(t,
		    std::forward<Functor>(f),
		    std::forward<Args>(args)...,
		    std::get<N>(t)))) ->
	decltype(unpack<Tuple, N + 1>::invoke(t,
		    std::forward<Functor>(f),
		    std::forward<Args>(args)...,
		    std::get<N>(t)))
	{
		return unpack<Tuple, N + 1>::invoke(t,
		    std::forward<Functor>(f),
		    std::forward<Args>(args)...,
		    std::get<N>(t));
	}

	template<typename Functor, typename... Args>
	static auto
	invoke(Tuple& t, Functor&& f, Args&&... args)
	noexcept(
		noexcept(unpack<Tuple, N + 1>::invoke(std::forward<Tuple>(t),
		    std::forward<Functor>(f),
		    std::forward<Args>(args)...,
		    std::move(std::get<N>(t))))) ->
	decltype(unpack<Tuple, N + 1>::invoke(std::forward<Tuple>(t),
		    std::forward<Functor>(f),
		    std::forward<Args>(args)...,
		    std::move(std::get<N>(t))))
	{
		return unpack<Tuple, N + 1>::invoke(std::forward<Tuple>(t),
		    std::forward<Functor>(f),
		    std::forward<Args>(args)...,
		    std::get<N>(t));
	}
};
/*
 * Invoke a functor on a tuple, unpacking each tuple element as an argument.
 *
 * This is the tail section, where all arguments are expanded and the functor
 * is to be invoked.
 */
template<typename Tuple, std::size_t N>
struct unpack<Tuple, N, true>
{
	template<typename Functor, typename... Args>
	static auto
	invoke(const Tuple& t, Functor&& f, Args&&... args)
	noexcept(noexcept(f(std::forward<Args>(args)...))) ->
	decltype(f(std::forward<Args>(args)...))
	{
		return f(std::forward<Args>(args)...);
	}
};


/*
 * Helper functor, given a group of arguments, constructs a tuple with all but
 * the first argument.
 */
struct tail
{
	template<typename T0, typename... Types>
	std::tuple<Types...>
	operator()(T0&& v0, Types&... types)
	noexcept(noexcept(std::make_tuple(std::forward<Types>(types)...)))
	{
		return std::make_tuple(std::forward<Types>(types)...);
	}
};

/*
 * For a tuple containing elements Ti..., return a tuple of elements
 * where Begin <= i < End.
 *
 * The default case deals with Begin > 0.
 */
template<std::size_t Begin, std::size_t End>
struct slice
{
	template<typename T0, typename... Types>
	auto
	operator()(const T0& v0, Types&&... t)
	noexcept(noexcept(slice<Begin - 1, End - 1>()(
		    std::forward<Types>(t)...))) ->
	decltype(slice<Begin - 1, End - 1>()(std::forward<Types>(t)...))
	{
		return slice<Begin - 1, End - 1>()(std::forward<Types>(t)...);
	}
};

/*
 * Slice specialization for creating a slice starting with the first element
 * of a tuple.
 */
template<std::size_t End>
struct slice<0U, End>
{
	template<typename T0, typename... Types>
	auto
	operator()(T0&& v0, Types&&... t)
	noexcept(noexcept(std::tuple_cat(
		    std::make_tuple(std::forward<T0>(v0)),
		    slice<0U, End - 1>()(std::forward<Types>(t)...)))) ->
	decltype(std::tuple_cat(
		    std::make_tuple(std::forward<T0>(v0)),
		    slice<0U, End - 1>()(std::forward<Types>(t)...)))
	{
		return std::tuple_cat(
		    std::make_tuple(std::forward<T0>(v0)),
		    slice<0U, End - 1>()(std::forward<Types>(t)...));
	}
};

/*
 * Slice specialization for creating an empty slice of a tuple.
 * This is the tail-guard of the slice algorithm.
 */
template<>
struct slice<0U, 0U>
{
	template<typename... Types>
	std::tuple<>
	operator()(Types&&...)
	{
		return std::tuple<>();
	}
};

/*
 * Invoke templated functor for each argument.
 */
template<typename Functor>
struct visit_args
{
	Functor& functor;

	visit_args(Functor& functor)
	:	functor(functor)
	{
		/* Empty body. */
	}

	void
	operator()() const noexcept
	{
		/* Empty body. */
	}

	template<typename T0, typename... T>
	void
	operator()(T0&& v0, T&&... v)
	noexcept(noexcept(functor(std::forward<T0>(v0))) &&
		noexcept((*this)(std::forward<T>(v)...)))
	{
		functor(std::forward<T0>(v0));
		(*this)(std::forward<T>(v)...);
	}
};



} /* namespace ilias::tuple_detail */


/* Unpack arguments to functor. */
template<typename Functor, typename... Types>
auto
unpack(const std::tuple<Types...>& t, Functor f)
noexcept(noexcept(tuple_detail::unpack<std::tuple<Types...>>::invoke(t,
	    std::move(f)))) ->
decltype(tuple_detail::unpack<std::tuple<Types...>>::invoke(t,
	    std::move(f)))
{
	return tuple_detail::unpack<std::tuple<Types...>>::invoke(t,
	    std::move(f));
}

/* Unpack arguments to functor. */
template<typename Functor, typename... Types>
auto
unpack(std::tuple<Types...>& t, Functor f)
noexcept(noexcept(tuple_detail::unpack<std::tuple<Types...>>::invoke(t,
	    std::move(f)))) ->
decltype(tuple_detail::unpack<std::tuple<Types...>>::invoke(t,
	    std::move(f)))
{
	return tuple_detail::unpack<std::tuple<Types...>>::invoke(t,
	    std::move(f));
}

/* Unpack arguments to functor. */
template<typename Functor, typename... Types>
auto
unpack(std::tuple<Types...>&& t, Functor f)
noexcept(noexcept(tuple_detail::unpack<std::tuple<Types...>>::invoke(
	    std::move(t),
	    std::move(f)))) ->
decltype(tuple_detail::unpack<std::tuple<Types...>>::invoke(
	    std::move(t),
	    std::move(f)))
{
	return tuple_detail::unpack<std::tuple<Types...>>::invoke(std::move(t),
	    std::move(f));
}


/*
 * For a tuple<T0, T1, ..., Tn> return the tuple<T1, ... Tn>.
 *
 * Note that this function is undefined for an empty tuple.
 */
template<typename T0, typename... Types>
std::tuple<Types...>
tail(const std::tuple<T0, Types...>& t)
noexcept(noexcept(unpack(t, tuple_detail::tail())))
{
	return unpack(t, tuple_detail::tail());
}

/*
 * For a tuple<T0, T1, ..., Tn> return the tuple<T1, ... Tn>.
 *
 * Note that this function is undefined for an empty tuple.
 */
template<typename T0, typename... Types>
std::tuple<Types...>
tail(std::tuple<T0, Types...>&& t)
noexcept(noexcept(unpack(std::move(t), tuple_detail::tail())))
{
	return unpack(std::move(t), tuple_detail::tail());
}


/* Return a slice of a tuple. */
template<std::size_t Begin, std::size_t End, typename... Types>
auto
slice(const std::tuple<Types...>& t)
noexcept(noexcept(unpack(t, tuple_detail::slice<Begin, End>()))) ->
decltype(unpack(t, tuple_detail::slice<Begin, End>()))
{
	static_assert(Begin <= End, "Slice end before slice begin.");
	static_assert(End <= sizeof...(Types),
	    "Slice end exceeds tuple size.");

	return unpack(t, tuple_detail::slice<Begin, End>());
}

/* Return a slice of a tuple (specialization for move semantics). */
template<std::size_t Begin, std::size_t End, typename... Types>
auto
slice(std::tuple<Types...>&& t)
noexcept(noexcept(unpack(std::move(t), tuple_detail::slice<Begin, End>()))) ->
decltype(unpack(std::move(t), tuple_detail::slice<Begin, End>()))
{
	static_assert(Begin <= End, "Slice end before slice begin.");
	static_assert(End <= sizeof...(Types),
	    "Slice end exceeds tuple size.");

	return unpack(std::move(t), tuple_detail::slice<Begin, End>());
}


/*
 * Visitor pattern: the functor is invoked for each element in the tuple.
 */
template<typename Functor, typename... Types>
Functor
visit(const std::tuple<Types...>& t, Functor f)
noexcept(noexcept(unpack(t, tuple_detail::visit_args<Functor>(f))))
{
	unpack(t, tuple_detail::visit_args<Functor>(f));
	return f;
}

/*
 * Visitor pattern: the functor is invoked for each element in the tuple.
 */
template<typename Functor, typename... Types>
Functor
visit(std::tuple<Types...>& t, Functor f)
noexcept(noexcept(unpack(t, tuple_detail::visit_args<Functor>(f))))
{
	unpack(t, tuple_detail::visit_args<Functor>(f));
	return f;
}

/*
 * Visitor pattern: the functor is invoked for each element in the tuple.
 */
template<typename Functor, typename... Types>
Functor
visit(std::tuple<Types...>&& t, Functor f)
noexcept(noexcept(unpack(std::move(t), tuple_detail::visit_args<Functor>(f))))
{
	unpack(std::move(t), tuple_detail::visit_args<Functor>(f));
	return f;
}


} /* namespace ilias */

#endif /* ILIAS_TUPLE_H */
