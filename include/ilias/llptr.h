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


template<typename Type, typename AcqRel>
struct acqrel_helper
{
private:
	mutable AcqRel m_acqrel;

	using ref_t = Type&;

public:
	/*
	 * Acquire implementation, does bulk acquisition
	 * (i.e. a single operation to acquire all requested references).
	 *
	 * SFINAE based.
	 */
	template<typename Refcount, typename Return>
	void
	acquire(ref_t p, std::size_t nrefs,
	    Return (AcqRel::*)(ref_t, Refcount) = &AcqRel::acquire)
	const noexcept
	{
		if (nrefs > 0)
			this->m_acqrel.acquire(p, nrefs);
	}

	/*
	 * Release implementation, does bulk release
	 * (i.e. a single operation to release all requested references).
	 *
	 * SFINAE based.
	 */
	template<typename Refcount, typename Return>
	void
	release(ref_t p, std::size_t nrefs,
	    Return (AcqRel::*)(ref_t, Refcount) = &AcqRel::release)
	const noexcept
	{
		if (nrefs > 0)
			this->m_acqrel.release(p, nrefs);
	}

	/*
	 * Fallback implementation of acquire, if the SFINAE above fails.
	 *
	 * To acquire N references, N calls to AcqRel::acquire are performed.
	 */
	void
	acquire(ref_t p, std::size_t nrefs, ...) const noexcept
	{
		while (nrefs-- > 0)
			this->m_acqrel.acquire(p);
	}

	/*
	 * Fallback implementation of release, if the SFINAE above fails.
	 *
	 * To release N references, N calls to AcqRel::release are performed.
	 */
	void
	release(ref_t p, std::size_t nrefs, ...) const noexcept
	{
		while (nrefs-- > 0)
			this->m_acqrel.release(p);
	}
};


} /* namespace ilias::llptr_detail */


namespace {


/*
 * Implement an atomic pointer that is tagged with flags.
 *
 * The flags use the low bits of the pointer, hence the pointer
 * must be aligned to a multiple of 2^Flags.
 */
template<typename Type, unsigned int Flags = 0U>
class atomic_flag_ptr
{
public:
	using flags_type = std::bitset<Flags>;
	using pointer = Type*;
	using element_type = std::tuple<pointer, flags_type>;

private:
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

	static pointer
	_ptr(std::uintptr_t v) noexcept
	{
		return reinterpret_cast<pointer>(v & _mask());
	}

	static flags_type
	_bitmask(std::uintptr_t v) noexcept
	{
		return flags_type{ v & _flags() };
	}

	static std::uintptr_t
	_encode(pointer p, const flags_type& fl) noexcept
	{
		static_assert(std::alignment_of<Type>::value % (1U << Flags) ==
		    0U,
		    "llptr: type alignment must be greater than 1<<Flags.");

		return (reinterpret_cast<std::uintptr_t>(p) |
		    fl.to_ulong());
	}

	static std::uintptr_t
	_encode(const std::tuple<pointer, flags_type>& v) noexcept
	{
		return _encode(std::get<0>(v), std::get<1>(v));
	}

	static element_type
	_decode(std::uintptr_t v) noexcept
	{
		return element_type{ _ptr(v), _bitmask(v) };
	}

public:
	atomic_flag_ptr() = default;
	atomic_flag_ptr(const atomic_flag_ptr&) = delete;
	atomic_flag_ptr(atomic_flag_ptr&&) = delete;
	atomic_flag_ptr& operator=(const atomic_flag_ptr&) = delete;

	atomic_flag_ptr(element_type v) noexcept
	:	m_ptr{ _encode(v) }
	{
		/* Empty body. */
	}

	atomic_flag_ptr(std::nullptr_t) noexcept
	{
		/* Empty body. */
	}

	element_type
	load(std::memory_order mo = std::memory_order_seq_cst) const noexcept
	{
		return _decode(this->m_ptr.load(mo));
	}

	void
	store(element_type v,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		this->m_ptr.store(_encode(v), mo);
	}

	element_type
	exchange(element_type v,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		return _decode(this->m_ptr.exchange(_encode(v), mo));
	}

	bool
	compare_exchange_strong(element_type& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail) noexcept
	{
		auto expect_ = _encode(expect);
		bool rv = this->m_ptr.compare_exchange_strong(expect_,
		    _encode(set), mo_success, mo_fail);
		if (!rv)
			expect = _decode(expect_);
		return rv;
	}

	bool
	compare_exchange_strong(element_type& expect, element_type set,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		auto expect_ = _encode(expect);
		bool rv = this->m_ptr.compare_exchange_strong(expect_,
		    _encode(set), mo);
		if (!rv)
			expect = _decode(expect_);
		return rv;
	}

	bool
	compare_exchange_weak(element_type& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail) noexcept
	{
		auto expect_ = _encode(expect);
		bool rv = this->m_ptr.compare_exchange_weak(expect_,
		    _encode(set), mo_success, mo_fail);
		if (!rv)
			expect = _decode(expect_);
		return rv;
	}

