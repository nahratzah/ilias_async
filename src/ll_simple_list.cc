#include <ilias/detail/ll_simple_list.h>

namespace ilias {
namespace ll_list_detail {
namespace ll_simple_list {


bool
elem::wait_unlinked() const noexcept
{
	std::atomic_thread_fence(std::memory_order_release);

	/* Wait for situation to stabalize. */
	while (this->is_deleted(std::memory_order_acquire));

	return (this->pred() == this && this->succ() == this);
}

void
elem::wait_unused() const noexcept
{
	std::atomic_thread_fence(std::memory_order_release);
	while (this->m_pred_.load_no_acquire(std::memory_order_acquire) !=
	    add_present(this));
	while (this->m_succ_.load_no_acquire(std::memory_order_acquire) !=
	    add_present(this));
	while (this->m_refcnt_.load(std::memory_order_acquire) != 2U);
}

/*
 * Propagate the deleted flag on the successor pointer to the successor.
 *
 * If m_succ is marked deleted, propagate the deleted bit to the successor
 * and clear the deleted bit.
 */
bool
elem::succ_propagate_fl(const data_t& s) const noexcept
{
	assert(std::get<0>(s) != nullptr);

	bool deleted;
	if (std::get<1>(s) == DELETED) {
		deleted = true;
		auto expect = add_present(const_cast<elem*>(this));
		bool cas = std::get<0>(s)->m_pred_.compare_exchange_strong(
		    expect,
		    add_deleted(elem_ptr{ const_cast<elem*>(this) }),
		    std::memory_order_release,
		    std::memory_order_relaxed);
		assert(cas || std::get<1>(expect) == DELETED);
	} else
		deleted = std::get<0>(s)->is_deleted();

	return deleted;
}

elem_ptr
elem::succ() const noexcept
{
	data_t s = this->m_succ_.load(std::memory_order_consume);
	bool deleted = this->succ_propagate_fl(s);

	/* Skip all deleted successors. */
	while (deleted) {
		data_t ss = std::get<0>(s)->m_succ_.load(
		    std::memory_order_consume);
		deleted = std::get<0>(s)->succ_propagate_fl(ss);

		std::get<1>(ss) = PRESENT;
		if (this->m_succ_.compare_exchange_strong(
		    s,
		    ss,
		    std::memory_order_release,
		    std::memory_order_consume))
			s = std::move(ss);
		else
			deleted = this->succ_propagate_fl(s);
	}

	assert(std::get<0>(s) != nullptr);
	return std::get<0>(s);
}

data_t
elem::pred_fl() const noexcept
{
	data_t p = this->m_pred_.load(std::memory_order_consume);

	/*
	 * Search forward to change p from 'any' predecessor
	 * to the direct predecessor of this.
	 */
	if (std::get<1>(p) == PRESENT) {
		for (elem_ptr ps = std::get<0>(p)->succ();
		    std::get<1>(p) == PRESENT && ps != this;
		    ps = std::get<0>(p)->succ()) {
			if (this->m_pred_.compare_exchange_strong(
			    p,
			    add_present(ps),
			    std::memory_order_release,
			    std::memory_order_consume))
				p = add_present(std::move(ps));
		}
	}

	/*
	 * Two cases at the moment:
	 * [1] p is the direct predecessor of this,
	 * [2] this is deleted and p is the direct predecessor
	 *     from the moment of deletion.
	 * In both cases, p may be a deleted element (meaning that our
	 * predecessor pointer is stale).  The way to fix this is the same
	 * for both cases (because [1] was made true by the loop above).
	 */
	while (std::get<0>(p)->is_deleted()) {
		data_t pp = std::get<0>(p)->m_pred_.load(
		    std::memory_order_consume);
		std::get<1>(pp) = std::get<1>(p);
		if (this->m_pred_.compare_exchange_strong(
		    p,
		    pp,
		    std::memory_order_release,
		    std::memory_order_consume))
			p = std::move(pp);
	}

	assert(std::get<0>(p) != nullptr);
	return p;
}

bool
elem::unlink() noexcept
{
	bool result = false;

	/*
	 * Prevent relinking from happening while we are attempting to delete:
	 * the lazy part of the deletion will not complete until the last
	 * reference to this goes away.
	 */
	const elem_ptr self{ this };

	/* Inform predecessor that we are going away. */
	data_t pred = this->pred_fl();
	while (!result &&
	    std::get<1>(pred) != DELETED &&
	    std::get<0>(pred) != this) {
		result = std::get<0>(pred)->m_succ_.compare_exchange_strong(
		    add_present(this),
		    add_deleted(self),
		    std::memory_order_acq_rel,
		    std::memory_order_relaxed);
		if (!result)
			pred = this->pred_fl();
	}
	/* Cannot unlink unlinked element. */
	if (std::get<0>(pred) == this)
		return false;

	/*
	 * Mark ourselves as deleted.
	 *
	 * We try a cas operation:
	 * if it fails some other thread must have updated our state,
	 * since the predecessor is clearly marking us as deleted.
	 */
	std::get<1>(pred) = DELETED;
	auto expect = add_present(std::get<0>(pred).get());
	bool cas = this->m_pred_.compare_exchange_strong(
	    expect,
	    std::move(pred),
	    std::memory_order_release,
	    std::memory_order_relaxed);
	assert(cas || std::get<1>(expect) == DELETED);

	/*
	 * Our predecessor is now no longer referring to us,
	 * time to get our successor to forget about this as well.
	 *
	 * Note that if the successor got deleted while we were
	 * busy getting deleted ourselves, the real successor may
	 * require an update too.
	 */
	data_t s = this->m_succ_.load(std::memory_order_consume);
	bool deleted = this->succ_propagate_fl(s);
	std::get<0>(s)->pred();
	if (deleted)
		this->succ()->pred();

	/*
	 * Done (the rest of the deletion happens lazily when the last elem_ptr
	 * linked to us goes away, at least one of which we own above.
	 */
	return result;
}

link_result
elem::link_between_(std::tuple<elem*, elem*> ins,
    std::tuple<elem_ptr, elem_ptr> pos) noexcept
{
	/*
	 * Conditional assignment.
	 *
	 * Takes a elem_llptr and assigns set,
	 * iff the current value is expected.
	 *
	 * Has prepare/commit semantics, where the whole operation
	 * is automatically rolled back in the destructor
	 * unless the commit() method is called.
	 */
	class cond_assign
	{
	private:
		elem_llptr& p_;
		elem_ptr expect_;
		bool fail_{ true };
		bool commit_{ false };

	public:
		cond_assign(elem_llptr& p, elem_ptr expected, elem_ptr set)
		noexcept
		:	p_{ p },
			expect_{ std::move(expected) }
		{
			elem_llptr::no_acquire_t expect =
			    add_present(this->expect_.get());

			while (!this->p_.compare_exchange_weak(
			    expect,
			    add_present(set),
			    std::memory_order_relaxed,
			    std::memory_order_relaxed)) {
				if (std::get<1>(expect) == PRESENT) {
					assert(this->fail_ == true);
					return;
				}
				expect = add_present(this->expect_.get());
			}

			this->fail_ = false;
		}

		cond_assign(const cond_assign&) = delete;
		cond_assign(cond_assign&&) = delete;
		cond_assign& operator=(const cond_assign&) = delete;

		~cond_assign() noexcept
		{
			if (!this->fail_ && !this->commit_) {
				this->p_.store(
				    add_present(std::move(expect_)));
			}
		}

		void
		commit() noexcept
		{
			assert(!this->fail_);
			this->commit_ = true;
		}

		explicit operator bool() const noexcept
		{
			return !this->fail_;
		}
	};


	/* Argument validation. */
	assert(std::get<0>(ins) != nullptr && std::get<1>(ins) != nullptr &&
	    std::get<0>(pos) != nullptr && std::get<1>(pos) != nullptr);

	/*
	 * Change ins, so it has appropriate links to pos.
	 *
	 * Note that cond_assign will hold a reference to ins.
	 */
	cond_assign ins0_linked{
		std::get<0>(ins)->m_pred_,
		std::get<0>(ins),
		std::get<0>(pos)
	    };
	if (!ins0_linked)
		return link_result::INS0_LINKED;

	cond_assign ins1_linked{
		std::get<1>(ins)->m_succ_,
		std::get<1>(ins),
		std::get<1>(pos)
	    };
	if (!ins1_linked)
		return link_result::INS1_LINKED;

	assert(ins0_linked && ins1_linked);

	/* Change pos0 from pos1 to ins0. */
	if (!std::get<0>(pos)->m_succ_.compare_exchange_strong(
	     add_present(std::get<1>(pos).get()),
	     add_present(std::get<0>(ins)),
	     std::memory_order_release,
	     std::memory_order_relaxed))
		return link_result::INVALID_POS;

	/* Link successful, commit conditional assignments. */
	ins0_linked.commit();
	ins1_linked.commit();

	/* Link complete, fix pos1 predecessor. */
	if (!std::get<1>(pos)->m_pred_.compare_exchange_strong(
	    add_present(std::get<0>(pos).get()),
	    add_present(std::get<1>(ins)),
	    std::memory_order_release,
	    std::memory_order_relaxed))
		std::get<1>(pos)->pred();
	return link_result::SUCCESS;
}

link_result
elem::link_before_(std::tuple<elem*, elem*> ins,
    elem_ptr pos) noexcept
{
	assert(std::get<0>(ins) != nullptr && std::get<1>(ins) != nullptr &&
	    pos != nullptr);

	while (!pos->is_deleted()) {
		auto rv = link_between_(ins,
		    std::make_tuple(pos->pred(), pos));
		switch (rv) {
		default:
			return rv;
		case link_result::INVALID_POS:
			/* Reload pos->pred() in next iteration. */
			break;
		}
	}
	return link_result::INVALID_POS;
}

link_result
elem::link_after_(std::tuple<elem*, elem*> ins,
    elem_ptr pos) noexcept
{
	assert(std::get<0>(ins) != nullptr && std::get<1>(ins) != nullptr &&
	    pos != nullptr);

	while (!pos->is_deleted()) {
		auto rv = link_between_(ins,
		    std::make_tuple(pos, pos->succ()));
		switch (rv) {
		default:
			return rv;
		case link_result::INVALID_POS:
			/* Reload pos->succ() in next iteration. */
			break;
		}
	}
	return link_result::INVALID_POS;
}


}}} /* namespace ilias::ll_list_detail::ll_simple_list */
