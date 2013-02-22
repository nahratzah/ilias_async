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
#ifndef ILIAS_EVENTSET_H
#define ILIAS_EVENTSET_H

#include <ilias/ilias_async_export.h>
#include <functional>
#include <mutex>

namespace ilias {
namespace ev_detail {


class base_event_set
{
protected:
	class event;

private:
	enum state {
		INACTIVE,
		ACTIVE,
		ACT_AGAIN,
	};

	state m_state{ INACTIVE };
	std::mutex m_mtx;	/* Protect event access. */
	std::size_t m_active;	/* Active event. */

	std::function<void()> _fire(std::unique_lock<std::mutex>&, event*,
	    std::size_t, bool) noexcept;

public:
	base_event_set(std::size_t initial = SIZE_MAX) noexcept
	:	m_active(initial)
	{
		/* Empty body. */
	}

	ILIAS_ASYNC_EXPORT void fire(event*, std::size_t, bool again = true)
	    noexcept;
	ILIAS_ASYNC_EXPORT void assign(event*, std::size_t,
	    std::function<void()>) noexcept;
	ILIAS_ASYNC_EXPORT void clear(event*, std::size_t) noexcept;
	ILIAS_ASYNC_EXPORT void deactivate(event*, std::size_t) noexcept;
	ILIAS_ASYNC_EXPORT void deactivate_all() noexcept;
};


class base_event_set::event
{
friend class base_event_set;

public:
	event() = default;
	event(const event&) = delete;
	event(event&&) = delete;
	event& operator=(const event&) = delete;
	~event() noexcept;

private:
	bool m_restore{ false };
	std::function<void()> m_fn;

	std::function<void()> fire(std::unique_lock<std::mutex>&) noexcept;
	std::function<void()> assign(std::function<void()>) noexcept;
};


} /* namespace ilias::ev_detail */


template<std::size_t N_Events>
class eventset
:	private ev_detail::base_event_set
{
private:
	event m_ev[N_Events];	/* Member allocation of events. */

	static void
	ensure_idx(std::size_t idx)
	{
		if (idx >= N_Events) {
			throw std::invalid_argument(
			    "eventset index out of bounds");
		}
	}

public:
	eventset() = default;
	eventset(std::size_t initial)
	:	ev_detail::base_event_set(initial)
	{
		ensure_idx(initial);
	}

	void
	fire(std::size_t idx)
	{
		ensure_idx(idx);
		this->base_event_set::fire(this->m_ev, idx);
	}

	void
	assign(std::size_t idx, std::function<void()> fn)
	{
		ensure_idx(idx);
		this->base_event_set::assign(this->m_ev, idx, fn);
	}

	void
	deactivate() noexcept
	{
		this->base_event_set::deactivate_all();
	}

	void
	deactivate(std::size_t idx)
	{
		ensure_idx(idx);
		this->base_event_set::deactivate(this->m_ev, idx);
	}

	void
	clear(std::size_t idx) noexcept
	{
		ensure_idx(idx);
		this->base_event_set::clear(this->m_ev, idx);
	}

	void
	clear() noexcept
	{
		for (std::size_t i = 0; i != N_Events; ++i)
			this->base_event_set::clear(this->m_ev, i);
	}
};


} /* namespace ilias */

#endif /* ILIAS_EVENTSET_H */
