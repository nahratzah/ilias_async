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
#ifndef ILIAS_LLPTR_H
#define ILIAS_LLPTR_H

#include <ilias/ilias_async_export.h>
#include <ilias/hazard.h>
#include <ilias/refcnt.h>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace ilias {
namespace llptr_detail {


template<typename Type, typename AcqRel, unsigned int Flags>
struct pointer_helper
{
	using flags_type = std::bitset<Flags>;
	using simple_pointer = refpointer<Type, AcqRel>;
	using element_type = std::tuple<simple_pointer, flags_type>;
	using ptr_t = typename simple_pointer::pointer;
	using ref_t = typename simple_pointer::reference;
};


} /* namespace ilias::llptr_detail */


/*
 * Atomic pointer for reference counted type with flags.
 *
 * Interfaces as std::atomic<std::tuple<refpointer<Type>, std::bitset<Flags>>>.
 */
template<typename Type, typename AcqRel = default_refcount_mgr<Type>,
    unsigned int Flags = 0U>
class llptr
:	public llptr_detail::pointer_helper<Type, AcqRel, Flags>
{
public:
	using typename
	    llptr_detail::pointer_helper<Type, AcqRel, Flags>::flags_type;
	using typename
	    llptr_detail::pointer_helper<Type, AcqRel, Flags>::simple_pointer;
	using typename
	    llptr_detail::pointer_helper<Type, AcqRel, Flags>::element_type;
	using typename
	    llptr_detail::pointer_helper<Type, AcqRel, Flags>::ptr_t;
	using typename
	    llptr_detail::pointer_helper<Type, AcqRel, Flags>::ref_t;
	using no_acquire_t = std::tuple<ptr_t, flags_type>;

private:
	using hazard_t = hazard<llptr, Type>;

	mutable AcqRel m_acqrel;
	std::atomic<std::uintptr_t> m_ptr{ 0U };

	static constexpr std::uintptr_t
	_flags() noexcept
	{
		return (std::uintptr_t(1U) << Flags) - 1U;
	}

	static constexpr std::uintptr_t
	_mask() noexcept
	{
		return ~_flags();
	}

	static ptr_t
	_ptr(std::uintptr_t v) noexcept
	{
		return reinterpret_cast<ptr_t>(v & _mask());
	}

	static flags_type
	_bitmask(std::uintptr_t v) noexcept
	{
		return flags_type{ v & _flags() };
	}

	static std::uintptr_t
	_encode(ptr_t p, const flags_type& fl) noexcept
	{
		static_assert(std::alignment_of<Type>::value % (1U << Flags) ==
		    0U,
		    "llptr: type alignment must be greater than 1<<Flags.");

		return (reinterpret_cast<std::uintptr_t>(p) |
		    fl.to_ulong());
	}

	static element_type
	_decode(std::uintptr_t v, bool acquire) noexcept
	{
		return element_type{ simple_pointer{ _ptr(v), acquire },
		    _bitmask(v) };
	}

	template<typename Refcount, typename Return>
	void
	acquire(ref_t p, unsigned int nrefs,
	    Return (AcqRel::*)(ref_t, Refcount) = &AcqRel::acquire)
	const noexcept
	{
		if (nrefs > 0)
			this->m_acqrel.acquire(p, nrefs);
	}

	template<typename Refcount, typename Return>
	void
	release(ref_t p, unsigned int nrefs,
	    Return (AcqRel::*)(ref_t, Refcount) = &AcqRel::release)
	const noexcept
	{
		if (nrefs > 0)
			this->m_acqrel.release(p, nrefs);
	}

	void
	acquire(ref_t p, unsigned int nrefs, ...) const noexcept
	{
		while (nrefs-- > 0)
			this->m_acqrel.acquire(p);
	}

	void
	release(ref_t p, unsigned int nrefs, ...) const noexcept
	{
		while (nrefs-- > 0)
			this->m_acqrel.release(p);
	}

	void
	grant(ptr_t p, unsigned int nrefs) const noexcept
	{
		if (p == nullptr)
			return;

		auto acquire_fn =
		    [this, &p](unsigned int n) {
			this->acquire(*p, n);
		    };
		auto release_fn =
		    [this, &p](unsigned int n) {
			this->release(*p, n);
		    };

		hazard_t::grant(std::move(acquire_fn), std::move(release_fn),
		    *this, *p, nrefs);
	}

	unsigned int
	do_hazard(hazard_t& hz, ptr_t v_ptr) const noexcept
	{
		if (!v_ptr)
			return 0U;

		unsigned int rv = 0U;
		hz.do_hazard(*v_ptr,
		    [&rv, this, v_ptr]() {
			if (_ptr(this->m_ptr.load(
			    std::memory_order_consume)) == v_ptr) {
				this->acquire(*v_ptr, 1U);
				++rv;
			}
		    },
		    [&rv]() {
			++rv;
		    });
		return rv;
	}

public:
	llptr() = default;
	llptr(const llptr&) = delete;
	llptr(llptr&&) = delete;
	llptr& operator=(const llptr&) = delete;

	llptr(element_type v) noexcept
	:	m_ptr{ _encode(std::get<0>(v).release(), std::get<1>(v)) }
	{
		/* Empty body. */
	}

	llptr(std::nullptr_t) noexcept
	:	llptr()
	{
		/* Empty body. */
	}

	~llptr() noexcept
	{
		this->store(nullptr, std::memory_order_release);
	}

	element_type
	load(std::memory_order mo = std::memory_order_seq_cst) const noexcept
	{
		std::uintptr_t v;
		unsigned int acq;
		hazard_t hz{ *this };

		do {
			v = this->m_ptr.load(mo);
			acq = this->do_hazard(hz, _ptr(v));
		} while (acq == 0 && _ptr(v) != nullptr);

		if (acq > 1U)
			this->release(*_ptr(v), acq - 1U);

		return _decode(v, false);
	}

	void
	store(element_type v, std::memory_order mo = std::memory_order_seq_cst)
	noexcept
	{
		const std::uintptr_t p = this->m_ptr.exchange(
		    _encode(std::get<0>(v).release(), std::get<1>(v)), mo);
		this->grant(_ptr(p), 1);
	}

	element_type
	exchange(element_type v,
	    std::memory_order mo = std::memory_order_seq_cst)
	noexcept
	{
		const std::uintptr_t p = this->m_ptr.exchange(
		    _encode(std::get<0>(v).release(), std::get<1>(v)), mo);
		this->grant(_ptr(p), 0);
		return _decode(p, false);
	}

	bool
	compare_exchange_weak(element_type& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect).get(),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_weak(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(_ptr(expect_), 1);
			return true;
		}

		/*
		 * Skip hazard logic if there is no need to acquire
		 * the reference.
		 */
		if (_ptr(expect_) == std::get<0>(expect).get()) {
			std::get<1>(expect) = _bitmask(expect_);
			return false;
		} else if (_ptr(expect_) == nullptr) {
			std::get<0>(expect).reset();
			std::get<1>(expect) = _bitmask(expect_);
			return false;
		}

		hazard_t hz{ *this };
		unsigned int acq;
		while ((acq = this->do_hazard(hz, _ptr(expect_))) == 0U &&
		    _ptr(expect_) != nullptr)
			expect_ = this->m_ptr.load(mo_fail);

		if (acq > 1U)
			this->release(*_ptr(expect_), acq - 1U);

		expect = _decode(expect_, false);
		return false;
	}

	bool
	compare_exchange_weak(no_acquire_t& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_weak(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(_ptr(expect_), 1);
			return true;
		}

		expect = std::make_tuple(_ptr(expect_), _bitmask(expect_));
		return false;
	}

	/*
	 * Compare_exchange_weak implementation for rvalue-ref expect.
	 *
	 * If you don't care about the value of expect, there's no need
	 * to do the complicated dance of acquiring the expect reference.
	 */
	bool
	compare_exchange_weak(element_type&& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect).get(),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_weak(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(std::get<0>(expect).release(), 2);
			return true;
		}
		return false;
	}

	bool
	compare_exchange_weak(no_acquire_t&& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_weak(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(_ptr(expect_), 1);
			return true;
		}
		return false;
	}

	bool
	compare_exchange_strong(element_type& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		const auto expect_ = _encode(std::get<0>(expect).get(),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		for (;;) {
			auto expect__ = expect_;
			if (this->m_ptr.compare_exchange_strong(expect__, set_,
			    mo_success, mo_fail)) {
				std::get<0>(set).release();
				this->grant(_ptr(expect__), 1);
				return true;
			}

			/*
			 * Skip acquisition of reference if there is no
			 * need to acquire one.
			 */
			if (_ptr(expect__) == nullptr) {
				expect = _decode(expect__, false);
				return false;
			} else if (_ptr(expect__) ==
			    std::get<0>(expect).get()) {
				std::get<1>(expect) = _bitmask(expect__);
				return false;
			}

			hazard_t hz{ *this };
			do {
				auto acq = this->do_hazard(hz, _ptr(expect__));
				if (acq != 0U) {
					if (acq > 1U) {
						this->release(*_ptr(expect_),
						    acq - 1U);
					}
					expect = _decode(expect__, false);
					return false;
				}
				expect__ = this->m_ptr.load(mo_fail);
			} while (expect__ != expect_);
		}
	}

	bool
	compare_exchange_strong(no_acquire_t& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_strong(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(_ptr(expect_), 1);
			return true;
		}

		expect = std::make_tuple(_ptr(expect_), _bitmask(expect_));
		return false;
	}

	/*
	 * Compare_exchange_strong implementation for rvalue-ref expect.
	 *
	 * If you don't care about the value of expect, there's no need
	 * to do the complicated dance of acquiring the expect reference.
	 */
	bool
	compare_exchange_strong(element_type&& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect).get(),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_strong(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(std::get<0>(expect).release(), 2);
			return true;
		}
		return false;
	}

	bool
	compare_exchange_strong(no_acquire_t&& expect, element_type set,
	    std::memory_order mo_success = std::memory_order_seq_cst,
	    std::memory_order mo_fail = std::memory_order_seq_cst)
	noexcept
	{
		auto expect_ = _encode(std::get<0>(expect),
		    std::get<1>(expect));
		const auto set_ = _encode(std::get<0>(set).get(),
		    std::get<1>(set));

		if (this->m_ptr.compare_exchange_strong(expect_, set_,
		    mo_success, mo_fail)) {
			std::get<0>(set).release();
			this->grant(_ptr(expect_), 1);
			return true;
		}
		return false;
	}

	void
	reset(std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		this->grant(_ptr(this->m_ptr.exchange(0U, mo)), 1);
	}

	llptr&
	operator=(element_type v) noexcept
	{
		this->store(std::move(v));
		return *this;
	}

	llptr&
	operator=(std::nullptr_t) noexcept
	{
		this->reset();
		return *this;
	}

	operator element_type() const noexcept
	{
		return this->load();
	}

	bool
	is_lock_free() const noexcept
	{
		hazard_t hz{ *this };
		return this->m_ptr.is_lock_free() && hz.is_lock_free();
	}

	friend bool
	atomic_is_lock_free(const llptr* p) noexcept
	{
		return p && p->is_lock_free();
	}

	flags_type
	fetch_or(flags_type fl,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		return _bitmask(this->m_ptr.fetch_or(fl.to_ulong(), mo));
	}

	flags_type
	fetch_xor(flags_type fl,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		return _bitmask(this->m_ptr.fetch_xor(fl.to_ulong(), mo));
	}

	flags_type
	fetch_and(flags_type fl,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		return _bitmask(
		    this->m_ptr.fetch_and(_mask() | fl.to_ulong(), mo));
	}

	flags_type
	load_flags(std::memory_order mo = std::memory_order_seq_cst)
	const noexcept
	{
		return _bitmask(this->m_ptr.load(mo));
	}

	no_acquire_t
	load_no_acquire(std::memory_order mo = std::memory_order_seq_cst)
	noexcept
	{
		const auto v = this->m_ptr.load(mo);
		return std::make_tuple(_ptr(v), _bitmask(v));
	}
};


} /* namespace ilias */

#endif /* ILIAS_LLPTR_H */
