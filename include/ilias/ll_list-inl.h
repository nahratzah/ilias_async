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
	this->owner_ = other.owner_;
	ll_simple_list::link_after(&this->forw_, &other.forw_);
	ll_simple_list::link_before(&this->back_, &other.back_);
}

inline basic_iter::basic_iter(basic_iter&& other) noexcept
:	basic_iter{}
{
	this->owner_ = other.owner_;
	ll_simple_list::link_after(&this->forw_, &other.forw_);
	ll_simple_list::link_before(&this->back_, &other.back_);

	other.owner_ = nullptr;
	other.forw_.unlink();
	other.back_.unlink();
}

inline basic_iter::basic_iter(basic_list* list, tag_first)
:	basic_iter{}
{
	if (list == nullptr)
		throw std::invalid_argument("null list");

	constexpr std::array<elem_type, 2> types = {
		elem_type::HEAD,
		elem_type::ELEM
	    };

	do_noexcept([&]() {
		elem_ptr f = list->head_.succ(types);
		while (!this->link_at(list, f))
			f = f->succ(types);
	    });
}

inline basic_iter::basic_iter(basic_list* list, tag_last)
:	basic_iter{}
{
	if (list == nullptr)
		throw std::invalid_argument("null list");

	constexpr std::array<elem_type, 2> types = {
		elem_type::HEAD,
		elem_type::ELEM
	    };

	do_noexcept([&]() {
		elem_ptr f = list->head_.pred(types);
		while (!this->link_at(list, f))
			f = f->pred(types);
	    });
}

inline basic_iter::basic_iter(basic_list* list, tag_head)
:	basic_iter{}
{
	if (list == nullptr)
		throw std::invalid_argument("null list");

	do_noexcept([&](elem_ptr f) {
		while (!this->link_at(list, f));
	    },
	    elem_ptr{ &list->head_ });
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
inline ll_list_detail::elem_ptr
ll_smartptr_list<Type, Tag, AcqRel>::as_elem_(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::pointer& p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	return ll_list_detail::elem_ptr{ p.get() };
}

template<typename Type, typename Tag, typename AcqRel>
inline ll_list_detail<Type, Tag, AcqRel>::const_elem_ptr
ll_smartptr_list<Type, Tag, AcqRel>::as_elem_(
    const typename ll_smartptr_list<Type, Tag, AcqRel>::const_pointer& p)
noexcept
{
	if (p == nullptr)
		return nullptr;
	return ll_list_detail::elem_ptr{ p.get() };
}


template<typename Type, typename Tag, typename AcqRel>
const typename ll_smartptr_list<Type, Tag, AcqRel>::pointer&
ll_smartptr_list<Type, Tag, AcqRel>::iterator::get() const noexcept
{
	return this->val_;
}

template<typename Type, typename Tag, typename AcqRel>
const typename ll_smartptr_list<Type, Tag, AcqRel>::iterator::pointer&
ll_smartptr_list<Type, Tag, AcqRel>::iterator::operator->() const noexcept
{
	return this->get().get();
}

template<typename Type, typename Tag, typename AcqRel>
typename ll_smartptr_list<Type, Tag, AcqRel>::iterator::reference&
ll_smartptr_list<Type, Tag, AcqRel>::iterator::operator*() const noexcept
{
	return *this->get();
}

template<typename Type, typename Tag, typename AcqRel>
explicit operator typename ll_smartptr_list<Type, Tag, AcqRel>::pointer() const
noexcept
{
	return this->get();
}

template<typename Type, typename Tag, typename AcqRel>
bool
ll_smartptr_list<Type, Tag, AcqRel>::link_at_(
    ll_smartptr_list<Type, Tag, AcqRel>* list,
    typename ll_smartptr_list::pointer p)
{
	if (p == nullptr)
		throw std::invalid_argument("cannot iterate to null");
	if (list == nullptr)
		throw std::invalid_argument("null list");

	return do_noexcept([&]() {
		bool rv = this->impl_.link_at(&list->impl_, as_elem(p));
		if (rv)
			this->val_ = std::move(p);
		return rv;
	    });
}

template<typename Type, typename Tag, typename AcqRel>
ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::iterator_to(
    ll_smartptr_list<Type, Tag, AcqRel>::reference r) noexcept
{
	iterator result;
	if (!result.link_at_(this, pointer{ &r }))
		result.link_at_(this, ll_list_detail::tag_head{});
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::begin() noexcept
{
	iterator result;
	result.link_at_(this, ll_list_detail::tag_first{});
	return result;
}

template<typename Type, typename Tag, typename AcqRel>
ll_smartptr_list<Type, Tag, AcqRel>::iterator
ll_smartptr_list<Type, Tag, AcqRel>::end() noexcept
{
	iterator result;
	result.link_at_(this, ll_list_detail::tag_head{});
	return result;
}


} /* namespace ilias */
