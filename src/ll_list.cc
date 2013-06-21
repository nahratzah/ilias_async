#include <ilias/ll_list.h>


namespace ilias {
namespace ll_list_detail {


class simple_elem::prepare_store
{
private:
	simple_elem_ptr& m_ptr;
	bool success;

public:
	prepare_store(simple_elem_ptr& p, simple_ptr v) noexcept
	:	m_ptr{ p },
		success{
			p.compare_exchange_strong(
			    add_present(std::get<0>(
			      p.load(std::memory_order_relaxed))),
			    add_present(std::move(v)),
			    std::memory_order_relaxed,
			    std::memory_order_relaxed)
		}
	{
		/* Empty body. */
	}

	prepare_store(const prepare_store&) = delete;

	prepare_store(prepare_store&& o) noexcept
	:	m_ptr{ o.m_ptr },
		success{ false }
	{
		using std::swap;

		swap(this->success, o.success);
	}

	~prepare_store() noexcept
	{
		if (this->success)
			this->m_ptr.reset(std::memory_order_relaxed);
	}

	explicit operator bool() const noexcept
	{
		return this->success;
	}

	void
	commit() noexcept
	{
		assert(success);
		success = false;
	}
};

simple_elem_ptr::element_type
simple_elem::succ() const noexcept
{
	auto s = this->m_succ.load(std::memory_order_consume);
	for (;;) {
		simple_ptr ss;
		flags_type fl;
		std::tie(ss, fl) =
		    std::get<0>(s)->m_succ.load(std::memory_order_consume);

		/* GUARD */
		if (fl == PRESENT || ss == std::get<0>(s))
			return s;	/* s is not deleted or is this. */

		/* Aid in deletion of ss. */
		ss->m_pred.compare_exchange_weak(
		    add_present(std::get<0>(s).get()),
		    add_delete(std::get<0>(s)),
		    std::memory_order_relaxed, std::memory_order_relaxed);

		/* Update our succession pointer. */
		if (this->m_succ.compare_exchange_weak(s, std::tie(ss, fl),
		    std::memory_order_consume, std::memory_order_consume))
			std::get<0>(s) = std::move(ss);
	}

	/* UNREACHABLE */
}

simple_elem_ptr::element_type
simple_elem::pred() const noexcept
{
	auto p = this->m_pred.load(std::memory_order_consume);
	while (std::get<1>(p) != DELETED) {
		simple_ptr ps;
		flags_type fl;
		std::tie(ps, fl) = std::get<0>(p)->succ();

		/* GUARD: p->succ() == this. */
		if (std::tie(ps, fl) == add_present(this))
			return ps;

		/* Update predecessor. */
		if (this->m_pred.compare_exchange_weak(p, add_present(ps),
		    std::memory_order_consume, std::memory_order_consume))
			std::get<0>(p) = std::move(ps);
	}

	/*
	 * Handle case where this is a deleted element.
	 */
	simple_ptr pp;
	flags_type fl;
	std::tie(pp, fl) = std::get<0>(p)->m_pred.load(
	    std::memory_order_consume);

	while (fl == DELETED) {
		/*
		 * pp is also deleted, so it points to its correct
		 * predecessor.
		 *
		 * Note that this predecessor may also be deleted,
		 * so restarting the call is required.
		 */
		if (this->m_pred.compare_exchange_weak(p,
		    add_delete(pp),
		    std::memory_order_consume,
		    std::memory_order_consume))
			std::get<0>(p) = std::move(pp);
		std::tie(pp, fl) = std::get<0>(p)->m_pred.load(
		    std::memory_order_consume);
	}

	/*
	 * p is now the first non-deleted element of a chain of deleted
	 * elements.
	 */
	return p;
}

bool
simple_elem::link(simple_elem_range ins, simple_elem_range between)
noexcept
{
	simple_ptr& b = std::get<0>(ins);
	simple_ptr& e = std::get<1>(ins);

	simple_ptr pred, succ;
	std::tie(pred, succ) = std::move(between);

	assert(b && e && pred && succ);

	prepare_store b_store{ b->m_pred, pred },
	    e_store{ e->m_succ, succ };
	assert(b_store && e_store);	/* XXX change to return value? */

	if (!pred->m_succ.compare_exchange_strong(add_present(succ.get()),
	    add_present(std::move(b)),
	    std::memory_order_release, std::memory_order_relaxed))
		return false;
	succ->m_pred.compare_exchange_strong(add_present(pred.get()),
	    add_present(std::move(e)),
	    std::memory_order_relaxed, std::memory_order_relaxed);
	return true;
}

bool
simple_elem::link_after(simple_elem_range ins, simple_ptr pred)
noexcept
{
	assert(pred);
	simple_ptr succ;
	flags_type fl;

	do {
		std::tie(succ, fl) = pred->succ();
		if (fl == DELETED)
			return false;
	} while (!link(ins, std::make_tuple(pred, std::move(succ))));
	return true;
}

bool
simple_elem::link_before(simple_elem_range ins, simple_ptr succ)
noexcept
{
	assert(succ);
	simple_ptr pred;
	flags_type fl;

	do {
		std::tie(pred, fl) = succ->pred();
		if (fl == DELETED)
			return false;
	} while (!link(ins, std::make_tuple(std::move(pred), succ)));
	return true;
}


}} /* namespace ilias::ll_list_detail */
