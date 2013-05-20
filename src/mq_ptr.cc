#include <ilias/mq_ptr.h>

namespace ilias {


template
	mq_in_ptr<void, std::allocator<void>>
	new_mq_ptr<void, std::allocator<void>>();
template
	mq_in_ptr<int, std::allocator<int>>
	new_mq_ptr<int, std::allocator<int>>();


} /* namespace ilias */