	bool
	compare_exchange_weak(element_type& expect, element_type set,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		auto expect_ = _encode(expect);
		bool rv = this->m_ptr.compare_exchange_weak(expect_,
		    _encode(set), mo);
		if (!rv)
			expect = _decode(expect_);
		return rv;
	}

	void
	reset(std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		this->m_ptr.store(0U, mo);
	}

	atomic_flag_ptr&
	operator=(element_type v) noexcept
	{
		this->store(std::move(v));
		return *this;
	}

	atomic_flag_ptr&
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
		return this->m_ptr.is_lock_free();
	}

	friend bool
	atomic_is_lock_free(const atomic_flag_ptr* p) noexcept
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
};


} /* namespace ilias::<unnamed> */


/*
 * Atomic pointer for reference counted type with flags.
 *
 * Interfaces as std::atomic<std::tuple<refpointer<Type>, std::bitset<Flags>>>.
 */
template<typename Type, typename AcqRel = default_refcount_mgr<Type>,
    unsigned int Flags = 0U>
class llptr
:	private llptr_detail::acqrel_helper<Type, AcqRel>
{
private:
	using impl_type = atomic_flag_ptr<Type, Flags>;

public:
	using flags_type = typename impl_type::flags_type;
	using simple_pointer = typename impl_type::pointer;
	using pointer = refpointer<Type, AcqRel>;
	using element_type = std::tuple<pointer, flags_type>;
	using no_acquire_t = typename impl_type::element_type;

private:
	using hazard_t = hazard<llptr, Type>;
	using ptr_t = Type*;

	impl_type m_impl;

	static typename impl_type::element_type
	convert_release(element_type v) noexcept
	{
		using rv_type = typename impl_type::element_type;

		return rv_type{ std::get<0>(v).release(), std::get<1>(v) };
	}

	static typename impl_type::element_type
	convert_get(const element_type& v) noexcept
	{
		using rv_type = typename impl_type::element_type;

		return rv_type{ std::get<0>(v).get(), std::get<1>(v) };
	}

	static element_type
	convert_acquire(const typename impl_type::element_type& v, bool acq)
	noexcept
	{
		return element_type{
			pointer{ std::get<0>(v), acq },
			std::get<1>(v)
		};
	}

	/*
	 * Grant references to readouts in progress.
	 */
	void
	grant(ptr_t p, std::size_t nrefs) const noexcept
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

	/*
	 * Perform hazard part of acquisition algorithm.
	 */
	unsigned int
	do_hazard(hazard_t& hz, ptr_t v_ptr) const noexcept
	{
		if (!v_ptr)
			return 0U;

		unsigned int rv = 0U;
		hz.do_hazard(*v_ptr,
		    [&rv, this, v_ptr]() {
			if (_ptr(this->m_ptr.load(
			    std::memory_order_relaxed)) == v_ptr) {
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
	:	m_impl{ convert_release(std::move(v)) }
	{
		/* Empty body. */
	}

	llptr(std::nullptr_t) noexcept
	{
		/* Empty body. */
	}

	~llptr() noexcept
	{
		this->exchange(0U, std::memory_order_acquire);
	}

	element_type
	load(std::memory_order mo = std::memory_order_seq_cst) const noexcept
	{
		auto v = this->m_impl.load(mo);
		if (std::get<0>(v) == nullptr)
			return std::make_tuple(nullptr, std::get<1>(v));

		hazard_t hz{ *this };

		auto acq = this->do_hazard(hz, std::get<0>(v));
		while (acq == 0 && std::get<0>(v) != nullptr) {
			v = this->m_impl.load(mo);
			acq = this->do_hazard(hz, std::get<0>(v));
		}

		if (acq > 1U)
			this->release(*_ptr(v), acq - 1U);

		return convert_acquire(v, false);
	}

	void
	store(element_type v, std::memory_order mo = std::memory_order_seq_cst)
	noexcept
	{
		const auto p = std::get<0>(
		    this->m_impl.exchange(convert_release(v), mo));
		if (p) {
			std::atomic_thread_fence(std::memory_order_acquire);
			this->grant(p, 1);
		}
	}

	element_type
	exchange(element_type v,
	    std::memory_order mo = std::memory_order_seq_cst)
	noexcept
	{
		const auto p = this->m_impl.exchange(convert_release(v), mo);
		this->grant(std::get<0>(p), 0);
		return convert_acquire(p, false);
	}

	bool
	compare_exchange_weak(no_acquire_t& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		if (!this->m_impl.compare_exchange_weak(expect,
		    convert_get(set), mo_success, mo_fail))
			return false;

		std::get<0>(set).release();
		this->grant(std::get<0>(expect), 1);
		return true;
	}

	bool
	compare_exchange_weak(no_acquire_t&& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		return this->compare_exchange_weak(expect, std::move(set),
		    mo_success, mo_fail);
	}

	bool
	compare_exchange_weak(element_type& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		auto expect_ = convert_get(expect);
		if (this->compare_exchange_weak(expect_, std::move(set),
		    mo_success, mo_fail))
			return true;

		/*
		 * Skip hazard logic if there is no need to acquire
		 * the reference.
		 */
		if (std::get<0>(expect_) == std::get<0>(expect).get()) {
			expect = std::make_tuple(
			    std::move(std::get<0>(expect)),
			    std::get<1>(expect_));
			return false;
		}
		if (std::get<0>(expect_) == nullptr) {
			expect = std::make_tuple(
			    nullptr,
			    std::get<1>(expect_));
			return false;
		}

		hazard_t hz{ *this };
		std::size_t acq;
		do {
			acq = this->do_hazard(hz, std::get<0>(expect_));
		} while (acq == 0 &&
		    std::get<0>(expect_ = this->m_impl.load(mo_fail)) !=
		    nullptr);

		if (acq > 1)
			this->release(std::get<0>(expect_), acq - 1U);
		expect = convert_acquire(expect_, false);
		return false;
	}

	bool
	compare_exchange_weak(element_type&& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		auto expect_ = convert_get(expect);
		if (!this->m_impl.compare_exchange_weak(expect_,
		    convert_get(set), mo_success, mo_fail))
			return false;

		std::get<0>(set).release();
		this->grant(std::get<0>(expect).release(), 2);
		return true;
	}

	bool
	compare_exchange_strong(no_acquire_t& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		if (!this->m_impl.compare_exchange_strong(expect,
		    convert_get(set), mo_success, mo_fail))
			return false;

		std::get<0>(set).release();
		this->grant(std::get<0>(expect), 1);
		return true;
	}

	bool
	compare_exchange_strong(no_acquire_t&& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		return this->compare_exchange_strong(expect, std::move(set),
		    mo_success, mo_fail);
	}

	bool
	compare_exchange_strong(element_type& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		no_acquire_t expect_;

		while (!this->m_impl.compare_exchange_strong(
		    expect_ = convert_get(expect),
		    convert_get(set),
		    mo_success, mo_fail)) {
			if (this->compare_exchange_strong(expect_,
			    std::move(set),
			    mo_success, mo_fail))
				return true;

			/*
			 * Skip hazard logic if there is no need to acquire
			 * the reference.
			 */
			if (std::get<0>(expect_) ==
			    std::get<0>(expect).get()) {
				expect = std::make_tuple(
				    std::move(std::get<0>(expect)),
				    std::get<1>(expect_));
				return false;
			}
			if (std::get<0>(expect_) == nullptr) {
				expect = std::make_tuple(
				    nullptr,
				    std::get<1>(expect_));
				return false;
			}

			/*
			 * Use hazard logic to obtain reference to
			 * expect_ pointer
			 * (iff expect_ differs from expect).
			 */
			hazard_t hz{ *this };
			do {
				auto acq = this->do_hazard(hz,
				    std::get<0>(expect_));
				if (acq != 0 ||
				    std::get<0>(expect_) == nullptr) {
					if (acq > 1U) {
						this->release(
						    std::get<0>(expect_),
						    acq - 1U);
					}
					expect = convert_acquire(expect_,
					    false);
					return false;
				}
			} while (convert_get(expect) !=
			    (expect_ = this->m_impl.load(mo_fail)));
		}

		std::get<0>(set).release();
		this->grant(std::get<0>(expect).get(), 1);
		return true;
	}

	bool
	compare_exchange_strong(element_type&& expect, element_type set,
	    std::memory_order mo_success, std::memory_order mo_fail)
	noexcept
	{
		auto expect_ = convert_get(expect);
		if (!this->m_impl.compare_exchange_strong(expect_,
		    convert_get(set), mo_success, mo_fail))
			return false;

		std::get<0>(set).release();
		this->grant(std::get<0>(expect).release(), 2);
		return true;
	}

	void
	reset(std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		this->store(std::make_tuple(nullptr, flags_type{}), mo);
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
		return atomic_is_lock_free(&this->m_impl) &&
		    atomic_is_lock_free(&hz);
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
		return this->m_impl.fetch_or(fl, mo);
	}

	flags_type
	fetch_xor(flags_type fl,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		return this->m_impl.fetch_xor(fl, mo);
	}

	flags_type
	fetch_and(flags_type fl,
	    std::memory_order mo = std::memory_order_seq_cst) noexcept
	{
		return this->m_impl.fetch_and(fl, mo);
	}

	flags_type
	load_flags(std::memory_order mo = std::memory_order_seq_cst)
	const noexcept
	{
		return this->m_impl.load_flags(mo);
	}

	no_acquire_t
	load_no_acquire(std::memory_order mo = std::memory_order_seq_cst) const
	noexcept
	{
		return this->m_impl.load(mo);
	}
};


} /* namespace ilias */

#endif /* ILIAS_LLPTR_H */
