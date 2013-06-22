#ifndef ILIAS_LL_LIST_H
#define ILIAS_LL_LIST_H

#include <ilias/ilias_async_export.h>
#include <ilias/llptr.h>
#include <ilias/refcnt.h>


namespace ilias {
namespace ll_list_detail {


enum class elem_type : unsigned char {
	HEAD,
	ELEM,
	ITER_FWD,
	ITER_BACK
};

class simple_elem;
using elem_refcnt = unsigned short;

struct simple_elem_acqrel
{
	inline void acquire(const simple_elem&, elem_refcnt = 1U) const
	    noexcept;
	inline void release(const simple_elem&, elem_refcnt = 1U) const
	    noexcept;
};

using simple_elem_ptr = llptr<simple_elem, simple_elem_acqrel, 1U>;
using flags_type = typename simple_elem_ptr::flags_type;
using simple_ptr = typename simple_elem_ptr::pointer;
using simple_elem_range = std::tuple<simple_ptr, simple_ptr>;

constexpr flags_type DELETED{ 1U };
constexpr flags_type PRESENT{ 0U };

template<typename Pointer>
std::tuple<Pointer, flags_type>
add_present(Pointer p) noexcept
{
	return std::make_tuple(p, PRESENT);
}

template<typename Pointer>
std::tuple<Pointer, flags_type>
add_delete(Pointer p) noexcept
{
	return std::make_tuple(p, DELETED);
}


class simple_elem
{
friend struct simple_elem_acqrel;

protected:
	mutable simple_elem_ptr m_succ{ add_present(this) },
	    m_pred{ add_present(this) };
	mutable std::atomic<elem_refcnt> m_refcnt{ 0U };

private:
	elem_refcnt
	unused_refcnt(std::memory_order mo = std::memory_order_seq_cst) const
	noexcept
	{
		return (std::get<0>(this->m_succ.load(mo)) == this ? 2U : 0U);
	}

	void
	wait_unused() const noexcept
	{
		elem_refcnt min, refs;
		do {
			refs = this->m_refcnt.load(std::memory_order_acquire);
			min = this->unused_refcnt(std::memory_order_relaxed);
		} while (refs != min);
	}

public:
	simple_elem() = default;

	~simple_elem() noexcept
	{
		this->wait_unused();
	}

private:
	class prepare_store;

public:
	ILIAS_ASYNC_EXPORT simple_elem_ptr::element_type succ() const noexcept;
	ILIAS_ASYNC_EXPORT simple_elem_ptr::element_type pred() const noexcept;
	ILIAS_ASYNC_EXPORT static bool link(simple_elem_range ins,
	    simple_elem_range between)
	    noexcept;
	ILIAS_ASYNC_EXPORT static bool link_after(simple_elem_range ins,
	    simple_ptr pred) noexcept;
	ILIAS_ASYNC_EXPORT static bool link_before(simple_elem_range ins,
	    simple_ptr succ) noexcept;
	ILIAS_ASYNC_EXPORT bool unlink() noexcept;

	static bool
	link_after(simple_ptr ins, simple_ptr pred) noexcept
	{
		simple_elem_range r;
		std::get<0>(r) = ins;
		std::get<1>(r) = std::move(ins);
		return link_after(std::move(r), std::move(pred));
	}

	static bool
	link_before(simple_ptr ins, simple_ptr succ) noexcept
	{
		simple_elem_range r;
		std::get<0>(r) = ins;
		std::get<1>(r) = std::move(ins);
		return link_before(std::move(r), std::move(succ));
	}
};

inline void
simple_elem_acqrel::acquire(const simple_elem& e, elem_refcnt nrefs) const
noexcept
{
	if (nrefs > 0) {
		e.m_refcnt.fetch_add(nrefs,
		    std::memory_order_acquire);
	}
}

inline void
simple_elem_acqrel::release(const simple_elem& e, elem_refcnt nrefs) const
noexcept
{
	if (nrefs == 0)
		return;

	auto r = e.m_refcnt.load(std::memory_order_relaxed);
	do {
		assert(r >= nrefs);
		if (r == nrefs) {
			if (nrefs < 2U) {
				e.m_refcnt.fetch_add(2U - nrefs,
				    std::memory_order_acquire);
				nrefs = 2U;
			}

			auto pred_store = add_present(simple_ptr{
				const_cast<simple_elem*>(&e),
				false
			    });
			auto succ_store = add_present(simple_ptr{
				const_cast<simple_elem*>(&e),
				false
			    });
			nrefs -= 2U;

			e.m_pred.store(std::move(pred_store),
			    std::memory_order_release);
			e.m_succ.store(std::move(succ_store),
			    std::memory_order_release);

			if (nrefs > 0U) {
				e.m_refcnt.fetch_sub(nrefs,
				    std::memory_order_release);
			} else {
				std::atomic_thread_fence(
				    std::memory_order_release);
			}
			return;
		}
	} while (!e.m_refcnt.compare_exchange_weak(r, r - nrefs,
	    std::memory_order_release,
	    std::memory_order_relaxed));
}


} /* namespace ilias::ll_list_detail */




} /* namespace ilias */


#endif /* ILIAS_LL_LIST_H */
