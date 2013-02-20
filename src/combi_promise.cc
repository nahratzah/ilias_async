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
#include <ilias/combi_promise.h>

namespace ilias {
namespace cprom_detail {


std::shared_ptr<void>
base_combiner::complete() noexcept
{
	using std::swap;

	assert(bool(this->m_self));
	std::shared_ptr<void> self;
	swap(self, this->m_self);

	this->m_fn(*this);
	return self;
}

std::shared_ptr<void>
base_combiner::notify() noexcept
{
	if (this->n_defer.fetch_sub(1,
	    std::memory_order_release) == 1)
		return this->complete();
	return std::shared_ptr<void>();
}

void
base_combiner::enable() noexcept
{
	this->m_self = this->shared_from_this();
	this->notify();
}

}} /* namespace ilias::cprom_detail */
