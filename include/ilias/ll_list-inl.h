namespace ilias {
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

template<unsigned int N>
elem_ptr
elem::succ(std::array<elem_type, N> types) const noexcept
{
	using std::begin;
	using std::end;

	elem_ptr i = this->succ();
	while (std::find(begin(types), end(types), i->get_type()) ==
	    end(types))
		i = i->succ();
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

inline basic_list::size_type
basic_list::size() const noexcept
{
	size_type count = 0;
	for (elem_ptr i = this->head_.succ();
	    !i->is_head();
	    i = i->succ()) {
		if (i->is_elem())
			++count;
	}
	return count;
}

inline bool
basic_list::empty() const noexcept
{
	for (elem_ptr i = this->head_.succ();
	    !i->is_head() && !i->is_elem();
	    i = i->succ());
	return i->is_head();
}

inline bool
basic_list::push_front(elem* e)
{
	if (e == nullptr)
		throw std::invalid_argument("null element");

	return do_noexcept([&]() {
		auto rv = ll_basic_list::elem::link_after(e,
		    elem_ptr{ &this->head_ });
		assert(rv != ll_basic_list::link_result::INVALID_POS);
		return (rv == ll_basic_list::link_result::SUCCESS);
	    });
}

inline bool
basic_list::push_back(elem* e)
{
	if (e == nullptr)
		throw std::invalid_argument("null element");

	return do_noexcept([&]() {
		auto rv = ll_basic_list::elem::link_before(e,
		    elem_ptr{ &this->head_ });
		assert(rv != ll_basic_list::link_result::INVALID_POS);
		return (rv == ll_basic_list::link_result::SUCCESS);
	    });
}


inline basic_iter::basic_iter() noexcept
:	owner_{ nullptr }
	forw_{ elem_type::ITER_FORW },
	back_{ elem_type::ITER_BACK }
{
	/* Empty body. */
}

inline basic_iter::basic_iter(const basic_iter& other) noexcept
:	basic_iter{}
{
	if ((this->owner_ = other.owner_) != nullptr) {
		ll_simple_list::link_after(&this->forw_, &other.forw_);
		ll_simple_list::link_before(&this->back_, &other.back_);
	}
}

inline basic_iter::basic_iter(basic_iter&& other) noexcept
:	basic_iter{}
{
	if ((this->owner_ = other.owner_) != nullptr) {
		ll_simple_list::link_after(&this->forw_, &other.forw_);
		ll_simple_list::link_before(&this->back_, &other.back_);
		other.unlink();
	}
}

inline elem_ptr
basic_iter::link_at(basic_list* list, tag where)
{
	constexpr std::array<elem_type, 2> types = {
		elem_type::HEAD,
		elem_type::ELEM
	    };

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
		this->forw_.unlink()
		this->back_.unlink();
		this->owner_ = nullptr;
		return true;
	} else
		return false;
}


} /* namespace ilias::ll_list_detail */


