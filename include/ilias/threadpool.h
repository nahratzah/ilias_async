/*
 * Copyright (c) 2012 - 2013 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef ILIAS_THREADPOOL_H
#define ILIAS_THREADPOOL_H

#include <ilias/ilias_async_export.h>
#include <ilias/threadpool_intf.h>
#include <memory>

namespace ilias {


class ILIAS_ASYNC_EXPORT threadpool
{
private:
	class ILIAS_ASYNC_LOCAL impl;

	struct impl_deleter
	{
		void operator()(impl*) const noexcept;
	};

	std::unique_ptr<impl, impl_deleter> m_impl;

public:
	threadpool();
	threadpool(unsigned int);

	threadpool(threadpool&& o) noexcept
	:	m_impl(std::move(o.m_impl))
	{
		/* Empty body. */
	}

	~threadpool() noexcept;

	bool curthread_is_threadpool() const noexcept;

	threadpool(const threadpool&) = delete;
	threadpool& operator=(const threadpool&) = delete;

	void set_nthreads(unsigned int);
	unsigned int get_nthreads() const noexcept;


	class ILIAS_ASYNC_EXPORT threadpool_service
	:	public virtual threadpool_service_intf
	{
	friend class threadpool::impl;

	private:
		impl& m_self;

	protected:
		unsigned int wakeup(unsigned int) noexcept;

	public:
		threadpool_service(threadpool& tp) noexcept
		:	m_self(*tp.m_impl)
		{
			/* Empty body. */
		}

		~threadpool_service() noexcept;
	};

	threadpool&
	threadpool_service_arg() noexcept
	{
		return *this;
	}

	ILIAS_ASYNC_EXPORT void attach(
	    threadpool_service_ptr<threadpool_service>);
};


extern template ILIAS_ASYNC_EXPORT
	void threadpool_attach<tp_service_multiplexer, threadpool>(
	    tp_service_multiplexer&, threadpool&);

class workq_service;
extern template ILIAS_ASYNC_EXPORT
	void threadpool_attach<workq_service, threadpool>(
	    workq_service&, threadpool&);


} /* namespace ilias */

#endif /* ILIAS_THREADPOOL_H */
