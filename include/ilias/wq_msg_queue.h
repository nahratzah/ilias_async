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
