#include <ilias/ll_list.h>


namespace ilias {
namespace ll_list_detail {


basic_list::size_type
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

bool
basic_list::empty() const noexcept
{
	elem_ptr i;
	for (i = this->head_.succ();
	    !i->is_head() && !i->is_elem();
	    i = i->succ());
	return i->is_head();
}

elem_ptr
basic_list::pop_front() noexcept
{
	constexpr std::array<elem_type, 2> types{{
		elem_type::HEAD,
		elem_type::ELEM
	    }};

	elem_ptr i_next;
	for (elem_ptr i = this->head_.succ(types);
	    i->is_elem();
	    i = std::move(i_next)) {
		i_next = i->succ(types);
		if (unlink(i))
			return i;
	}
	return nullptr;
}

elem_ptr
basic_list::pop_back() noexcept
{
	constexpr std::array<elem_type, 2> types{{
		elem_type::HEAD,
		elem_type::ELEM
	    }};

	elem_ptr i_next;
	for (elem_ptr i = this->head_.pred(types);
	    i->is_elem();
	    i = std::move(i_next)) {
		i_next = i->pred(types);
		if (unlink(i))
			return i;
	}
	return nullptr;
}

bool
basic_list::insert_after_(elem* ins, const_elem_ptr pos, basic_iter* out_iter)
noexcept
{
	assert(ins != nullptr && ins->is_elem());
	assert(pos != nullptr && pos->is_forw_iter());
	assert(out_iter == nullptr ||
	    (pos != &out_iter->forw_ && pos != &out_iter->back_));

	std::tuple<elem_ptr, elem_ptr> pos_tpl{
		const_pointer_cast<elem>(pos),
		nullptr
	    };
	ll_simple_list::link_result lr;
	if (out_iter)
		out_iter->unlink();

	/* Insert the element. */
	do {
		/* Fix pos_tpl to hold both ends of the insertion point. */
		std::get<1>(pos_tpl) = std::get<0>(pos_tpl)->succ();
		while (std::get<1>(pos_tpl)->is_forw_iter()) {
			std::get<0>(pos_tpl) = std::move(std::get<1>(pos_tpl));
			std::get<1>(pos_tpl) = std::get<0>(pos_tpl)->succ();
		}

		/* Validate pos_tpl. */
		assert(!std::get<0>(pos_tpl)->is_back_iter());
		assert(!std::get<1>(pos_tpl)->is_forw_iter());

		lr = elem::link_between(ins, pos_tpl);
	} while (lr == ll_simple_list::link_result::INVALID_POS);

	/* Return failure. */
	if (lr != ll_simple_list::link_result::SUCCESS)
		return false;

	/* Update out_iter. */
	if (out_iter)
		out_iter->link_post_insert_(this, std::move(pos_tpl));

	return true;
}

bool
basic_list::insert_before_(elem* ins, const_elem_ptr pos, basic_iter* out_iter)
noexcept
{
	assert(ins != nullptr && ins->is_elem());
	assert(pos != nullptr && pos->is_back_iter());
	assert(out_iter == nullptr ||
	    (pos != &out_iter->forw_ && pos != &out_iter->back_));

	std::tuple<elem_ptr, elem_ptr> pos_tpl{
		nullptr,
		const_pointer_cast<elem>(pos),
	    };
	ll_simple_list::link_result lr;
	if (out_iter)
		out_iter->unlink();

	/* Insert the element. */
	do {
		/* Fix pos_tpl to hold both ends of the insertion point. */
		std::get<0>(pos_tpl) = std::get<1>(pos_tpl)->pred();
		while (std::get<0>(pos_tpl)->is_back_iter()) {
			std::get<1>(pos_tpl) = std::move(std::get<0>(pos_tpl));
			std::get<0>(pos_tpl) = std::get<1>(pos_tpl)->pred();
		}

		/* Validate pos_tpl. */
		assert(!std::get<0>(pos_tpl)->is_back_iter());
		assert(!std::get<1>(pos_tpl)->is_forw_iter());

		lr = elem::link_between(ins, pos_tpl);
	} while (lr == ll_simple_list::link_result::INVALID_POS);

	/* Return failure. */
	if (lr != ll_simple_list::link_result::SUCCESS)
		return false;

	/* Update out_iter. */
	if (out_iter)
		out_iter->link_post_insert_(this, std::move(pos_tpl));

	return true;
}


elem_ptr
basic_iter::next(basic_list::size_type n) noexcept
{
	assert(n > 0);
	if (this->owner_ == nullptr)
		return nullptr;
	assert(this->forw_.is_linked() && this->back_.is_linked());

	constexpr std::array<elem_type, 2> types{{
		elem_type::HEAD,
		elem_type::ELEM
	    }};

	basic_list*const owner = this->owner_;
	/* Step n items. */
	elem_ptr i = this->forw_.succ(types);
	for (basic_list::size_type n_ = 1; n_ < n; ++n)
		i = i->succ(types);

	/* Try to link. */
	while (!this->link_at(owner, i))
		i = i->succ(types);
	return i;
}

elem_ptr
basic_iter::prev(basic_list::size_type n) noexcept
{
	assert(n > 0);
	if (this->owner_ == nullptr)
		return nullptr;
	assert(this->forw_.is_linked() && this->back_.is_linked());

	constexpr std::array<elem_type, 2> types{{
		elem_type::HEAD,
		elem_type::ELEM
	    }};

	basic_list*const owner = this->owner_;
	/* Step n items. */
	elem_ptr i = this->back_.pred(types);
	for (basic_list::size_type n_ = 1; n_ < n; ++n)
		i = i->pred(types);

	/* Try to link. */
	while (!this->link_at(owner, i))
		i = i->pred(types);
	return i;
}

void
basic_iter::link_post_insert_(basic_list* list,
    std::tuple<elem_ptr, elem_ptr> between) noexcept
{
	assert(this->owner_ == nullptr);
	assert(!this->forw_.is_linked() && !this->back_.is_linked());
	assert(!std::get<0>(between)->is_back_iter());
	assert(!std::get<1>(between)->is_forw_iter());

	this->owner_ = list;

	std::tuple<elem_ptr, elem_ptr> back_pos{
		std::move(std::get<0>(between)),
		nullptr
	    };
	for (;;) {
		std::get<1>(back_pos) = std::get<0>(back_pos)->succ();
		while (std::get<1>(back_pos)->is_forw_iter()) {
			std::get<0>(back_pos) =
			    std::move(std::get<1>(back_pos));
			std::get<1>(back_pos) =
			    std::get<0>(back_pos)->succ();
		}
		auto lr = elem::link_between(&this->back_, back_pos);
		if (lr == ll_simple_list::link_result::SUCCESS)
			break;	/* GUARD */

		assert(lr == ll_simple_list::link_result::INVALID_POS);
		/*
		 * We don't own get<0>(back_pos), so the owner can freely
		 * unlink it.  If that happens, acquire a new position.
		 */
		if (!std::get<0>(back_pos)->is_linked())
			std::get<0>(back_pos) = std::get<0>(back_pos)->pred();
	}

	std::tuple<elem_ptr, elem_ptr> forw_pos{
		nullptr,
		std::move(std::get<1>(between))
	    };
	for (;;) {
		std::get<0>(forw_pos) = std::get<1>(forw_pos)->pred();
		while (std::get<0>(forw_pos)->is_back_iter()) {
			std::get<1>(forw_pos) =
			    std::move(std::get<0>(forw_pos));
			std::get<0>(forw_pos) =
			    std::get<1>(forw_pos)->pred();
		}
		auto lr = elem::link_between(&this->forw_, forw_pos);
		if (lr == ll_simple_list::link_result::SUCCESS)
			break;	/* GUARD */

		assert(lr == ll_simple_list::link_result::INVALID_POS);
		/*
		 * We don't own get<1>(forw_pos), so the owner can freely
		 * unlink it.  If that happens, acquire a new position.
		 */
		if (!std::get<1>(forw_pos)->is_linked())
			std::get<1>(forw_pos) = std::get<1>(forw_pos)->succ();
	}
}

bool
basic_iter::link_at_(basic_list* list, elem_ptr pos) noexcept
{
	assert(list != nullptr && pos != nullptr);
	assert(pos->is_head() || pos->is_elem());
	const bool check_pos_validity = pos->is_elem();

	this->unlink();
	this->owner_ = list;

	const auto rv_forw = ll_simple_list::elem::link_after(
	    &this->forw_, pos, check_pos_validity);
	assert(rv_forw != ll_simple_list::link_result::INS0_LINKED &&
	    rv_forw != ll_simple_list::link_result::INS1_LINKED);

	const auto rv_back = ll_simple_list::elem::link_before(
	    &this->back_, pos, check_pos_validity);
	assert(rv_back != ll_simple_list::link_result::INS0_LINKED &&
	    rv_back != ll_simple_list::link_result::INS1_LINKED);

	if (rv_forw == ll_simple_list::link_result::SUCCESS &&
	    rv_back == ll_simple_list::link_result::SUCCESS)
		return true;

	this->unlink();
	return false;
}

bool
forw_equal(const basic_iter& a, const basic_iter& b) noexcept
{
	if (a.owner_ != b.owner_)
		return false;
	if (!a.owner_)
		return true;

	assert(a.forw_.is_linked());
	assert(b.forw_.is_linked());

	for (const_elem_ptr a_i{ &a.forw_ };
	    a_i->is_iter();
	    a_i = a_i->succ()) {
		if (a_i == &b.forw_)
			return true;
	}

	for (const_elem_ptr b_i{ &b.forw_ };
	    b_i->is_iter();
	    b_i = b_i->succ()) {
		if (b_i == &a.forw_)
			return true;
	}

	return false;
}

bool
back_equal(const basic_iter& a, const basic_iter& b) noexcept
{
	if (a.owner_ != b.owner_)
		return false;
	if (!a.owner_)
		return true;

	assert(a.back_.is_linked());
	assert(b.back_.is_linked());

	for (const_elem_ptr a_i{ &a.back_ };
	    a_i->is_iter();
	    a_i = a_i->succ()) {
		if (a_i == &b.back_)
			return true;
	}

	for (const_elem_ptr b_i{ &b.back_ };
	    b_i->is_iter();
	    b_i = b_i->succ()) {
		if (b_i == &a.back_)
			return true;
	}

	return false;
}


}} /* namespace ilias::ll_list_detail */
