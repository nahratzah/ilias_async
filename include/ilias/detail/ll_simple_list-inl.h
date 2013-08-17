#ifndef ILIAS_DETAIL_LL_SIMPLE_LIST_INL
#define ILIAS_DETAIL_LL_SIMPLE_LIST_INL

#include <algorithm>
#include <functional>
#include <cassert>

namespace ilias {
namespace ll_list_detail {
namespace ll_simple_list {


template<typename T>
std::tuple<
    typename std::remove_const<typename std::remove_reference<T>::type>::type,
    flags_t>
add_present(T&& v) noexcept
{
	using result_type = std::tuple<
	    typename std::remove_const<
	     typename std::remove_reference<T>::type>::type,
	    flags_t>;

	return result_type{ std::forward<T>(v), PRESENT };
}

template<typename T>
std::tuple<
    typename std::remove_const<typename std::remove_reference<T>::type>::type,
    flags_t>
add_deleted(T&& v) noexcept
{
	using result_type = std::tuple<
	    typename std::remove_const<
	     typename std::remove_reference<T>::type>::type,
	    flags_t>;

	return result_type{ std::forward<T>(v), DELETED };
}


inline void
elem_refcnt_mgr::acquire(const elem& e,
    refcount_t nrefs)
{
	refcount_t r = e.m_refcnt_.fetch_add(nrefs, std::memory_order_acquire);
	assert(r + nrefs >= r);
}

inline void
elem_refcnt_mgr::release(const elem& e,
    refcount_t nrefs)
{
	refcount_t r = e.m_refcnt_.fetch_sub(nrefs, std::memory_order_release);
	assert(r >= nrefs);

	if (r == nrefs) {
		auto assign = add_present(elem_ptr{ const_cast<elem*>(&e) });
		e.m_pred_.store(assign, std::memory_order_release);
		e.m_succ_.store(std::move(assign), std::memory_order_release);
	}
}


inline elem::elem() noexcept
:	m_refcnt_{ 0U },
	m_pred_{ add_present(this) },
	m_succ_{ add_present(this) }
{
	/* Empty body. */
}

inline elem::elem(const elem&) noexcept
:	elem{}
{
	/* Empty body. */
}

inline elem::elem(elem&&) noexcept
:	elem{}
{
	/* Empty body. */
}

inline elem::~elem() noexcept
{
	this->wait_unused();

	/*
	 * Increment refcounter,
	 * to block reset operation in elem_refcnt_mgr.
	 */
	this->m_refcnt_.fetch_add(1U, std::memory_order_acquire);
}

inline bool
elem::is_linked() const noexcept
{
	auto p = this->m_pred_.load_no_acquire(std::memory_order_acquire);
	auto s = this->m_succ_.load_no_acquire(std::memory_order_acquire);

	return
	    (std::get<0>(p) != this && std::get<1>(p) == PRESENT) ||
	    (std::get<0>(s) != this && std::get<1>(s) == PRESENT);
}

inline bool
elem::is_unused() const noexcept
{
	return
	    (this->m_pred_.load_no_acquire(std::memory_order_acquire) ==
	     add_present(this)) &&
	    (this->m_succ_.load_no_acquire(std::memory_order_acquire) ==
	     add_present(this)) &&
	    (this->m_refcnt_.load(std::memory_order_acquire) == 2U);
}

inline link_result
elem::link_between(std::tuple<elem*, elem*> ins,
    std::tuple<elem_ptr, elem_ptr> pos)
{
	if (std::get<0>(ins) == nullptr || std::get<1>(ins) == nullptr ||
	    std::get<0>(pos) == nullptr || std::get<1>(pos) == nullptr)
		throw std::invalid_argument("link_between: null argument");

	return link_between_(std::move(ins), std::move(pos));
}

inline link_result
elem::link_before(std::tuple<elem*, elem*> ins,
    elem_ptr pos)
{
	if (std::get<0>(ins) == nullptr || std::get<1>(ins) == nullptr ||
	    pos == nullptr)
		throw std::invalid_argument("link_before: null argument");

	return link_before_(std::move(ins), std::move(pos));
}

inline link_result
elem::link_after(std::tuple<elem*, elem*> ins,
    elem_ptr pos)
{
	if (std::get<0>(ins) == nullptr || std::get<1>(ins) == nullptr ||
	    pos == nullptr)
		throw std::invalid_argument("link_after: null argument");

	return link_after_(std::move(ins), std::move(pos));
}

inline link_result
elem::link_between(elem* e, std::tuple<elem_ptr, elem_ptr> pos)
{
	e->wait_unlinked();
	return link_between(std::make_tuple(e, e), std::move(pos));
}

inline link_result
elem::link_before(elem* e, elem_ptr pos)
{
	e->wait_unlinked();
	return link_before(std::make_tuple(e, e), std::move(pos));
}

inline link_result
elem::link_after(elem* e, elem_ptr pos)
{
	e->wait_unlinked();
	return link_after(std::make_tuple(e, e), std::move(pos));
}

inline link_result
elem::link_between(elem_range&& r,
    std::tuple<elem_ptr, elem_ptr> pos)
{
	if (r.empty())
		return link_result::SUCCESS;

	elem_range::release_ rel{ r };
	link_result rv = link_between(rel.get(), std::move(pos));
	if (rv == link_result::SUCCESS)
		rel.commit();
	return rv;
}

inline link_result
elem::link_before(elem_range&& r,
    elem_ptr pos)
{
	if (r.empty())
		return link_result::SUCCESS;

	elem_range::release_ rel{ r };
	link_result rv = link_between(rel.get(), std::move(pos));
	if (rv == link_result::SUCCESS)
		rel.commit();
	return rv;
}

inline link_result
elem::link_after(elem_range&& r,
    elem_ptr pos)
{
	if (r.empty())
		return link_result::SUCCESS;

	elem_range::release_ rel{ r };
	link_result rv = link_after(rel.get(), std::move(pos));
	if (rv == link_result::SUCCESS)
		rel.commit();
	return rv;
}


inline elem_range::elem_range(elem_range&& other) noexcept
:	elem_range{}
{
	other.push_front(&this->m_self_);
	other.m_self_.unlink();
}

template<typename Iter>
inline elem_range::elem_range(Iter b, Iter e)
{
	using namespace std::placeholders;
	using std::for_each;

	for_each(std::move(b), std::move(e),
	    std::bind(&elem_range::push_back, this, _1));
}

inline elem_range::~elem_range() noexcept
{
	for (elem_ptr e = std::get<0>(this->m_self_.succ());
	    e != &this->m_self_;
	    e = std::get<0>(e->succ()))
		e->unlink();
	assert(this->empty());
}

inline void
elem_range::push_front(elem* e)
{
	if (e == nullptr)
		throw std::invalid_argument("elem_range: null element");
	if (!e->wait_unlinked())
		throw std::invalid_argument("elem_range: linked element");

	auto rv = elem::link_after(std::make_tuple(e, e),
	    elem_ptr{ &this->m_self_ });
	switch (rv) {
	case link_result::SUCCESS:
		return;
	case link_result::INS0_LINKED:
	case link_result::INS1_LINKED:
		throw std::invalid_argument("elem_range: linked element");
	default:
		/* UNREACHABLE */
		assert(false);
		for (;;);
	}
}

inline void
elem_range::push_back(elem* e)
{
	if (e == nullptr)
		throw std::invalid_argument("elem_range: null element");
	if (!e->wait_unlinked())
		throw std::invalid_argument("elem_range: linked element");

	auto rv = elem::link_before(std::make_tuple(e, e),
	    elem_ptr{ &this->m_self_ });
	switch (rv) {
	case link_result::SUCCESS:
		return;
	case link_result::INS0_LINKED:
	case link_result::INS1_LINKED:
		throw std::invalid_argument("elem_range: linked element");
	default:
		/* UNREACHABLE */
		assert(false);
		for (;;);
	}
}

inline bool
elem_range::empty() const noexcept
{
	return (std::get<0>(this->m_self_.succ()) == &this->m_self_);
}


inline elem_range::release_::release_(elem_range& r) noexcept
:	self_{ r },
	commited_{ false }
{
	std::get<0>(this->data_) = std::get<0>(
	    this->self_.m_self_.m_succ_.exchange(
	     add_present(elem_ptr{ &this->self_.m_self_ }),
	     std::memory_order_acquire));
	std::get<1>(this->data_) = std::get<0>(
	    this->self_.m_self_.m_pred_.exchange(
	     add_present(elem_ptr{ &this->self_.m_self_ }),
	     std::memory_order_acquire));

	if (std::get<0>(this->data_) == std::get<1>(this->data_)) {
		assert(std::get<0>(this->data_) == &r.m_self_);

		this->data_ = std::make_tuple(nullptr, nullptr);
	} else {
		assert(std::get<0>(this->data_) != &r.m_self_);
		assert(std::get<1>(this->data_) != &r.m_self_);

		std::get<0>(this->data_)->m_pred_.store(
		    add_present(std::get<0>(this->data_)),
		    std::memory_order_relaxed);
		std::get<1>(this->data_)->m_succ_.store(
		    add_present(std::get<1>(this->data_)),
		    std::memory_order_relaxed);
	}
}

inline elem_range::release_::~release_() noexcept
{
	if (!this->commited_ &&
	    this->data_ != std::make_tuple(nullptr, nullptr)) {
		auto rv = elem::link_after(this->get(),
		    elem_ptr{ &this->self_.m_self_ });
		assert(rv == link_result::SUCCESS);
	}
}

inline std::tuple<elem*, elem*>
elem_range::release_::get() const noexcept
{
	assert(this->data_ != std::make_tuple(nullptr, nullptr));
	return std::tuple<elem*, elem*>{
		std::get<0>(this->data_).get(),
		std::get<1>(this->data_).get()
	    };
}

inline void
elem_range::release_::commit() noexcept
{
	assert(this->data_ != std::make_tuple(nullptr, nullptr));
	assert(!this->commited_);
	this->commited_ = true;
}


}}} /* namespace ilias::ll_list_detail::ll_simple_list */

#endif /* ILIAS_DETAIL_LL_SIMPLE_LIST_INL */
