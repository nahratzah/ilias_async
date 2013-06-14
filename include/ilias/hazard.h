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
#ifndef ILIAS_HAZARD_H
#define ILIAS_HAZARD_H

#include <ilias/ilias_async_export.h>
#include <ilias/util.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>


namespace ilias {
namespace hazard_detail {


/* Hazard pointer logic. */
struct alignas(64) hazard_t
{
	static constexpr std::uintptr_t FLAG = 0x1U;
	static constexpr std::uintptr_t MASK = ~FLAG;

	std::atomic<std::uintptr_t> owner;
	std::atomic<std::uintptr_t> value;
};


} /* namespace ilias::hazard_detail */


/*
 * Basic hazard-pointer implementation.
 */
class basic_hazard
{
private:
	using hazard_t = hazard_detail::hazard_t;

	hazard_t& m_hazard;

	static std::uintptr_t
	validate_owner(std::uintptr_t p)
	{
		if (p == 0U) {
			throw std::invalid_argument("hazard: "
			    "owner must be non-null");
		}

		if ((p & hazard_t::FLAG) != 0U) {
			throw std::invalid_argument("hazard: "
			    "owner may not have LSB set");
		}

		return p;
	}

	ILIAS_ASYNC_EXPORT static hazard_t& allocate_hazard(std::uintptr_t)
	    noexcept;
	ILIAS_ASYNC_EXPORT static std::size_t hazard_count() noexcept;
	ILIAS_ASYNC_EXPORT static std::size_t hazard_grant(std::uintptr_t,
	    std::uintptr_t) noexcept;

public:
	basic_hazard(std::uintptr_t owner)
	:	m_hazard{ allocate_hazard(validate_owner(owner)) }
	{
		assert(this->m_hazard.value.load(std::memory_order_relaxed) ==
		    0U);
	}

	basic_hazard(const basic_hazard&) = delete;
	basic_hazard(basic_hazard&&) = delete;
	basic_hazard& operator=(const basic_hazard&) = delete;

	~basic_hazard() noexcept
	{
		assert(this->m_hazard.value.load(std::memory_order_relaxed) ==
		    0U);
		this->m_hazard.owner.fetch_and(hazard_t::FLAG,
		    std::memory_order_release);
	}

	bool
	is_lock_free() const noexcept
	{
		return atomic_is_lock_free(&this->m_hazard.owner) &&
		    atomic_is_lock_free(&this->m_hazard.value);
	}

	friend bool
	atomic_is_lock_free(const basic_hazard* h) noexcept
	{
		return h && h->is_lock_free();
	}

	template<typename OperationFn, typename NilFn>
	void
	do_hazard(std::uintptr_t value,
	    OperationFn&& operation, NilFn&& on_nil)
	noexcept
	{
		auto ov = this->m_hazard.value.exchange(value,
		    std::memory_order_acquire);
		assert(ov == 0U);
		do_noexcept(operation);
		if (this->m_hazard.value.exchange(0U,
		    std::memory_order_release) == 0U)
			do_noexcept(on_nil);
	}

	template<typename AcquireFn, typename ReleaseFn>
	static void
	grant(AcquireFn&& acquire, ReleaseFn&& release,
	    std::uintptr_t owner, std::uintptr_t value,
	    unsigned int nrefs = 0U)
	{
		validate_owner(owner);

		do_noexcept([&]() {
			const auto hzc = hazard_count();

			if (nrefs < hzc) {
				acquire(hzc - nrefs);
				nrefs = hzc;
			}

			nrefs -= hazard_grant(owner, value);
			if (nrefs > 0U)
				release(nrefs);
		    });
	}
};

/*
 * Hazard handler for a specific owner type.
 */
template<typename OwnerType, typename ValueType>
class hazard
:	public basic_hazard
{
	static_assert((std::alignment_of<OwnerType>::value &
	    hazard_detail::hazard_t::FLAG) == 0U,
	    "hazard: "
	    "reference type must be aligned on an even number of bytes");

public:
	using owner_type = OwnerType;
	using value_type = ValueType;
	using owner_reference = const owner_type&;
	using value_reference = const value_type&;

	static std::uintptr_t
	owner_key(owner_reference v) noexcept
	{
		return reinterpret_cast<std::uintptr_t>(std::addressof(v));
	}

	static std::uintptr_t
	value_key(value_reference v) noexcept
	{
		return reinterpret_cast<std::uintptr_t>(std::addressof(v));
	}

	hazard(owner_reference v)
	:	basic_hazard{ owner_key(v) }
	{
		/* Empty body. */
	}

	template<typename OperationFn, typename NilFn>
	auto
	do_hazard(value_reference value,
	    OperationFn&& operation, NilFn&& on_nil)
	noexcept
	->	decltype(std::declval<basic_hazard>().do_hazard(
		    std::declval<std::uintptr_t>(),
		    std::forward<OperationFn>(operation),
		    std::forward<NilFn>(on_nil)))
	{
		return this->basic_hazard::do_hazard(value_key(value),
		    std::forward<OperationFn>(operation),
		    std::forward<NilFn>(on_nil));
	}

	template<typename AcquireFn, typename ReleaseFn>
	static void
	grant(AcquireFn&& acquire, ReleaseFn&& release,
	    owner_reference owner, value_reference value,
	    unsigned int nrefs = 0U)
	{
		basic_hazard::grant(std::forward<AcquireFn>(acquire),
		    std::forward<ReleaseFn>(release),
		    owner_key(owner), value_key(value), nrefs);
	}
};


} /* namespace ilias */

#endif /* ILIAS_HAZARD_H */
