#ifndef ILIAS_LL_LIST_INL
#define ILIAS_LL_LIST_INL

#include <ilias/util.h>
#include <cassert>
#include <stdexcept>

namespace ilias {


template<typename Tag>
inline std::size_t
ll_list_hook<Tag>::_elem_offset() noexcept
{
	/*
	 * Calculate offset of elem_ member
	 * (since offsetof is technically undefined behaviour and
	 * triggers compiler warnings). :(
	 */
	constexpr std::uintptr_t ADDR = 0x1000;
	return reinterpret_cast<std::uintptr_t>(
	    &reinterpret_cast<ll_list_hook*>(ADDR)->elem_) - ADDR;
}


namespace ll_list_detail {


inline elem::elem(elem_type et) noexcept
:	type_{ et }
{
	/* Empty body. */
}

inline elem::~elem() noexcept
{
	this->unlink();
}

inline elem_ptr
elem::succ() const noexcept
{
	return static_pointer_cast<elem>(std::get<0>(
	    this->ll_simple_list::elem::succ()));
}

inline elem_ptr
elem::pred() const noexcept
{
	return static_pointer_cast<elem>(std::get<0>(
	    this->ll_simple_list::elem::pred()));
}

template<std::size_t N>
elem_ptr
elem::succ(const std::array<elem_type, N>& types) const noexcept
{
	using std::begin;
	using std::end;

	elem_ptr i = this->succ();
	while (std::find(begin(types), end(types), i->get_type()) ==
	    end(types))
		i = i->succ();
	return i;
}

template<std::size_t N>
elem_ptr
elem::pred(const std::array<elem_type, N>& types) const noexcept
{
	using std::begin;
	using std::end;

	elem_ptr i = this->pred();
	while (std::find(begin(types), end(types), i->get_type()) ==
	    end(types))
		i = i->pred();
	return i;
}

inline bool
elem::is_head() const noexcept
{
	return (this->get_type() == elem_type::HEAD);
}

inline bool
elem::is_elem() const noexcept
{
	return (this->get_type() == elem_type::ELEM);
}

inline bool
elem::is_iter() const noexcept
{
	return (this->is_forw_iter() || this->is_back_iter());
}

inline bool
elem::is_forw_iter() const noexcept
{
	return (this->get_type() == elem_type::ITER_FORW);
}

inline bool
elem::is_back_iter() const noexcept
{
	return (this->get_type() == elem_type::ITER_BACK);
}

inline elem_type
elem::get_type() const noexcept
{
	return this->type_;
}


inline basic_list::basic_list() noexcept
:	head_{ elem_type::HEAD }
{
	/* Empty body. */
}

inline bool
basic_list::push_front(elem* e)
{
	if (e == nullptr)
		throw std::invalid_argument("null element");

	return do_noexcept([&]() {
		auto rv = elem::link_after(e,
		    elem_ptr{ &this->head_ });
		assert(rv != ll_simple_list::link_result::INVALID_POS);
		return (rv == ll_simple_list::link_result::SUCCESS);
	    });
}

inline bool
basic_list::push_back(elem* e)
{
	if (e == nullptr)
		throw std::invalid_argument("null element");

	return do_noexcept([&]() {
		auto rv = elem::link_before(e,
		    elem_ptr{ &this->head_ });
		assert(rv != ll_simple_list::link_result::INVALID_POS);
		return (rv == ll_simple_list::link_result::SUCCESS);
	    });
}

inline bool
basic_list::insert_after(elem* ins, const basic_iter& pos, basic_iter* out)
{
	if (ins == nullptr)
		throw std::invalid_argument("null insertion");
	if (pos.owner_ != this)
		throw std::invalid_argument("invalid iterator");
	if (out == &pos) {
		throw std::invalid_argument("insert position and output "
		    "iterator may not be the same (use a temporary!)");
	}

	return this->insert_after_(ins, &pos.forw_, out);
}

inline bool
basic_list::insert_before(elem* ins, const basic_iter& pos, basic_iter* out)
{
	if (ins == nullptr)
		throw std::invalid_argument("null insertion");
	if (pos.owner_ != this)
		throw std::invalid_argument("invalid iterator");
	if (out == &pos) {
		throw std::invalid_argument("insert position and output "
		    "iterator may not be the same (use a temporary!)");
	}

	return this->insert_before_(ins, &pos.back_, out);
}


inline basic_iter::basic_iter() noexcept
:	owner_{ nullptr },
	forw_{ elem_type::ITER_FORW },
	back_{ elem_type::ITER_BACK }
{
	/* Empty body. */
}

inline basic_iter::basic_iter(const basic_iter& other) noexcept
:	basic_iter{}
{
	if ((this->owner_ = other.owner_) != nullptr) {
		ll_simple_list::elem::link_after(&this->forw_,
		    elem_ptr{ const_cast<elem*>(&other.forw_) });
		ll_simple_list::elem::link_before(&this->back_,
		    elem_ptr{ const_cast<elem*>(&other.back_) });
	}
}

inline basic_iter::basic_iter(basic_iter&& other) noexcept
:	basic_iter{}
{
	if ((this->owner_ = other.owner_) != nullptr) {
		ll_simple_list::elem::link_after(&this->forw_,
		    elem_ptr{ &other.forw_ });
		ll_simple_list::elem::link_before(&this->back_,
		    elem_ptr{ &other.back_ });
		other.unlink();
	}
}

inline basic_iter&
basic_iter::operator=(const basic_iter& other) noexcept
{
	this->forw_.unlink();
	this->back_.unlink();

	if ((this->owner_ = other.owner_) != nullptr) {
		ll_simple_list::elem::link_after(&this->forw_,
		    elem_ptr{ const_cast<elem*>(&other.forw_) });
		ll_simple_list::elem::link_before(&this->back_,
		    elem_ptr{ const_cast<elem*>(&other.back_) });
	}

	return *this;
}

inline basic_iter&
basic_iter::operator=(basic_iter&& other) noexcept
{
	this->forw_.unlink();
	this->back_.unlink();

	if ((this->owner_ = other.owner_) != nullptr) {
		ll_simple_list::elem::link_after(&this->forw_,
		    elem_ptr{ const_cast<elem*>(&other.forw_) });
		ll_simple_list::elem::link_before(&this->back_,
		    elem_ptr{ const_cast<elem*>(&other.back_) });
		other.unlink();
	}

	return *this;
}

inline elem_ptr
basic_iter::link_at(basic_list* list, tag where)
{
	constexpr std::array<elem_type, 2> types{{
		elem_type::HEAD,
		elem_type::ELEM
	    }};

	elem_ptr f;
	switch (where) {
	case tag::first:
		f = list->head_.succ(types);
		break;
	case tag::last:
		f = list->head_.pred(types);
		break;
	case tag::head:
		f = &list->head_;
		break;
	}

	while (!this->link_at(list, f)) {
		switch (where) {
		case tag::first:
			f = f->succ(types);
			break;
		case tag::last:
			f = f->pred(types);
			break;
		case tag::head:
			/* Retry at head. */
			break;
		}
	}

	return f;
}

inline bool
basic_iter::link_at(basic_list* list, elem_ptr pos)
{
	if (list == nullptr)
		throw std::invalid_argument("null list");
	if (pos == nullptr)
		throw std::invalid_argument("null position");

	return this->link_at_(list, std::move(pos));
}

inline bool
basic_iter::unlink() noexcept
{
	if (this->owner_) {
		this->forw_.unlink();
		this->back_.unlink();
		this->owner_ = nullptr;
		return true;
	} else
		return false;
}


template<typename Type, typename Tag, typename AcqRel>
inline elem_ptr
ll_list_transformations<Type, Tag, AcqRel>::as_elem_(
    const typename ll_list_transformations<Type, Tag, AcqRel>::pointer& p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	hook_type& hook = *p;
	return elem_ptr{ &hook.elem_ };
}

template<typename Type, typename Tag, typename AcqRel>
inline const_elem_ptr
ll_list_transformations<Type, Tag, AcqRel>::as_elem_(
    const typename ll_list_transformations<Type, Tag, AcqRel>::const_pointer&
     p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	const hook_type& hook = *p;
	return const_elem_ptr{ &hook.elem_ };
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_list_transformations<Type, Tag, AcqRel>::pointer
ll_list_transformations<Type, Tag, AcqRel>::as_type_(const elem_ptr& p)
noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	pointer rv;
	hazard_t hz{ *this };

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	hook_type& hook = *reinterpret_cast<hook_type*>(
	    reinterpret_cast<unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	reference value = static_cast<reference>(hook);

	hz.do_hazard(value,
	    [&]() {
		if (p->is_linked())
			rv = pointer{ &value };
	    },
	    [&]() {
		rv = pointer{ &value, false };
	    });
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_list_transformations<Type, Tag, AcqRel>::const_pointer
ll_list_transformations<Type, Tag, AcqRel>::as_type_(
    const const_elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	const_pointer rv;
	hazard_t hz{ *this };

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	const hook_type& hook = *reinterpret_cast<const hook_type*>(
	    reinterpret_cast<const unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	const_reference value = static_cast<const_reference>(hook);

	hz.do_hazard(value,
	    [&]() {
		if (p->is_linked())
			rv = const_pointer{ &value };
	    },
	    [&]() {
		rv = const_pointer{ &value, false };
	    });
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_list_transformations<Type, Tag, AcqRel>::pointer
ll_list_transformations<Type, Tag, AcqRel>::as_type_unlinked_(
    const elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	hook_type& hook = *reinterpret_cast<hook_type*>(
	    reinterpret_cast<unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	reference value = static_cast<reference>(hook);

	return pointer{ &value };
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_list_transformations<Type, Tag, AcqRel>::const_pointer
ll_list_transformations<Type, Tag, AcqRel>::as_type_unlinked_(
    const const_elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	const hook_type& hook = *reinterpret_cast<const hook_type*>(
	    reinterpret_cast<const unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	const_reference value = static_cast<const_reference>(hook);

	return const_pointer{ &value };
}

template<typename Type, typename Tag, typename AcqRel>
inline void
ll_list_transformations<Type, Tag, AcqRel>::post_unlink_(
    ll_list_transformations<Type, Tag, AcqRel>::const_reference unlinked_value,
    std::size_t uv_refs) const noexcept
{
	hazard_t::grant(
	    [&unlinked_value](std::size_t nrefs) {
		AcqRel::acquire(unlinked_value, nrefs);
	    },
	    [&unlinked_value](std::size_t nrefs) {
		AcqRel::release(unlinked_value, nrefs);
	    },
	    *this, unlinked_value, uv_refs);
}

template<typename Type, typename Tag, typename AcqRel>
inline void
ll_list_transformations<Type, Tag, AcqRel>::release_pointer(
    ll_list_transformations<Type, Tag, AcqRel>::pointer& p) noexcept
{
	p.release();
}

template<typename Type, typename Tag, typename AcqRel>
inline void
ll_list_transformations<Type, Tag, AcqRel>::release_pointer(
    ll_list_transformations<Type, Tag, AcqRel>::const_pointer& p) noexcept
{
	p.release();
}


template<typename Type, typename Tag>
inline elem_ptr
ll_list_transformations<Type, Tag, no_acqrel>::as_elem_(
    typename ll_list_transformations<Type, Tag, no_acqrel>::pointer p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	hook_type& hook = *p;
	return elem_ptr{ &hook.elem_ };
}

template<typename Type, typename Tag>
inline const_elem_ptr
ll_list_transformations<Type, Tag, no_acqrel>::as_elem_(
    typename ll_list_transformations<Type, Tag, no_acqrel>::const_pointer p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	const hook_type& hook = *p;
	return const_elem_ptr{ &hook.elem_ };
}

template<typename Type, typename Tag>
inline typename ll_list_transformations<Type, Tag, no_acqrel>::pointer
ll_list_transformations<Type, Tag, no_acqrel>::as_type_(const elem_ptr& p)
noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	pointer rv = nullptr;
	hazard_t hz{ *this };

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	hook_type& hook = *reinterpret_cast<hook_type*>(
	    reinterpret_cast<unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	reference value = static_cast<reference>(hook);

	hz.do_hazard(value,
	    [&]() {
		if (p->is_linked())
			rv = &value;
	    },
	    []() {
		assert(false);
	    });
	return rv;
}

template<typename Type, typename Tag>
inline typename ll_list_transformations<Type, Tag, no_acqrel>::const_pointer
ll_list_transformations<Type, Tag, no_acqrel>::as_type_(
    const const_elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	const_pointer rv = nullptr;
	hazard_t hz{ *this };

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	const hook_type& hook = *reinterpret_cast<const hook_type*>(
	    reinterpret_cast<const unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	const_reference value = static_cast<const_reference>(hook);

	hz.do_hazard(value,
	    [&]() {
		if (p->is_linked())
			rv = &value;
	    },
	    []() {
		assert(false);
	    });
	return rv;
}

template<typename Type, typename Tag>
inline typename ll_list_transformations<Type, Tag, no_acqrel>::pointer
ll_list_transformations<Type, Tag, no_acqrel>::as_type_unlinked_(
    const elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	hook_type& hook = *reinterpret_cast<hook_type*>(
	    reinterpret_cast<unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	reference value = static_cast<reference>(hook);

	return &value;
}

template<typename Type, typename Tag>
inline typename ll_list_transformations<Type, Tag, no_acqrel>::const_pointer
ll_list_transformations<Type, Tag, no_acqrel>::as_type_unlinked_(
    const const_elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	const_pointer rv = nullptr;

	/* Use offsetof magic to calculate hook address. */
	const std::size_t off = hook_type::_elem_offset();
	const hook_type& hook = *reinterpret_cast<const hook_type*>(
	    reinterpret_cast<const unsigned char*>(p.get()) - off);
	assert(&hook.elem_ == p);

	/* Cast to derived type. */
	const_reference value = static_cast<const_reference>(hook);

	return &value;
}

template<typename Type, typename Tag>
inline void
ll_list_transformations<Type, Tag, no_acqrel>::post_unlink_(
    ll_list_transformations<Type, Tag, no_acqrel>::const_reference
     unlinked_value,
    std::size_t uv_refs) const noexcept
{
	hazard_t::wait_unused(*this, unlinked_value);
}

template<typename Type, typename Tag>
inline void
ll_list_transformations<Type, Tag, no_acqrel>::release_pointer(
    ll_list_transformations<Type, Tag, no_acqrel>::pointer& p) noexcept
{
	p = nullptr;
}

template<typename Type, typename Tag>
inline void
ll_list_transformations<Type, Tag, no_acqrel>::release_pointer(
    ll_list_transformations<Type, Tag, no_acqrel>::const_pointer& p) noexcept
{
	p = nullptr;
}


} /* namespace ilias::ll_list_detail */


template<typename Type, typename Tag, typename AcqRel>
inline ll_smartptr_list<Type, Tag, AcqRel>::~ll_smartptr_list() noexcept
{
	this->clear();
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::empty() const noexcept
{
	return this->impl_.empty();
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::size_type
ll_smartptr_list<Type, Tag, AcqRel>::size() const noexcept
{
	return this->impl_.size();
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::pointer
ll_smartptr_list<Type, Tag, AcqRel>::pop_front() noexcept
{
	pointer result = this->as_type_unlinked_(this->impl_.pop_front());
	if (result)
		this->post_unlink_(*result, 0U);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::pointer
ll_smartptr_list<Type, Tag, AcqRel>::pop_back() noexcept
{
	pointer result = this->as_type_unlinked_(this->impl_.pop_back());
	if (result)
		this->post_unlink_(*result, 0U);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::push_back(
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	auto p_ = as_elem_(p).get();
	auto rv = this->impl_.push_back(p_);
	if (rv)
		parent_t::release_pointer(p);
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::push_front(
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	auto p_ = as_elem_(p).get();
	auto rv = this->impl_.push_front(p_);
	if (rv)
		parent_t::release_pointer(p);
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline std::pair<typename ll_smartptr_list<Type, Tag, AcqRel>::iterator, bool>
ll_smartptr_list<Type, Tag, AcqRel>::insert_after(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator& pos,
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	if (p == nullptr)
		throw std::invalid_argument("null element");

	std::pair<iterator, bool> result;
	if ((result.second = this->impl_.insert_after(
	    this->as_elem_(p), &pos.impl_, &result.first.impl_)))
		result.first.val_ = std::move(p);
	else
		result.first = pos;
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline std::pair<typename ll_smartptr_list<Type, Tag, AcqRel>::iterator, bool>
ll_smartptr_list<Type, Tag, AcqRel>::insert_after(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator& pos,
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	if (p == nullptr)
		throw std::invalid_argument("null element");

	std::pair<iterator, bool> result;
	if ((result.second = this->impl_.insert_after(
	    this->as_elem_(p), &pos.impl_, &result.first.impl_)))
		result.first.val_ = std::move(p);
	else
		result.first = pos;
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline std::pair<typename ll_smartptr_list<Type, Tag, AcqRel>::iterator, bool>
ll_smartptr_list<Type, Tag, AcqRel>::insert_before(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator& pos,
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	if (p == nullptr)
		throw std::invalid_argument("null element");

	std::pair<iterator, bool> result;
	if ((result.second = this->impl_.insert_before(
	    this->as_elem_(p), &pos.impl_, &result.first.impl_)))
		result.first.val_ = std::move(p);
	else
		result.first = pos;
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline std::pair<typename ll_smartptr_list<Type, Tag, AcqRel>::iterator, bool>
ll_smartptr_list<Type, Tag, AcqRel>::insert_before(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator& pos,
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	if (p == nullptr)
		throw std::invalid_argument("null element");

	std::pair<iterator, bool> result;
	if ((result.second = this->impl_.insert_before(
	    this->as_elem_(p), &pos.impl_, &result.first.impl_)))
		result.first.val_ = std::move(p);
	else
		result.first = pos;
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::insert(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator& pos,
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	iterator result;
	bool success;
	std::tie(result, success) = this->insert_before(pos, std::move(p));
	if (success)
		++result;
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::insert(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator& pos,
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	iterator result;
	bool success;
	std::tie(result, success) = this->insert_before(pos, std::move(p));
	if (success)
		++result;
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Disposer>
inline void
ll_smartptr_list<Type, Tag, AcqRel>::clear_and_dispose(
    Disposer disposer)
noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer&&>())))
{
	/* XXX try to see if this can be done using splicing logic? */
	while (pointer p = this->pop_front())
		disposer(std::move(p));
}

template<typename Type, typename Tag, typename AcqRel>
inline void
ll_smartptr_list<Type, Tag, AcqRel>::clear() noexcept
{
	return this->clear_and_dispose([](const pointer&) { /* SKIP */ });
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Disposer>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::erase_and_dispose(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator& iter,
    Disposer disposer)
noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer&&>())))
{
	return this->erase(iterator{ this, iter },
	    std::move_if_noexcept(disposer));
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Disposer>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::erase_and_dispose(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator& iter,
    Disposer disposer)
noexcept(noexcept(std::declval<Disposer&>()(std::declval<pointer&&>())))
{
	iterator rv{ iter };
	++rv;

	const auto& p = iter.get();
	if (this->unlink(p))
		disposer(std::move(p));
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::erase(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator& iter)
noexcept
{
	return this->erase_and_dispose(iter, [](const pointer&) { /* SKIP */ });
}

template<typename Type, typename Tag, typename AcqRel>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::erase(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator& iter)
noexcept
{
	return this->erase_and_dispose(iter, [](const pointer&) { /* SKIP */ });
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Predicate, typename Disposer>
inline void
ll_smartptr_list<Type, Tag, AcqRel>::remove_and_dispose_if(Predicate predicate,
    Disposer disposer)
noexcept(
	noexcept(std::declval<Predicate&>()(
	    std::declval<const_reference>())) &&
	noexcept(std::declval<Disposer&>()(
	    std::declval<pointer&&>())))
{
	for (iterator i = this->begin();
	    i.get();
	    ++i) {
		if (predicate(*i))
			erase_and_dispose(i, disposer);
	}
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Disposer>
inline void
ll_smartptr_list<Type, Tag, AcqRel>::remove_and_dispose(
   ll_smartptr_list<Type, Tag, AcqRel>::const_reference value,
   Disposer disposer)
noexcept(
	noexcept(std::declval<const_reference>() ==
	    std::declval<const_reference>()) &&
	noexcept(std::declval<Disposer&>()(
	    std::declval<pointer&&>())))
{
	this->remove_and_dispose_if(
	    [&value](const_reference test) {
		return (value == test);
	    },
	    std::move(disposer));
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Predicate>
inline void
ll_smartptr_list<Type, Tag, AcqRel>::remove_if(Predicate predicate)
noexcept(
	noexcept(std::declval<Predicate&>()(
	    std::declval<const_reference>())))
{
	this->remove_and_dispose_if(std::move(predicate),
	    [](const pointer&) {
		/* SKIP */
	    });
}

template<typename Type, typename Tag, typename AcqRel>
inline void
ll_smartptr_list<Type, Tag, AcqRel>::remove(
   ll_smartptr_list<Type, Tag, AcqRel>::const_reference value)
noexcept(
	noexcept(std::declval<const_reference>() ==
	    std::declval<const_reference>()))
{
	this->remove_and_dispose(value,
	    [](const pointer&) {
		/* SKIP */
	    });
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::unlink(const pointer& p) noexcept
{
	assert(p != nullptr);

	const bool rv = as_elem_(p)->unlink();
	if (rv)
		this->post_unlink_(*p, 1U);
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::unlink(const const_pointer& p) noexcept
{
	assert(p != nullptr);

	const bool rv = as_elem_(p)->unlink();
	if (rv)
		this->post_unlink_(*p, 1U);
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    ll_smartptr_list<Type, Tag, AcqRel>::reference r) noexcept
{
	iterator result;
	if (!result.link_at_(this, pointer{ &r }))
		result.link_at_(this, ll_list_detail::basic_iter::tag::head);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    ll_smartptr_list<Type, Tag, AcqRel>::const_reference r) const noexcept
{
	const_iterator result;
	if (!result.link_at_(this, const_pointer{ &r }))
		result.link_at_(this, ll_list_detail::basic_iter::tag::head);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    const ll_smartptr_list<Type, Tag, AcqRel>::pointer& p)
{
	if (p == nullptr)
		throw std::invalid_argument("cannot iterate to null");
	return this->iterator_to(*p);
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    const ll_smartptr_list<Type, Tag, AcqRel>::const_pointer& p) const
{
	if (p == nullptr)
		throw std::invalid_argument("cannot iterate to null");
	return this->iterator_to(*p);
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::begin() noexcept
{
	iterator result;
	result.link_at_(this, ll_list_detail::basic_iter::tag::first);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::end() noexcept
{
	bool success;
	iterator result;
	success = result.link_at_(this, ll_list_detail::basic_iter::tag::head);
	assert(success);
	return result;
}


namespace ll_list_iter_detail {


template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::parent_t::pointer
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
    IsConstIter>::get() const noexcept
{
	return this->val_;
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::parent_t::pointer
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
    IsConstIter>::operator->() const noexcept
{
	return this->get();
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::parent_t::reference
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
    IsConstIter>::operator*() const noexcept
{
	return *this->get();
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::operator typename
  ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
   IsConstIter>::parent_t::pointer() const noexcept
{
	return this->get();
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::parent_t::pointer
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
    IsConstIter>::release() noexcept
{
	return std::move(this->val_);
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
bool
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::link_at_(
    ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
     IsConstIter>::list_t* list,
    typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
     IsConstIter>::parent_t::pointer p)
{
	bool rv = this->impl_.link_at(&list->impl_, list_t::as_elem_(p));
	if (rv)
		this->val_ = std::move(p);
	return rv;
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
void
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::link_at_(
    ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
     IsConstIter>::list_t* list,
    ll_list_detail::basic_iter::tag where)
{
	using elem_ptr_t = typename std::conditional<IsConstIter,
		ll_list_detail::const_elem_ptr,
		ll_list_detail::elem_ptr
	    >::type;

	bool done = false;
	elem_ptr_t link{ nullptr };
	do {
		link = this->impl_.link_at(&list->impl_, where);

		if (link == nullptr) {
			/* SKIP */
		} else if (link->is_head()) {
			this->val_.reset();
			done = true;
		} else {
			if ((this->val_ = list->as_type_(std::move(link))) !=
			    nullptr)
				done = true;
		}
	} while (!done);
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
void
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::next(ll_list_detail::basic_list::size_type n) noexcept
{
	this->val_ = nullptr;
	for (auto elem = this->impl_.next(n);
	    elem != nullptr && elem->is_elem();
	    elem = this->impl_.next()) {
		if ((this->val_ = list_t::as_type_(elem)) !=
		    nullptr)
			return;
	}
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
void
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::prev(ll_list_detail::basic_list::size_type n) noexcept
{
	this->val_ = nullptr;
	for (auto elem = this->impl_.prev(n);
	    elem != nullptr && elem->is_elem();
	    elem = this->impl_.prev()) {
		if ((this->val_ = list_t::as_type_(elem)) !=
		    nullptr)
			return;
	}
}


template<typename Iter>
inline iter_direction<Iter, forward>::iter_direction(
    const iter_direction::parent_t& p)
noexcept(std::is_nothrow_copy_constructible<parent_t>::value)
:	parent_t{ p }
{
	/* Empty body. */
}

template<typename Iter>
inline iter_direction<Iter, forward>::iter_direction(
    iter_direction::parent_t&& p)
noexcept(std::is_nothrow_move_constructible<parent_t>::value)
:	parent_t{ std::move(p) }
{
	/* Empty body. */
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t&
iter_direction<Iter, forward>::operator++() noexcept
{
	this->next();
	return *this;
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t&
iter_direction<Iter, forward>::operator--() noexcept
{
	this->prev();
	return *this;
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t
iter_direction<Iter, forward>::operator++(int) const noexcept
{
	derived_t clone = *this;
	this->next();
	return clone;
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t
iter_direction<Iter, forward>::operator--(int) const noexcept
{
	derived_t clone = *this;
	this->prev();
	return clone;
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t
next(typename iter_direction<Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) noexcept
{
	if (n > 0)
		iter.next(n);
	if (n < 0)
		iter.prev(-n);
	return iter;
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t
prev(typename iter_direction<Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) noexcept
{
	if (n > 0)
		iter.prev(n);
	if (n < 0)
		iter.next(-n);
	return iter;
}

template<typename Iter>
typename iter_direction<Iter, forward>::derived_t
advance(typename iter_direction<Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) noexcept
{
	if (n > 0)
		iter.next(n);
	if (n < 0)
		iter.prev(-n);
	return iter;
}


template<typename Iter>
inline iter_direction<Iter, reverse>::iter_direction(
    const iter_direction::parent_t& p)
noexcept(std::is_nothrow_copy_constructible<parent_t>::value)
:	parent_t{ p }
{
	/* Empty body. */
}

template<typename Iter>
inline iter_direction<Iter, reverse>::iter_direction(
    iter_direction::parent_t&& p)
noexcept(std::is_nothrow_move_constructible<parent_t>::value)
:	parent_t{ std::move(p) }
{
	/* Empty body. */
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t&
iter_direction<Iter, reverse>::operator++() noexcept
{
	this->prev();
	return *this;
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t&
iter_direction<Iter, reverse>::operator--() noexcept
{
	this->next();
	return *this;
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t
iter_direction<Iter, reverse>::operator++(int) const noexcept
{
	derived_t clone = *this;
	this->prev();
	return clone;
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t
iter_direction<Iter, reverse>::operator--(int) const noexcept
{
	derived_t clone = *this;
	this->next();
	return clone;
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t
next(typename iter_direction<Iter, reverse>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) noexcept
{
	if (n > 0)
		iter.prev(n);
	if (n < 0)
		iter.next(-n);
	return iter;
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t
prev(typename iter_direction<Iter, reverse>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) noexcept
{
	if (n > 0)
		iter.next(n);
	if (n < 0)
		iter.prev(-n);
	return iter;
}

template<typename Iter>
typename iter_direction<Iter, reverse>::derived_t
advance(typename iter_direction<Iter, reverse>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) noexcept
{
	if (n > 0)
		iter.prev(n);
	if (n < 0)
		iter.next(-n);
	return iter;
}


}} /* namespace ilias::ll_list_iter_detail */

#endif /* ILIAS_LL_LIST_INL */
