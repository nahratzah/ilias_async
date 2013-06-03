#include <ilias/workq.h>
#include <ilias/wq_sort.h>
#include <ilias/threadpool.h>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>


const size_t SORT_SIZE = 512 * 1024 * 1024 / sizeof(int);  /* 1 GB of RAM */


std::vector<int>
create_data(size_t sz)
{
	std::vector<int> rv;
	rv.reserve(sz);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dis;

	while (rv.size() < sz)
		rv.push_back(dis(gen));
	return rv;
}


template<typename FN, typename... Args>
auto
time_functor(FN&& fn, Args&&... args)
->	decltype(std::chrono::high_resolution_clock::now() -
	    std::chrono::high_resolution_clock::now())
{
	const auto start = std::chrono::high_resolution_clock::now();
	fn(std::forward<Args>(args)...);
	const auto end = std::chrono::high_resolution_clock::now();
	return end - start;
}


template<typename T>
auto
msec(const T& v)
->	decltype(std::chrono::duration_cast<std::chrono::milliseconds>(v).
	    count())
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(v).
	    count();
}

template<typename MSG, typename FN, typename... Args>
auto
print_do_done(MSG&& msg, FN&& fn, Args&&... args)
->	decltype(fn(std::forward<Args>(args)...))
{
	(std::cout << msg << "... ").flush();
	auto rv = fn(std::forward<Args>(args)...);
	(std::cout << "done" << std::endl).flush();
	return rv;
}


int
main()
{
	using namespace std::placeholders;

	const auto input = print_do_done("generating data to sort",
	    &create_data, SORT_SIZE);
	auto wqs = ilias::new_workq_service();
	ilias::threadpool tp;
	threadpool_attach(*wqs, tp);

	auto stl_sort = std::bind(
	    [](std::vector<int> in) {
		return time_functor([&in]() {
			std::sort(in.begin(), in.end());
		    });
	    }, std::cref(input));

	auto wq_merge_sort = std::bind(
	    [](std::vector<int> in, ilias::workq_ptr wq) {
		return time_functor([&in, &wq]() {
			auto b = in.begin();
			auto e = in.end();
			ilias::merge_sort(wq, b, e).get();
		    });
	    }, std::cref(input), _1);

	auto wq_quick_sort = std::bind(
	    [](std::vector<int> in, ilias::workq_ptr wq) {
		return time_functor([&in, &wq]() {
			auto b = in.begin();
			auto e = in.end();
			ilias::quick_sort(wq, b, e).get();
		    });
	    }, std::cref(input), _1);

	(std::cout <<
	    "Timing of sorting algorithms on " << input.size() <<
	      " random numbers." << std::endl <<
	    "STL sort: ").flush() <<
	      msec(stl_sort()) << " ms" << std::endl;
	(std::cout <<
	    "workq based merge sort: ").flush() <<
	      msec(wq_merge_sort(wqs->new_workq())) << " ms" << std::endl;
	(std::cout <<
	    "workq based quick sort: ").flush() <<
	      msec(wq_merge_sort(wqs->new_workq())) << " ms" << std::endl;
	return 0;
}
