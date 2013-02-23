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
