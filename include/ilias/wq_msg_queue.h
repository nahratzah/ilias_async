#ifndef ILIAS_WQ_MSG_QUEUE_H
#define ILIAS_WQ_MSG_QUEUE_H

#include <ilias/msg_queue.h>
#include <ilias/workq.h>
#include <type_traits>
#include <functional>

namespace ilias {


template<typename JobType>
typename std::enable_if<std::is_base_of<workq_job, JobType>::value, void>::type
output_callback(msg_queue_events& mqev, std::shared_ptr<JobType> wqjob,
    unsigned int flags = 0)
{
	output_callback(mqev, workq_job_ptr(std::move(wqjob)), flags);
}

template<typename JobType>
typename std::enable_if<std::is_base_of<workq_job, JobType>::value, void>::type
empty_callback(msg_queue_events& mqev, std::shared_ptr<JobType> wqjob,
    unsigned int flags = 0)
{
	empty_callback(mqev, workq_job_ptr(std::move(wqjob)), flags);
}


ILIAS_ASYNC_EXPORT void output_callback(msg_queue_events&, workq_job_ptr,
    unsigned int = 0);
ILIAS_ASYNC_EXPORT void empty_callback(msg_queue_events&, workq_job_ptr,
    unsigned int = 0);


} /* namespace ilias */

#endif /* ILIAS_WQ_MSG_QUEUE_H */
