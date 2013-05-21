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
#ifndef ILIAS_WQ_CALLBACK_H
#define ILIAS_WQ_CALLBACK_H

#include <ilias/util.h>
#include <ilias/workq.h>
#include <atomic>
#include <mutex>
#include <type_traits>
#include <utility>


namespace ilias {
namespace wq_callback_detail {


/*
 * Workq job for callbacks.
 *
 * Stores the most recent argument only and invokes with that argument.
 */
template<typename ArgType, typename FnType>
class wq_callback_job
:	public workq_job
{
private:
	using arg_data =
	    opt_data<typename std::remove_reference<ArgType>::type>;
	using fn_type = FnType;

	std::mutex m_lck;
	arg_data m_arg;
	fn_type m_fn;

public:
	template<typename Fn>
	wq_callback_job(workq_ptr wq, Fn&& fn, unsigned int flags = 0)
	:	workq_job{ std::move(wq), flags },
		m_fn{ std::forward<Fn>(fn) }
	{
		/* Empty body. */
	}

	void
	run() noexcept override
	{
		using std::swap;

		auto arg = do_locked(this->m_lck, [&]() {
			return this->m_arg;
		    });

		if (arg)
			this->m_fn(*arg);
	}

	static void
	do_callback(const std::shared_ptr<wq_callback_job>& self, ArgType arg)
	noexcept
	{
		do_locked(self->m_lck, [&]() {
			self->m_arg = std::forward<ArgType>(arg);
		    });
		self->activate();
	}
};

/*
 * Specialization for pointer types.
 *
 * Uses std::atomic for atomic storage of datatype.
 */
template<typename ArgType, typename FnType>
class wq_callback_job<ArgType&, FnType>
:	public workq_job
{
private:
	using arg_data =
	    std::atomic<typename std::remove_reference<ArgType>::type*>;
	using fn_type = FnType;

	arg_data m_arg{ nullptr };
	fn_type m_fn;

public:
	template<typename Fn>
	wq_callback_job(workq_ptr wq, Fn&& fn, unsigned int flags = 0)
	:	workq_job{ std::move(wq), flags },
		m_fn{ std::forward<Fn>(fn) }
	{
		/* Empty body. */
	}

	void
	run() noexcept override
	{
		auto p = this->m_arg.load(nullptr,
		    std::memory_order_consume);

		if (p)
			this->m_fn(*p);
	}

	static void
	do_callback(const std::shared_ptr<wq_callback_job>& self, ArgType& arg)
	noexcept
	{
		self->m_arg.store(&arg,
		    std::memory_order_release);
		std::atomic_signal_fence(std::memory_order_acquire);
		self->activate();
	}
};


} /* namespace ilias::wq_callback_detail */


template<typename Ev, typename Fn>
void
callback(Ev& ev, workq_ptr wq, Fn&& fn, unsigned int flags = 0)
{
	using namespace std::placeholders;
	using arg_type = typename Ev::callback_arg_type;
	using fn_type = typename std::remove_const<
	    typename std::remove_reference<Fn>::type>::type;
	using job = wq_callback_detail::wq_callback_job<arg_type, fn_type>;

	auto impl =
	    new_workq_job<job>(std::move(wq), std::forward<Fn>(fn), flags);
	callback(ev, std::bind(&job::do_callback, std::move(impl), _1));
}


} /* namespace ilias */

#endif /* ILIAS_WQ_CALLBACK_H */
