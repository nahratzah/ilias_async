#ifndef ILIAS_WQ_SORT_H
#define ILIAS_WQ_SORT_H

#include <ilias/workq.h>
#include <ilias/promise.h>
#include <ilias/wq_promise.h>
#include <ilias/combi_promise.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <random>
#include <vector>
#include <exception>
#include <cassert>

namespace ilias {
namespace sort_detail {


const int MAX_STL_DIST = 128;


/*
 * Merge step in merge sort.
 */
template<typename T, typename Less>
std::vector<T>
merge(const std::vector<T>& v0, const std::vector<T>& v1, Less less)
{
	using vector_t = std::vector<T>;

	vector_t rv;
	rv.reserve(v0.size() + v1.size());

	auto b0 = v0.begin();
	auto b1 = v1.begin();
	const auto e0 = v0.end();
	const auto e1 = v1.end();

	while (b0 != e0 && b1 != e1) {
		if (less(*b1, *b0))
			rv.push_back(*b1++);
		else
			rv.push_back(*b0++);
	}

	std::copy(b0, e0, std::back_inserter(rv));
	std::copy(b1, e1, std::back_inserter(rv));
	return rv;
}

/*
 * Partition operation in quick sort.
 */
template<typename Iterator, typename Less>
Iterator
quick_sort_partition(const Iterator& b, const Iterator& e, Less less)
{
	using iterator = Iterator;
	using difference_type =
	    typename std::iterator_traits<iterator>::difference_type;

	assert(b != e);

	/* Choose random pivot. */
	difference_type off;
	try {
		std::random_device rd;
		std::uniform_int_distribution<difference_type> dis(
		    0, b - e - 1);
		off = dis(rd);
	} catch (...) {
		/* Fallback: some value. */
		off = 0;
	}
	iterator m = b + off;
	assert(off >= 0 && off < e - b);

	iterator tail = m + 1;
	for (iterator i = b; i != m; ++i) {
		if (less(*m, *i)) {
			iter_swap(m, i);
			--m;
			if (i == m)
				break;
			iter_swap(m, i);
		}
	}
	for (iterator i = tail; i != e; ++i) {
		if (less(*i, *m)) {
			iter_swap(m, i);
			++m;
			if (i != m)
				iter_swap(m, i);
		}
	}
	return m;
}

/*
 * Recursion body of the quick sort algorithm.
 *
 * The body stores the output promise in temporary storage,
 * so it can be filled in after the sub steps complete.
 */
template<typename VPtr, typename Iterator, typename Less>
void
quick_sort_body(promise<void> out, const workq_ptr& wq, const VPtr& v,
    Iterator b, Iterator e, Less less)
{
	using iterator = Iterator;
	using namespace std::placeholders;

	/* Optimization: sort small collections using STL. */
	if (e - b <= MAX_STL_DIST) {
		std::sort(b, e, std::move(less));
		out.set();
		return;
	}

	auto m = quick_sort_partition(b, e, less);
	auto head = new_promise<void>(wq,
	    std::bind(&quick_sort_body<VPtr, Iterator, Less>,
	    _1, wq, v, b, m, less), workq_job::TYPE_PARALLEL);
	auto tail = new_promise<void>(wq,
	    std::bind(&quick_sort_body<VPtr, Iterator, Less>,
	    _1, wq, v, m, e, less), workq_job::TYPE_PARALLEL);

	auto c = combine<void>([](promise<void> out,
	    std::tuple<future<void>, future<void>> in) {
		std::get<0>(in).get();  /* Triggers exception. */
		std::get<1>(in).get();  /* Triggers exception. */
		out.set();
	    },
	    std::move(head), std::move(tail));
	callback(c, std::bind([](promise<void> out, future<void> in) {
		try {
			/* Check that input completed succesfully. */
			in.get();

			out.set();
		} catch (...) {
			out.set_exception(std::current_exception());
		}
	    },
	    out, _1));
}


} /* namespace ilias::sort_detail */


/*
 * Merge-sort implementation.
 *
 * XXX Probably too inefficient.
 */
template<typename Iterator, typename Less =
    std::less<typename std::iterator_traits<Iterator>::value_type>>
auto
merge_sort(workq_ptr wq, Iterator b, Iterator e, Less less = Less()) ->
future<std::vector<typename std::iterator_traits<Iterator>::value_type>>
{
	using vector_t = std::vector<
	    typename std::iterator_traits<Iterator>::value_type>;
	using promise_t = promise<vector_t>;
	using future_t = future<vector_t>;
	using namespace std::placeholders;

	/*
	 * Handle merge-sort leaf case (1 node) and
	 * corner case (empty collection).
	 *
	 * As an optimization, if the data set is very small, use the
	 * STL sort.
	 */
	const auto dist = e - b;
	if (dist <= sort_detail::MAX_STL_DIST) {
		return new_promise<vector_t>(wq, std::bind(
		    [less](promise_t p, const vector_t& data_) {
			vector_t& data = const_cast<vector_t&>(data_);
			std::sort(data.begin(), data.end(), less);
			p.set(std::move(data));
		    },
		    _1, vector_t{ b, e }), workq_job::TYPE_PARALLEL);
	}

	try {
		/*
		 * Promise wrapper for merge step.
		 */
		auto prom_merge = [less](promise_t out,
		    const std::tuple<future_t, future_t>& in) {
			using value_type = typename
			    std::iterator_traits<Iterator>::value_type;
			out.set(sort_detail::merge<value_type>(
			    std::get<0>(in).get(), std::get<1>(in).get(),
			    less));
		    };

		/*
		 * Split collection in half.
		 */
		const auto halfdist = (dist + 1) / 2;
		assert(halfdist != 0 && halfdist < dist);
		Iterator m = b + halfdist;

		/*
		 * Divide and conquer:
		 * collection is divided in [b, m) and [m, e).
		 * Merging the two halves together produces the full sorted collection.
		 */
		return combine<vector_t>(wq, workq_job::TYPE_PARALLEL,
		    std::move(prom_merge),
		    merge_sort(wq, b, m, less), merge_sort(wq, m, e, less));
	} catch (...) {
		auto rv = new_promise<vector_t>();
		rv.set_exception(std::current_exception());
		return rv;
	}
}


/*
 * Quick-sort implementation.
 *
 * Unstable sort algorithm.
 * Note that this quick sort may fail due to out-of-memory conditions,
 * because the asynchronous callbacks during sorting require additional
 * memory allocations.
 *
 * XXX Probably too inefficient.
 */
template<typename Iterator, typename Less =
    std::less<typename std::iterator_traits<Iterator>::value_type>>
auto
quick_sort(workq_ptr wq, Iterator b_, Iterator e_, Less less = Less()) ->
future<std::vector<typename std::iterator_traits<Iterator>::value_type>>
{
	using vector_t = std::vector<
	    typename std::iterator_traits<Iterator>::value_type>;
	using vector_ptr = std::shared_ptr<vector_t>;
	using promise_t = promise<vector_t>;
	using future_t = future<vector_t>;
	using std::iter_swap;
	using iterator = typename vector_t::iterator;
	using namespace std::placeholders;

	/* Alias the body of the quick sort routine. */
	const auto body =
	    &sort_detail::quick_sort_body<vector_ptr, iterator, Less>;

	try {
		/* Allocate output space. */
		auto rv = std::make_shared<vector_t>(b_, e_);
		/* Create calculation body promise. */
		auto algorithm = new_promise<void>(wq, std::bind(
		    body, _1, wq, rv, rv->begin(), rv->end(), less),
		    workq_job::TYPE_PARALLEL);
		return combine<vector_t>(
		    [rv](promise_t out, std::tuple<future<void>> in) {
			/* Check that input completed. */
			std::get<0>(in).get();

			out.set(std::move(*rv));
		    },
		    std::move(algorithm));
	} catch (...) {
		auto rv = new_promise<vector_t>();
		rv.set_exception(std::current_exception());
		return rv;
	}
}


} /* namespace ilias */

#endif /* ILIAS_WQ_SORT_H */
