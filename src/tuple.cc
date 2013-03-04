/* Example code, will go away soonish. */

#include <tuple>
#include <ilias/tuple.h>

#include <string>
#include <iostream>

using namespace ilias;


struct print
{
	template<typename T>
	void
	operator()(const T& v) const
	{
		std::cout << " " << v;
	}
};

int
main()
{
	std::tuple<int, std::string, float> v = { 42, "foobar", 3.14159 };

	auto v_tail = tail(v);
	auto v_slice = slice<0, 2>(v);

	std::cout << "Origin tuple was: ";
	visit(v, print());
	std::cout << std::endl;

	std::cout << "Tail is: ";
	visit(v_tail, print());
	std::cout << std::endl;

	std::cout << "Slice[0:2] is: ";
	visit(v_slice, print());
	std::cout << std::endl;

	return 0;
}
