#include <ilias/wq_msg_queue.h>

namespace ilias {
namespace {


void
activate_job(workq_job_ptr ptr, unsigned int flags) noexcept
{
	if (ptr)
		ptr->activate(flags);
}


} /* namespace ilias::<unnamed> */


void
output_callback(msg_queue_events& mqev, workq_job_ptr job, unsigned int flags)
{
	output_callback(mqev, std::bind(&activate_job, std::move(job), flags));
}

void
empty_callback(msg_queue_events& mqev, workq_job_ptr job, unsigned int flags)
{
	empty_callback(mqev, std::bind(&activate_job, std::move(job), flags));
}


} /* namespace ilias */
