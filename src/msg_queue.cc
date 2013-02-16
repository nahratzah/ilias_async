#include <ilias/msg_queue.h>
#include <limits>


namespace ilias {
namespace mq_detail {


msg_queue_events::~msg_queue_events() noexcept
{
	/* Empty body. */
}

void
msg_queue_events::_fire_output() noexcept
{
	workq_deactivate(this->ev_empty);
	workq_activate(this->ev_output, workq_job::ACT_IMMED);
}

void
msg_queue_events::_fire_empty() noexcept
{
	workq_deactivate(this->ev_output);
	workq_activate(this->ev_empty);
}

void
msg_queue_events::_assign_output(workq_job_ptr job, bool fire) noexcept
{
	std::atomic_store_explicit(&this->ev_output, job,
	    std::memory_order_relaxed);
	if (fire && job)
		job->activate();
}

void
msg_queue_events::_assign_empty(workq_job_ptr job, bool fire) noexcept
{
	std::atomic_store_explicit(&this->ev_empty, job,
	    std::memory_order_relaxed);
	if (fire && job)
		job->activate();
}

void
msg_queue_events::_clear_events() noexcept
{
	this->_clear_output();
	this->_clear_empty();
}


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
	if (sz == subtract)
		this->_fire_empty();
	return subtract;
}


} /* namespace ilias::mq_detail */


template class msg_queue<int>;
template class msg_queue<std::string>;


} /* namespace ilias */
