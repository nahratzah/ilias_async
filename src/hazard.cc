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
#include <ilias/hazard.h>
#include <array>


namespace ilias {
namespace hazard_detail {
namespace {


using hazards_t = std::array<hazard_t, 64>;

std::atomic<unsigned short> hz_idx;
alignas(4096) hazards_t hazards;

bool
mark(hazard_t& h, std::uintptr_t owner, std::uintptr_t value) noexcept
{
	std::uintptr_t expect;
	do {
		if (h.value.load(std::memory_order_relaxed) != value)
			break;

		expect = owner;
		if (h.owner.compare_exchange_weak(expect,
		    owner | hazard_t::FLAG,
		    std::memory_order_acquire,
		    std::memory_order_relaxed)) {
			auto rv = h.value.compare_exchange_strong(value, 0U,
			    std::memory_order_relaxed,
			    std::memory_order_relaxed);

			h.owner.fetch_and(hazard_t::MASK,
			    std::memory_order_release);
			return rv;
		}
	} while ((expect & hazard_t::MASK) == owner);
	return false;
}


}} /* namespace ilias::hazard_detail::<unnamed> */


hazard_detail::hazard_t&
basic_hazard::allocate_hazard(std::uintptr_t owner) noexcept
{
	using namespace hazard_detail;

	assert(owner != 0U && (owner & hazard_t::FLAG) == 0U);

	const hazards_t::size_type idx =
	    hz_idx.fetch_add(1U, std::memory_order_relaxed) % hazards.size();

	for (auto h = hazards.begin() + idx; true; h = hazards.begin()) {
		for (; h != hazards.end(); ++h) {
			std::uintptr_t expect = 0U;
			if (h->owner.compare_exchange_weak(expect, owner,
			    std::memory_order_relaxed,
			    std::memory_order_relaxed))
				return *h;
		}
	}

	/* UNREACHABLE */
}

std::size_t
basic_hazard::hazard_count() noexcept
{
	using namespace hazard_detail;

	return hazards.size();
}

std::size_t
basic_hazard::hazard_grant(std::uintptr_t owner, std::uintptr_t value) noexcept
{
	using namespace hazard_detail;

	std::size_t count = 0;
	for (auto& h : hazards) {
		if (mark(h, owner, value))
			++count;
	}
	return count;
}


} /* namespace ilias */
