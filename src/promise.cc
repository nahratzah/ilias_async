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
#include <ilias/promise.h>

namespace ilias {


broken_promise::broken_promise()
:	std::runtime_error("broken promise: destroyed without setting a value")
{
	return;
}

broken_promise::~broken_promise() noexcept
{
	return;
}


uninitialized_promise::uninitialized_promise()
:	std::logic_error("uninitialized promise")
{
	return;
}

uninitialized_promise::~uninitialized_promise() noexcept
{
	return;
}


promise_cb_installed::promise_cb_installed()
:	std::logic_error("promise callback already installed")
{
	return;
}

promise_cb_installed::~promise_cb_installed() noexcept
{
	return;
}


namespace prom_detail {

base_prom_data::~base_prom_data() noexcept
{
	/* Empty body. */
}

void
base_prom_data::_start() noexcept
{
	/*
	 * Change state from CB_NONE to CB_NEED.
	 * If an execute functor is present, invoke it immediately and
	 * mark as done.
	 */
	if (this->m_cbstate == cb_state::CB_NONE) {
		if (this->m_execute) {
			this->invoke_execute_fn(this->m_execute);
			this->m_cbstate = cb_state::CB_DONE;
			this->m_execute = nullptr;
		} else
			this->m_cbstate = cb_state::CB_NEED;
	}
}

void
base_prom_data::_on_complete() noexcept
{
	using std::swap;

	/*
	 * Move all callbacks out of the lock,
	 * to protect against callbacks attempting to acquire the lock
	 * recursively.
	 */
	std::vector<execute_fn> cbs;
	{
		std::lock_guard<std::mutex> guard{ this->m_cblck };
		swap(cbs, this->m_callbacks);
	}

	for (auto& cb : cbs)
		this->invoke_execute_fn(cb);
}

void
base_prom_data::set_execute_fn(execute_fn fn)
{
	if (!fn)
		throw std::invalid_argument("nullptr execute function");

	std::lock_guard<std::mutex> guard{ this->m_cblck };

	/* Don't allow multiple callbacks to get invoked. */
	if (this->m_cbstate == cb_state::CB_DONE || this->m_execute)
		throw promise_cb_installed();

	if (this->m_cbstate == cb_state::CB_NEED) {
		this->invoke_execute_fn(fn);
		this->m_cbstate == cb_state::CB_DONE;
	} else
		this->m_execute = std::move(fn);
}

void
base_prom_data::start() noexcept
{
	std::lock_guard<std::mutex> guard{ this->m_cblck };
	this->_start();
}

void
base_prom_data::add_callback(execute_fn fn)
{
	if (!fn)
		throw std::invalid_argument("nullptr callback function");

	/* Already complete?  Invoke immediately. */
	if (this->ready()) {
		this->invoke_execute_fn(fn);
		return;
	}

	std::unique_lock<std::mutex> guard{ this->m_cblck };
	if (this->ready()) {
		/*
		 * Callbacks are only executed once, so if the promise
		 * is ready, we need to invoke this new callback manually.
		 */
		guard.unlock();
		this->invoke_execute_fn(fn);
	} else {
		this->m_callbacks.emplace_back(std::move(fn));
		/*
		 * Ensure the promise gets started,
		 * since callbacks depend on it.
		 */
		this->_start();
	}
}

bool
prom_data<void>::assign_exception(std::exception_ptr exc)
{
	if (!exc)
		throw std::invalid_argument("exception pointer is a nullptr");

	base_prom_data::lock lck{ *this };
	if (!lck)
		return false;

	this->m_exc = exc;
	lck.release(state::S_EXCEPT);
	return true;
}

bool
prom_data<void>::assign() noexcept
{
	base_prom_data::lock lck{ *this };
	if (!lck)
		return false;

	lck.release(state::S_SET);
	return true;
}

void
prom_data<void>::get() const
{
	this->wait();

	switch (this->current_state()) {
	case state::S_NIL:
	case state::S_BUSY:
		assert(false);
		while (true);	/* Undefined behaviour: spin. */
	case state::S_SET:
		return;
	case state::S_BROKEN:
		throw broken_promise();
	case state::S_EXCEPT:
		std::atomic_thread_fence(std::memory_order_acquire);
		std::rethrow_exception(this->m_exc);
	}
}


}} /* namespace ilias::prom_detail */
