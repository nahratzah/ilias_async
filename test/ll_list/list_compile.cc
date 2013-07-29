#include <ilias/ll_list.h>
#include <ilias/refcnt.h>
#include <iostream>


class foo
:	public ilias::ll_list_hook<>,
	public ilias::refcount_base<foo>
{
};

using foo_list = ilias::ll_smartptr_list<foo>;


int
main()
{
	foo_list fl;
	fl.push_back(ilias::make_refpointer<foo>());
	fl.push_front(ilias::make_refpointer<foo>());

	fl.visit([](const foo& e) {
		std::cout << "Element at address " << &e << std::endl;
	    });

	fl.erase(fl.begin());

	return 0;
}
