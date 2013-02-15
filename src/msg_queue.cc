#include <ilias/msg_queue.h>
#include <limits>


namespace ilias {
namespace mq_detail {


uintptr_t
void_msg_queue::_dequeue(uintptr_t max) noexcept
{
	auto sz = this->m_size.load(std::memory_order_relaxed);
	uintptr_t subtract;

	do {
		if (sz == 0)
			return 0;
		subtract = std::min(sz, max);
	} while (!this->m_size.compare_exchange_weak(sz, sz - subtract,
	    std::memory_order_relaxed, std::memory_order_relaxed));
	return subtract;
}


} /* namespace ilias::mq_detail */


template class msg_queue<int>;
template class msg_queue<std::string>;


} /* namespace ilias */
