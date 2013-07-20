#include <ilias/detail/ll_simple_list.h>

namespace ilias {
namespace ll_list_detail {
namespace ll_simple_list {


inline data_t
elem::succ() const noexcept
{
	data_t succ = this->m_succ_.load(std::memory_order_consume);
	for (;;) {
		assert(std::get<0>(succ));

		/* Test if successor is not being unlinked. */
		data_t succ_succ = std::get<0>(succ)->m_succ_.load(
		    std::memory_order_consume);
		if (std::get<1>(succ_succ_flags) == PRESENT)
			return succ;

		/* Replace direct successor, skipping unlinked successor. */
		data_t succ_assign{
			std::get<0>(succ_succ),
			std::get<1>(succ)
		    };
		if (this->m_succ_.compare_exchange_weak(succ, succ_assign,
		    std::memory_order_consume, std::memory_order_release))
			succ = std::move(succ_assign);
	}
}

inline data_t
elem::pred() const noexcept
{
	data_t pred = this->m_pred_.load(std::memory_order_consume);
	for (;;) {
		assert(std::get<0>(pred));
		if (std::get<0>(pred) == this)
			return pred;

		/* We are deleted, may not search forward. */
		if (std::get<1>(pred) == DELETED) {
			data_t pred_pred = pred->pred();
			if (this->m_pred_.compare_exchange_weak(
			    pred,
			    add_deleted(std::get<0>(pred_pred)),
			    std::memory_order_release,
			    std::memory_order_consume))
				std::get<0>(pred) = std::get<0>(pred_pred);
			continue;
		}

		/*
		 * If pred has this as successor, pred is a direct
		 * predecessor.
		 */
		data_t pred_succ = std::get<0>(pred)->succ();
		if (pred_succ == add_present(this))
			return pred;

		/*
		 * Pred is not a direct predecessor.
		 *
		 * If pred->succ() exists, try that;
		 * otherwise try pred->pred().
		 */
		data_t pred_assign = (std::get<1>(pred_succ) == PRESENT ?
		    pred_succ :
		    std::get<0>(pred)->m_pred_.load(
		     std::memory_order_consume));
		std::get<1>(pred_assign) = PRESENT;

		assert(std::get<0>(pred_assign) != nullptr);
		if (this->m_pred_.compare_exchange_weak(pred, pred_assign,
		    std::memory_order_consume, std::memory_order_release))
			pred = std::move(pred_assign);
	}
}

bool
elem::unlink() noexcept
{
	bool result = false;

	/*
	 * Prevent relinking from happening while we are attempting to delete.
	 */
	const elem_ptr self{ this };

	/* Mark predecessor link as broken. */
	data_t pred = this->pred();
	while (std::get<0>(pred) != this &&
	    std::get<1>(pred) == PRESENT) {
		if (this->m_pred_.compare_exchange_weak(
		    pred,
		    add_deleted(std::get<0>(pred)),
		    std::memory_order_acq_rel,
		    std::memory_order_relaxed)) {
			result = true;
			std::get<1>(pred) = DELETED;
		}
	}

	/* Mark successor link as broken. */
	data_t succ = this->succ();
	while (std::get<0>(succ) != this &&
	    std::get<1>(succ) == PRESENT) {
		if (this->m_succ_.compare_exchange_weak(
		    succ,
		    add_deleted(std::get<0>(succ)),
		    std::memory_order_acq_rel,
		    std::memory_order_relaxed)) {
			std::get<1>(succ) = DELETED;
		}
	}

	/* Make pred skip this. */
	if (pred->m_succ_.compare_exchange_strong(
	    add_present(this),
	    add_present(succ),
	    std::memory_order_release,
	    std::memory_order_relaxed)) {
		/* Do nothing. */
	} else if (pred->m_succ_.compare_exchange_strong(
	    add_deleted(this),
	    add_deleted(succ),
	    std::memory_order_release,
	    std::memory_order_relaxed)) {
		/* Do nothing. */
	} else
		pred->succ();

	/* Make succ skip this. */
	if (succ->m_pred_.compare_exchange_strong(
	    add_present(this),
	    add_present(pred),
	    std::memory_order_release,
	    std::memory_order_relaxed)) {
		/* Do nothing. */
	} else if (succ->m_pred_.compare_exchange_strong(
	    add_deleted(this),
	    add_deleted(pred),
	    std::memory_order_release,
	    std::memory_order_relaxed)) {
		/* Do nothing. */
	} else
		succ->pred();

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

	/* Change ins, so it has appropriate links to pos. */
	cond_assign ins0_linked{
		std::get<0>(ins).m_pred_,
		std::get<0>(ins),
		std::get<0>(pos)
	    };
	if (!ins0_linked)
		return link_result::INS0_LINKED;

	cond_assign ins1_linked{
		std::get<1>(ins).m_succ_,
		std::get<1>(ins),
		std::get<1>(pos)
	    };
	if (!ins1_linked)
		return link_result::INS1_LINKED;

	assert(ins0_linked && ins1_linked);

	/* Change pos0 from pos1 to ins0. */
	if (!std::get<0>(pos).m_succ_.compare_exchange_strong(
	     add_present(std::get<1>(pos).get()),
	     add_present(std::get<0>(ins)),
	     std::memory_order_release,
	     std::memory_order_relaxed))
		return link_result::INVALID_POS;

	/* Link successful, commit conditional assignments. */
	ins0_linked.commit();
	ins1_linked.commit();

	/* Link complete, fix pos1 predecessor. */
	if (!std::get<1>(pos).m_pred_.compare_exchange_strong(
	    add_present(std::get<0>(pos).get()),
	    add_present(std::get<1>(ins)),
	    std::memory_order_release,
	    std::memory_order_relaxed))
		std::get<1>(pos).pred();
	return link_result::SUCCESS;
}

link_result
elem::link_before_(std::tuple<elem*, elem*> ins,
    elem_ptr pos) noexcept
{
	assert(std::get<0>(ins) != nullptr && std::get<1>(ins) != nullptr &&
	    pos != nullptr);

	for (;;) {
		elem_ptr pos_pred;
		flags_t fl;
		std::tie(pos_pred, fl) = pos->pred();
		if (fl == DELETED)
			return link_result::INVALID_POS;

		auto rv = this->link_between(std::move(ins),
		    std::make_tuple(std::move(pos_pred), pos));
		switch (rv) {
		default:
			return rv;
		case link_result::INVALID_POS:
			/* Reload pos->succ() in next iteration. */
			break;
		}
	}
}

link_result
elem::link_after_(std::tuple<elem*, elem*> ins,
    elem_ptr pos) noexcept
{
	assert(std::get<0>(ins) != nullptr && std::get<1>(ins) != nullptr &&
	    pos != nullptr);

	for (;;) {
		elem_ptr pos_succ;
		flags_t fl;
		std::tie(pos_succ, fl) = pos->succ();
		if (fl == DELETED)
			return link_result::INVALID_POS;

		auto rv = this->link_between(std::move(ins),
		    std::make_tuple(pos, std::move(pos_succ)));
		switch (rv) {
		default:
			return rv;
		case link_result::INVALID_POS:
			/* Reload pos->succ() in next iteration. */
			break;
		}
	}
}


}}} /* namespace ilias::ll_list_detail::ll_simple_list */
