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

bool
simple_elem::wait_unlinked() const noexcept
{
	bool done;

	do {
		done = true;
		auto s = this->m_succ.load_no_acquire(
		    std::memory_order_relaxed);
		auto p = this->m_pred.load_no_acquire(
		    std::memory_order_relaxed);

		if (std::get<0>(s) == this) {
			/* SKIP. */
		} else if (std::get<1>(s) != DELETED)
			return false;
		else
			done = false;

		if (std::get<0>(p) == this) {
			/* SKIP. */
		} else if (std::get<1>(p) != DELETED)
			return false;
		else
			done = false;
	} while (!done);

	std::atomic_thread_fence(std::memory_order_acquire);
	return true;
}

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

link_result
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
	if (!b_store || !e_store)
		return link_result::ALREADY_LINKED;

	auto pred_expect = add_present(succ.get());
	if (!pred->m_succ.compare_exchange_strong(pred_expect,
	    add_present(std::move(b)),
	    std::memory_order_release, std::memory_order_relaxed)) {
		if (std::get<1>(pred_expect) == DELETED)
			return link_result::PRED_DELETED;
		return link_result::RETRY;
	}
	if (!succ->m_pred.compare_exchange_strong(add_present(pred.get()),
	    add_present(std::move(e)),
	    std::memory_order_relaxed, std::memory_order_relaxed))
		succ->pred();	/* Fix predecessor. */

	/* Ensure modification on b and e will not be undone. */
	b_store.commit();
	e_store.commit();

	return link_result::SUCCESS;
}

link_ab_result
simple_elem::link_after(simple_elem_range ins, simple_ptr pred)
noexcept
{
	assert(pred);
	simple_ptr succ;

	std::tie(succ, std::ignore) = pred->succ();
	for (;;) {
		link_result lr = link(ins,
		    std::make_tuple(pred, std::move(succ)));
		switch (lr) {
		default:
			return ab_result(lr);
		case link_result::SUCC_DELETED:
		case link_result::RETRY:
			std::tie(succ, std::ignore) = pred->succ();
			break;
		}
	}

	/* UNREACHABLE */
}

link_ab_result
simple_elem::link_before(simple_elem_range ins, simple_ptr succ)
noexcept
{
	assert(succ);
	simple_ptr pred;

	std::tie(pred, std::ignore) = succ->pred();
	for (;;) {
		link_result lr = link(ins,
		    std::make_tuple(std::move(pred), succ));
		switch (lr) {
		default:
			return ab_result(lr);
		case link_result::PRED_DELETED:
		case link_result::RETRY:
			std::tie(pred, std::ignore) = succ->pred();
			break;
		}
	}

	/* UNREACHABLE */
}

bool
simple_elem::unlink() noexcept
{
	/* Mark successor link as to-be-deleted. */
	auto succ = this->m_succ.load(std::memory_order_relaxed);
	do {
		if (std::get<0>(succ) == this)
			return false;
	} while (std::get<1>(succ) != DELETED &&
	    !this->m_succ.compare_exchange_weak(succ,
	      add_delete(std::get<0>(succ)),
	      std::memory_order_relaxed, std::memory_order_relaxed));
	std::atomic_thread_fence(std::memory_order_acq_rel);
	const bool rv = (std::get<1>(succ) == DELETED);

	/* Mark predecessor link as to-be-deleted. */
	auto pred = this->pred();
	while (std::get<1>(pred) != DELETED &&
	    !this->m_pred.compare_exchange_weak(pred,
	      add_delete(std::get<0>(pred)),
	      std::memory_order_acq_rel, std::memory_order_relaxed))
		pred = this->pred();

	/* Make pred skip this. */
	std::get<0>(pred)->m_succ.compare_exchange_strong(add_present(this),
	    add_present(std::get<0>(succ)),
	    std::memory_order_release, std::memory_order_relaxed);
	std::get<0>(pred)->m_succ.compare_exchange_strong(add_delete(this),
	    add_delete(std::get<0>(succ)),
	    std::memory_order_release, std::memory_order_relaxed);

	/* Make succ skip this. */
	std::get<0>(succ)->m_pred.compare_exchange_strong(add_present(this),
	    add_present(std::get<0>(pred)),
	    std::memory_order_release, std::memory_order_relaxed);
	std::get<0>(succ)->m_pred.compare_exchange_strong(add_delete(this),
	    add_delete(std::get<0>(pred)),
	    std::memory_order_release, std::memory_order_relaxed);

	return rv;
}

