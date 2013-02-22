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
#include <ilias/eventset.h>
#include <cassert>
#include <climits>

namespace ilias {
namespace ev_detail {


std::function<void()>
base_event_set::_fire(std::unique_lock<std::mutex>& guard,
    base_event_set::event* ev, std::size_t idx, bool again) noexcept
{
	assert(bool(guard));
	std::function<void()> rv;

	this->m_active = idx;
	if (this->m_state == INACTIVE) {
		do {
			if (rv) {
				guard.unlock();
				rv = nullptr;
				guard.lock();
			}

			this->m_state = ACTIVE;
			if (this->m_active != SIZE_MAX)
				rv = ev[this->m_active].fire(guard);
		} while (this->m_state == ACT_AGAIN);
		this->m_state = INACTIVE;
	} else
		this->m_state = ACT_AGAIN;
	return rv;
}

void
base_event_set::fire(base_event_set::event* ev, std::size_t idx, bool again)
    noexcept
{
	/* Delay destruction to outside of lock. */
	std::function<void()> delay;

	std::unique_lock<std::mutex> guard{ this->m_mtx };
	delay = this->_fire(guard, ev, idx, again);
}

void
base_event_set::assign(base_event_set::event* ev, std::size_t idx,
    std::function<void()> fn) noexcept
{
	/* Delay destruction to outside of lock. */
	std::function<void()> delay[2];

	std::unique_lock<std::mutex> guard{ this->m_mtx };
	delay[0] = ev[idx].assign(std::move(fn));
	if (this->m_active == idx)
		delay[1] = this->_fire(guard, ev, idx, true);
}

void
base_event_set::clear(base_event_set::event* ev, std::size_t idx) noexcept
{
	/* Delay destruction to outside of lock. */
	std::function<void()> delay;

	std::unique_lock<std::mutex> guard{ this->m_mtx };
	delay = ev[idx].assign(nullptr);
}

void
base_event_set::deactivate(base_event_set::event* ev, std::size_t idx) noexcept
{
	std::unique_lock<std::mutex> guard{ this->m_mtx };
	if (this->m_active == idx) {
		this->m_active = SIZE_MAX;
		if (this->m_state == ACT_AGAIN)
			this->m_state = ACTIVE;
	}
}

void
base_event_set::deactivate_all() noexcept
{
	std::unique_lock<std::mutex> guard{ this->m_mtx };
	this->m_active = SIZE_MAX;
	if (this->m_state == ACT_AGAIN)
		this->m_state = ACTIVE;
}


base_event_set::event::~event() noexcept
{
	return;
}

std::function<void()>
base_event_set::event::fire(std::unique_lock<std::mutex>& lck) noexcept
{
	std::function<void()> tmp = std::move(m_fn);
	this->m_restore = true;

	if (tmp) {
		lck.unlock();
		tmp();
		lck.lock();
	}

	if (this->m_restore) {
		this->m_fn = std::move(tmp);
		this->m_restore = false;
	}
	return tmp;
}

std::function<void()>
base_event_set::event::assign(std::function<void()> fn) noexcept
{
	this->m_restore = false;	/* Prevent restoration. */
	auto rv = std::move(this->m_fn);
	this->m_fn = std::move(fn);
	return rv;
}


}} /* namespace ilias::ev_detail */
