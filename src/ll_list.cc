#include <ilias/ll_list.h>


namespace ilias {
namespace ll_list_detail {


elem_ptr
basic_iter::next(basic_list::size_type n) noexcept
{
	if (this->owner_ == nullptr || !this->is_linked())
		return nullptr;
	assert(n > 0);

	constexpr std::array<elem_type, 2> types = {
		elem_type::HEAD,
		elem_type::ELEM
	    };

	return do_noexcept([&]() {
		/* Step n items. */
		elem_ptr i = this->forw_.succ(types);
		for (basic_list::size_type n_ = 1; n_ < n; ++n)
			i = i->succ(types);

		/* Try to link. */
		while (!link_at(this->owner_, i))
			i = i->succ(types);
		return i;
	    });
}

elem_ptr
basic_iter::prev(basic_list::size_type n) noexcept
{
	if (this->owner_ == nullptr || !this->is_linked())
		return nullptr;
	assert(n > 0);

	constexpr std::array<elem_type, 2> types = {
		elem_type::HEAD,
		elem_type::ELEM
	    };

	return do_noexcept([&]() {
		/* Step n items. */
		elem_ptr i = this->back_.pred(types);
		for (basic_list::size_type n_ = 1; n_ < n; ++n)
			i = i->pred(types);

		/* Try to link. */
		while (!link_at(this->owner_, i))
			i = i->pred(types);
		return i;
	    });
}

bool
basic_iter::link_at_(basic_list* list, elem_ptr pos) noexcept
{
	assert(list != nullptr && pos != nullptr);

	this->forw_.unlink();
	this->back_.unlink();
	this->owner_ = list;

	const auto rv_forw = ll_simple_list::link_after(
	    &this->forw_, pos);
	assert(rv_forw != link_result::INS0_LINKED &&
	    rv_forw != link_result::INS1_LINKED);

	const auto rv_back = ll_simple_list::link_before(
	    &this->back_, pos);
	assert(rv_back != link_result::INS0_LINKED &&
	    rv_back != link_result::INS1_LINKED);

	return (rv_forw == link_result::SUCCESS &&
	    rv_back == link_result::SUCCESS);
}

bool
forw_equal(const basic_iter& a, const basic_iter& b) noexcept
{
	if (a.list != b.list)
		return false;
	if (!a.list)
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
	if (a.list != b.list)
		return false;
	if (!a.list)
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