template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::push_back(
    typename ll_smartptr_list<Type, Tag, AcqRel>::pointer p)
{
	this->impl_.push_back(as_elem_(p));
	p.release();
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::push_front(
    typename ll_smartptr_list::pointer<Type, Tag, AcqRel> p)
{
	this->impl_.push_front(as_elem_(p));
	p.release();
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Disposer>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::erase(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator& iter,
    Disposer disposer)
noexcept(noexcept(disposer(std::declval<pointer&&>())))
{
	return this->erase(iterator{ this, iter },
	    std::move_if_noexcept(disposer));
}

template<typename Type, typename Tag, typename AcqRel>
template<typename Disposer>
inline typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::erase(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator& iter,
    Disposer disposer)
noexcept(noexcept(disposer(std::declval<pointer&&>())))
{
	iterator rv{ iter };
	++rv;

	const auto& p = iter.get();
	if (this->unlink(p))
		disposer(std::move(p));
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline ll_list_detail::elem_ptr
ll_smartptr_list<Type, Tag, AcqRel>::as_elem_(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::pointer& p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	hook_type& hook = *p;
	return ll_list_detail::elem_ptr{ &hook.elem_ };
}

template<typename Type, typename Tag, typename AcqRel>
inline ll_list_detail::const_elem_ptr
ll_smartptr_list<Type, Tag, AcqRel>::as_elem_(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_pointer& p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	const hook_type& hook = *p;
	return ll_list_detail::const_elem_ptr{ &hook.elem_ };
}

template<typename Type, typename Tag, typename AcqRel>
inline ll_smartptr_list<Type, Tag, AcqRel>::pointer
ll_smartptr_list<Type, Tag, AcqRel>::as_type_(const elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	pointer rv;
	hazard_t hz{ this->impl_ };

	/* Use offsetof magic to calculate hook address. */
	constexpr std::size_t off = offsetof(hook_type, elem_);
	hook_type& hook = *reinterpret_cast<hook_type*>(
	    reinterpret_cast<unsigned char*>(p.get()) - off);

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
inline ll_smartptr_list<Type, Tag, AcqRel>::const_pointer
ll_smartptr_list<Type, Tag, AcqRel>::as_type_(const const_elem_ptr& p) noexcept
{
	if (p == nullptr || !p->is_elem())
		return nullptr;

	const_pointer rv;
	hazard_t hz{ this->impl_ };

	/* Use offsetof magic to calculate hook address. */
	constexpr std::size_t off = offsetof(hook_type, elem_);
	const hook_type& hook = *reinterpret_cast<const hook_type*>(
	    reinterpret_cast<const unsigned char*>(p.get()) - off);

	/* Cast to derived type. */
	const_reference value = static_cast<const_reference>(hook);

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
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::unlink(const pointer& p) noexcept
{
	assert(p != nullptr);

	const bool rv = as_elem_(p)->unlink();
	if (rv) {
		hazard_t::grant(
		    [&p](std::size_t nrefs) {
			AcqRel::acquire(*p, nrefs);
		    },
		    [&p](std::size_t nrefs) {
			AcqRel::release(*p, nrefs);
		    },
		    this->impl_, *p, 1U);
	}
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
inline bool
ll_smartptr_list<Type, Tag, AcqRel>::unlink(const const_pointer& p) noexcept
{
	assert(p != nullptr);

	const bool rv = as_elem_(p)->unlink();
	if (rv) {
		hazard_t::grant(
		    [&p](std::size_t nrefs) {
			AcqRel::acquire(*p, nrefs);
		    },
		    [&p](std::size_t nrefs) {
			AcqRel::release(*p, nrefs);
		    },
		    this->impl_, *p, 1U);
	}
	return rv;
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    ll_smartptr_list<Type, Tag, AcqRel>::reference r) noexcept
{
	bool success;
	iterator result;
	if (!(success = result.link_at_(this, pointer{ &r }))) {
		success = result.link_at_(this,
		    ll_list_detail::basic_iter::tag::head);
	}
	assert(success);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::const_iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    ll_smartptr_list<Type, Tag, AcqRel>::const_reference r) const noexcept
{
	bool success;
	const_iterator result;
	if (!(success = result.link_at_(this, const_pointer{ &r }))) {
		success = result.link_at_(this,
		    ll_list_detail::basic_iter::tag::head);
	}
	assert(success);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::begin() noexcept
{
	bool success;
	iterator result;
	success = result.link_at_(this,
	    ll_list_detail::basic_iter::tag::first);
	assert(success);
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
ll_smartptr_list<Type, Tag, AcqRel>::iterator
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
const typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::parent_t::pointer&
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
    IsConstIter>::operator->() const noexcept
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
bool
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::link_at_(
    ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
     IsConstIter>::list_t* list,
    typename ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
     IsConstIter>::parent_t::pointer p)
{
	bool rv = this->impl_.link_at(&list->impl_, list_t::as_elem(p));
	if (rv)
		this->val_ = std::move(p);
	return rv;
}

template<typename Type, typename Tag, typename AcqRel, bool IsConstIter>
bool
ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
 IsConstIter>::link_at_(
    ll_smartptr_list_iterator<ll_smartptr_list<Type, Tag, AcqRel>,
     IsConstIter>::list_t* list,
    ll_list_detail::basic_iter::tag where)
{
	using elem_ptr_t = std::conditional<IsConstIter,
		ll_list_detail::const_elem_ptr,
		ll_list_detail::elem_ptr
	    >::type;

	bool done = false;
	elem_ptr_t link{ nullptr };
	do {
		link = this->impl_.link_at_(&list->impl_, where);

		if (link == nullptr) {
			/* SKIP */
		} else if (link->is_head()) {
			this->val_.reset();
			done = true;
		} else {
			if ((this->val_ = as_type_(std::move(link))) !=
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
		if ((this->val_ = list_t::as_type_(this->impl_.next())) !=
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
	for (auto elem = this->impl_.next(n);
	    elem != nullptr && elem->is_elem();
	    elem = this->impl_.next()) {
		if ((this->val_ = list_t::as_type_(this->impl_.next())) !=
		    nullptr)
			return;
	}
}


template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t&
iter_direction<Derived, Iter, forward>::operator++() noexcept
{
	this->iter_t::next();
	return *this;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t&
iter_direction<Derived, Iter, forward>::operator--() noexcept
{
	this->iter_t::prev();
	return *this;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
iter_direction<Derived, Iter, forward>::operator++() const noexcept
{
	derived_t clone = *this;
	this->iter_t::next();
	return clone;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
iter_direction<Derived, Iter, forward>::operator--() const noexcept
{
	derived_t clone = *this;
	this->iter_t::prev();
	return clone;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
next(typename iter_direction<Derived, Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) const noexcept
{
	if (n > 0)
		iter.iter_t::next(n);
	if (n < 0)
		iter.iter_t::prev(-n);
	return iter;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
prev(typename iter_direction<Derived, Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) const noexcept
{
	if (n > 0)
		iter.iter_t::prev(n);
	if (n < 0)
		iter.iter_t::next(-n);
	return iter;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
advance(typename iter_direction<Derived, Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) const noexcept
{
	if (n > 0)
		iter.iter_t::next(n);
	if (n < 0)
		iter.iter_t::prev(-n);
	return iter;
}


template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t&
iter_direction<Derived, Iter, forward>::operator++() noexcept
{
	this->iter_t::prev();
	return *this;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t&
iter_direction<Derived, Iter, forward>::operator--() noexcept
{
	this->iter_t::next();
	return *this;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
iter_direction<Derived, Iter, forward>::operator++() const noexcept
{
	derived_t clone = *this;
	this->iter_t::prev();
	return clone;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
iter_direction<Derived, Iter, forward>::operator--() const noexcept
{
	derived_t clone = *this;
	this->iter_t::next();
	return clone;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
next(typename iter_direction<Derived, Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) const noexcept
{
	if (n > 0)
		iter.iter_t::prev(n);
	if (n < 0)
		iter.iter_t::next(-n);
	return iter;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
prev(typename iter_direction<Derived, Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) const noexcept
{
	if (n > 0)
		iter.iter_t::next(n);
	if (n < 0)
		iter.iter_t::prev(-n);
	return iter;
}

template<typename Derived, typename Iter>
typename iter_direction<Derived, Iter, forward>::derived_t
advance(typename iter_direction<Derived, Iter, forward>::derived_t iter,
    ll_list_detail::basic_list::difference_type n) const noexcept
{
	if (n > 0)
		iter.iter_t::prev(n);
	if (n < 0)
		iter.iter_t::next(-n);
	return iter;
}


}} /* namespace ilias::ll_list_iter_detail */