void
simple_elem_acqrel::release(const simple_elem& e, elem_refcnt nrefs) const
noexcept
{
	if (nrefs == 0)
		return;

	/*
	 * XXX This is potentially hard on the stack, since each element
	 * may require release on another element.
	 * Stack depth could quickly grow because of the recursion.
	 *
	 * Tail recursion will not work, since an unlinked element
	 * results in 2 new elements potentially requiring unlinking.
	 */
	auto r = e.m_refcnt.load(std::memory_order_relaxed);
	do {
		assert(r >= nrefs);
		if (r == nrefs) {
			if (nrefs < 2U) {
				e.m_refcnt.fetch_add(2U - nrefs,
				    std::memory_order_acquire);
				nrefs = 2U;
			} else {
				std::atomic_thread_fence(
				    std::memory_order_acquire);
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

head::head(head&& o) noexcept
:	elem{ elem_type::HEAD }
{
	simple_ptr self{ this };
	simple_ptr optr{ this };

	const link_ab_result lnk = simple_elem::link_after(self, optr);
	assert(lnk == link_ab_result::SUCCESS);
	const bool ulnk = o.unlink();
	assert(ulnk);
}

bool
head::empty() const noexcept
{
	simple_ptr s{ const_cast<head*>(this) };
	do {
		std::tie(s, std::ignore) = s->succ();
	} while (static_cast<elem&>(*s).is_iter());
	return static_cast<elem&>(*s).is_head();
}

size_type
head::size() const noexcept
{
	size_type rv = 0;
	elem_ptr e{ const_cast<head*>(this) };

	for (elem_ptr i = e->succ(); i != e; i = i->succ()) {
		if (std::kill_dependency(i)->is_elem())
			++rv;
	}
	return rv;
}

elem_ptr
head::pop_front() noexcept
{
	for (elem_ptr e = this->succ(); !e->is_head(); e = e->succ()) {
		if (e->is_elem() && e->unlink())
			return std::kill_dependency(e);
	}
	return nullptr;
}

elem_ptr
head::pop_back() noexcept
{
	for (elem_ptr e = this->pred(); !e->is_head(); e = e->pred()) {
		if (e->is_elem() && e->unlink())
			return std::kill_dependency(e);
	}
	return nullptr;
}

bool
head::link_before_(const elem_ptr& pos0, elem* e) noexcept
{
	assert(e && e->is_elem());
	assert(pos0 && (pos0->is_iter() || pos0->is_head()));

	/* Wait until any in-progress unlink operations complete. */
	if (!e->wait_unlinked())
		return false;

	link_result rv;
	do {
		elem_ptr pos = pos0;
		elem_ptr pos_pred;
		for (;;) {
			pos_pred = pos->pred();
			if (pos_pred->is_back_iter() ||
			    (pos_pred->is_head() && pos_pred != pos0))
				pos = std::move(pos_pred);
			else
				break;
		}

		rv = simple_elem::link(simple_ptr{ e },
		    std::make_tuple(std::move(pos_pred), std::move(pos)));
	} while (rv == link_result::RETRY);

	return (rv == link_result::SUCCESS);
}

bool
head::link_after_(const elem_ptr& pos0, elem* e) noexcept
{
	assert(e && e->is_elem());
	assert(pos0 && (pos0->is_iter() || pos0->is_head()));

	/* Wait until any in-progress unlink operations complete. */
	if (!e->wait_unlinked())
		return false;

	link_result rv;
	do {
		elem_ptr pos = pos0;
		elem_ptr pos_succ;
		for (;;) {
			pos_succ = pos->succ();
			if (pos_succ->is_forw_iter() ||
			    (pos_succ->is_head() && pos_succ != pos0))
				pos = std::move(pos_succ);
			else
				break;
		}

		rv = simple_elem::link(simple_ptr{ e },
		    std::make_tuple(std::move(pos), std::move(pos_succ)));
	} while (rv == link_result::RETRY);

	return (rv == link_result::SUCCESS);
}

basic_iter::basic_iter(head& h) noexcept
{
	link_ab_result lr1 = simple_elem::link_after(
	    simple_ptr{ &this->m_forw }, &h);
	link_ab_result lr2 = simple_elem::link_before(
	    simple_ptr{ &this->m_back }, &h);

	assert(lr1 == link_ab_result::SUCCESS &&
	    lr2 == link_ab_result::SUCCESS);
}

basic_iter::basic_iter(const basic_iter& i) noexcept
{
	link_ab_result lr1 = simple_elem::link_after(
	    simple_ptr{ &this->m_forw }, simple_ptr{ &i.m_forw });
	link_ab_result lr2 = simple_elem::link_before(
	    simple_ptr{ &this->m_back }, simple_ptr{ &i.m_back });

	assert(lr1 == lr2 && lr1 != link_ab_result::ALREADY_LINKED);
}

basic_iter::basic_iter(basic_iter&& i) noexcept
{
	link_ab_result lr1 = simple_elem::link_after(
	    simple_ptr{ &this->m_forw }, simple_ptr{ &i.m_forw });
	link_ab_result lr2 = simple_elem::link_before(
	    simple_ptr{ &this->m_back }, simple_ptr{ &i.m_back });

	assert(lr1 == lr2 && lr1 != link_ab_result::ALREADY_LINKED);

	i.m_forw.unlink();
	i.m_back.unlink();
}

basic_iter::~basic_iter() noexcept
{
	this->m_forw.unlink();
	this->m_back.unlink();
}

basic_iter&
basic_iter::operator=(const basic_iter& i) noexcept
{
	if (&i == this)
		return *this;

	this->m_forw.unlink();
	this->m_back.unlink();

	this->m_forw.wait_unlinked();
	link_ab_result lr1 = simple_elem::link_after(
	    simple_ptr{ &this->m_forw },
	    simple_ptr{ &i.m_forw });
	this->m_back.wait_unlinked();
	link_ab_result lr2 = simple_elem::link_before(
	    simple_ptr{ &this->m_back },
	    simple_ptr{ &i.m_back });

	assert(lr1 == lr2 && lr1 != link_ab_result::ALREADY_LINKED);

	return *this;
}

basic_iter&
basic_iter::operator=(basic_iter&& i) noexcept
{
	if (&i == this)
		return *this;

	this->m_forw.unlink();
	this->m_back.unlink();

	this->m_forw.wait_unlinked();
	link_ab_result lr1 = simple_elem::link_after(
	    simple_ptr{ &this->m_forw },
	    simple_ptr{ &i.m_forw });
	this->m_back.wait_unlinked();
	link_ab_result lr2 = simple_elem::link_before(
	    simple_ptr{ &this->m_back },
	    simple_ptr{ &i.m_back });

	assert(lr1 == lr2 && lr1 != link_ab_result::ALREADY_LINKED);

	i.m_forw.unlink();
	i.m_back.unlink();

	return *this;
}

difference_type
distance(const basic_iter& first, const basic_iter& last) noexcept
{
	difference_type rv = 0;
	elem_ptr i{ &first.m_forw };
	elem_ptr e{ &last.m_forw };

	while (i != e) {
		if (std::kill_dependency(i)->is_elem())
			++rv;
		i = i->succ();
	}
	return rv;
}


}} /* namespace ilias::ll_list_detail */
