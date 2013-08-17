/*
 * Copyright (c) 2012 - 2013 Ariane van der Steldt <ariane@stack.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <ilias/workq.h>
#include <thread>

#if !HAS_TLS
#include "tls_fallback.h"
#endif
#if !HAS_THREAD_LOCAL
#include "thread_local.h"
#endif


#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable: 4290 )
#endif


namespace ilias {


/* Since we need a referable definition of static const somewhere. */
const unsigned int workq_job::STATE_RUNNING;
const unsigned int workq_job::STATE_HAS_RUN;
const unsigned int workq_job::STATE_ACTIVE;

const unsigned int workq_job::TYPE_ONCE;
const unsigned int workq_job::TYPE_PERSIST;
const unsigned int workq_job::TYPE_PARALLEL;
const unsigned int workq_job::TYPE_NO_AID;
const unsigned int workq_job::TYPE_MASK;

const unsigned int workq_job::ACT_IMMED;

const unsigned int ACT_IMMED_MAX_STACK = 64;


workq_error::~workq_error() noexcept
{
	return;
}

workq_deadlock::~workq_deadlock() noexcept
{
	return;
}

void
workq_deadlock::throw_me()
{
	throw workq_deadlock();
}

workq_stack_error::~workq_stack_error() noexcept
{
	return;
}

void
workq_stack_error::throw_me(const std::string& s)
{
	throw workq_stack_error(s);
}

void
workq_stack_error::throw_me(const char* s)
{
	throw workq_stack_error(s);
}


namespace {


class publish_wqs;

struct wq_stack
{
	workq_detail::wq_run_lock lck;
	wq_stack* pred;

	wq_stack(workq_detail::wq_run_lock&&) noexcept;
	~wq_stack() noexcept;

	const workq_detail::workq_intref<workq>&
	get_wq() const noexcept
	{
		return this->lck.get_wq();
	}

	const workq_detail::workq_intref<workq_job>&
	get_wq_job() const noexcept
	{
		return this->lck.get_wq_job();
	}

	const workq_detail::workq_intref<workq_detail::co_runnable>&
	get_co() const noexcept
	{
		return this->lck.get_co();
	}

	workq_detail::wq_run_lock
	steal_lock(const workq_job& j) noexcept
	{
		assert(this->get_wq_job() == &j || this->get_co() == &j);
		assert(this->lck.is_locked() || this->get_co() == &j);	/* Can only steal once. */
		return std::move(this->lck);
	}

	workq_detail::wq_run_lock
	store(workq_detail::wq_run_lock&& rlck) noexcept
	{
		workq_detail::wq_run_lock old = std::move(this->lck);
		this->lck = std::move(rlck);
		return old;
	}


	wq_stack() = delete;
	wq_stack(const wq_stack&) = delete;
	wq_stack& operator=(const wq_stack&) = delete;
};

struct wq_tls
{
friend class publish_wqs;

	workq_service* wqs;
	wq_stack* stack;
	unsigned int stack_depth;

	const wq_stack*
	find(const workq& wq) const noexcept
	{
		for (wq_stack* i = this->stack; i; i = i->pred) {
			if (i->get_wq() == &wq)
				return i;
		}
		return nullptr;
	}

	const wq_stack*
	find(const workq_job& job) const noexcept
	{
		for (wq_stack* i = this->stack; i; i = i->pred) {
			if (i->get_wq_job() == &job)
				return i;
		}
		return nullptr;
	}

	void
	push(wq_stack& s) noexcept
	{
		s.pred = this->stack;
		this->stack = &s;
		++stack_depth;
	}

	void
	pop(wq_stack& s) noexcept
	{
		--stack_depth;
		assert(this->stack == &s);
		this->stack = s.pred;
		s.pred = nullptr;
	}

	workq_detail::wq_run_lock
	steal_lock(const workq_job& j) noexcept
	{
		assert(this->stack);
		return this->stack->steal_lock(j);
	}

	workq_detail::wq_run_lock
	store(workq_detail::wq_run_lock&& rlck) noexcept
	{
		assert(this->stack);
		return this->stack->store(std::move(rlck));
	}

	const workq_detail::workq_intref<workq>&
	get_wq() const noexcept
	{
		static const workq_detail::workq_intref<workq> stack_empty;
		return (this->stack == nullptr ?
		    stack_empty : this->stack->get_wq());
	}

	wq_stack*
	head() const noexcept
	{
		return this->stack;
	}
};

wq_tls&
get_wq_tls()
{
#if HAS_TLS
	static THREAD_LOCAL wq_tls impl;
	return impl;
#else
	static tls<wq_tls> impl;
	return *impl;
#endif
};

class publish_wqs_busy
:	public virtual std::exception
{
public:
	publish_wqs_busy() = default;

	~publish_wqs_busy() noexcept;
	const char* what() const noexcept override;
};

publish_wqs_busy::~publish_wqs_busy() noexcept
{
	/* Empty body. */
}

const char*
publish_wqs_busy::what() const noexcept
{
	return "workq_service: "
	    "current thread already has published workq_service";
}

class publish_wqs
{
private:
	wq_tls& m_tls;
	workq_service*const m_wqs;

public:
	publish_wqs(workq_service& wqs) :
		m_tls(get_wq_tls()),
		m_wqs(&wqs)
	{
		using std::begin;

		if (this->m_tls.wqs)
			throw publish_wqs_busy();
		this->m_tls.wqs = this->m_wqs;
	}

	~publish_wqs() noexcept
	{
		assert(this->m_tls.wqs == this->m_wqs);
		this->m_tls.wqs = nullptr;
	}


	publish_wqs() = delete;
	publish_wqs(const publish_wqs&) = delete;
	publish_wqs& operator=(const publish_wqs&) = delete;
};


inline
wq_stack::wq_stack(workq_detail::wq_run_lock&& lck) noexcept :
	lck(std::move(lck)),
	pred(nullptr)
{
	get_wq_tls().push(*this);
}

inline
wq_stack::~wq_stack() noexcept
{
	get_wq_tls().pop(*this);
}


} /* ilias::<unnamed> */


namespace workq_detail {


wq_run_lock::wq_run_lock(workq_detail::co_runnable& co) noexcept :
	wq_run_lock()
{
	co.m_runcount.fetch_add(1, std::memory_order_acquire);
	this->m_co = &co;
	this->m_wq = co.get_workq();
	this->m_wq_lck = this->m_wq->lock_run_parallel();
	assert(this->m_wq_lck == workq::RUN_PARALLEL);
}

void
wq_run_lock::unlock() noexcept
{
	assert(!this->is_locked() || this->m_commited);

	if (this->m_wq_job && this->m_wq_job_lck != workq_job::BUSY)
		this->m_wq_job->unlock_run(this->m_wq_job_lck);
	if (this->m_wq)
		this->m_wq->unlock_run(this->m_wq_lck);
	if (this->m_co)
		this->m_co->m_runcount.fetch_sub(1, std::memory_order_release);
	this->m_wq.reset();
	this->m_wq_job.reset();
	this->m_co.reset();
	this->m_commited = false;
}

void
wq_run_lock::unlock_wq() noexcept
{
	if (this->m_wq)
		this->m_wq->unlock_run(this->m_wq_lck);
	this->m_wq.reset();
}

bool
wq_run_lock::co_unlock() noexcept
{
	assert(this->m_co);

	const auto old = this->m_co->m_runcount.fetch_sub(1,
	    std::memory_order_release);
	this->m_co.reset();
	this->unlock();
	return (old == 1);
}

bool
wq_run_lock::lock(workq& what) noexcept
{
	assert(!this->m_wq && !this->m_wq_job && !this->m_co);

	this->m_commited = false;
	this->m_wq = &what;
	this->m_wq_lck = this->m_wq->lock_run();

	switch (this->m_wq_lck) {
	case workq::RUN_SINGLE:
		/*
		 * Find a job we can run.
		 *
		 * Loop terminates either with a locked job, or without job.
		 */
		while ((this->m_wq_job = this->m_wq->m_runq.pop_front())) {
			if ((this->m_wq_job_lck = this->m_wq_job->lock_run()) != workq_job::BUSY)
				break;		/* GUARD */
		}

		/* Take job away from parallel runq iff it is parallel. */
		if (this->m_wq_job && (this->m_wq_job->m_type & workq_job::TYPE_PARALLEL))
			this->m_wq->m_p_runq.erase(this->m_wq->m_p_runq.iterator_to(*this->m_wq_job));

		/* Downgrade to parallel lock iff job is a parallel job. */
		if (this->m_wq_job && (this->m_wq_job->m_type & workq_job::TYPE_PARALLEL)) {
			this->m_wq_lck = this->m_wq->lock_run_downgrade(this->m_wq_lck);
			assert(this->m_wq_lck == workq::RUN_SINGLE ||
			    this->m_wq_lck == workq::RUN_PARALLEL);
		}

		break;
	case workq::RUN_PARALLEL:
		/*
		 * Find a parallel job we can run.
		 *
		 * Loop terminates either with a locked job, or without job.
		 */
		while ((this->m_wq_job = this->m_wq->m_p_runq.pop_front())) {
			if ((this->m_wq_job_lck = this->m_wq_job->lock_run()) != workq_job::BUSY)
				break;		/* GUARD */
		}

		/* Take job away from single runq. */
		if (this->m_wq_job)
			this->m_wq->m_runq.erase(this->m_wq->m_runq.iterator_to(*this->m_wq_job));

		break;
	}

	if (!this->is_locked()) {
		this->unlock();
		return false;
	} else
		assert(this->m_wq_job->is_running());
	return true;
}

bool
wq_run_lock::lock(workq_job& what) noexcept
{
	assert(!this->m_wq && !this->m_wq_job && !this->m_co);

	this->m_commited = false;
	this->m_wq = what.get_workq();

	/* Acquire proper lock type on workq. */
	if (what.m_type & workq_job::TYPE_PARALLEL) {
		this->m_wq_lck = this->m_wq->lock_run_parallel();
		if (this->m_wq_lck != workq::RUN_PARALLEL) {
			this->unlock();
			return false;
		}
	} else {
		this->m_wq_lck = this->m_wq->lock_run();
		if (this->m_wq_lck != workq::RUN_SINGLE) {
			this->unlock();
			return false;
		}
	}

	/* Acquire run lock for the given job. */
	this->m_wq_job = &what;
	this->m_wq_job_lck = this->m_wq_job->lock_run();

	if (!this->is_locked()) {
		this->unlock();
		return false;
	} else
		assert(this->m_wq_job->is_running());

	/* Take job from the runqs. */
	this->m_wq->m_runq.erase(this->m_wq->m_runq.iterator_to(*this->m_wq_job));
	if (this->m_wq_job->m_type & workq_job::TYPE_PARALLEL)
		this->m_wq->m_p_runq.erase(this->m_wq->m_p_runq.iterator_to(*this->m_wq_job));

	return true;
}

bool
wq_run_lock::lock(workq_service& wqs) noexcept
{
	assert(!this->m_wq && !this->m_wq_job && !this->m_co);

	/*
	 * Fetch a workq and hold on to it.
	 *
	 * Loop terminates when either we manage to lock a job on a workq,
	 * or when the runq is depleted.
	 */
	auto& runq_iter = wqs.get_runq_iterpos();
	auto wq = (++runq_iter).get();
	for (;;) {
		if (!wq) {
			runq_iter = wqs.m_wq_runq.begin();
			wq = runq_iter.get();
			if (!wq)
				break;	/* GUARD */
		}

		if (this->lock(*wq)) {
			/* Acquired a job: workq may stay on the runq. */
			break;		/* GUARD */
		} else {
			/*
			 * No job acquired, workq is depleted and
			 * must be removed.
			 */
			wqs.m_wq_runq.erase(runq_iter);

			/*
			 * Retest to see if the workq has a job.
			 *
			 * This test is important, because without it there
			 * will be a race condition where the job acquires new
			 * workq between the lock (above) and the erase call
			 * we just did.
			 */
			if (this->lock(*wq)) {
				wqs.m_wq_runq.insert(runq_iter, std::move(wq));
				wqs.wakeup();
				break;
			}
		}
	}
	runq_iter.release();
	return this->is_locked();
}

/*
 * Acquire a specific lock on only this workq.
 *
 * XXX needs to have priority over run threads for locking run-single.
 */
void
wq_run_lock::lock_wq(workq& what, workq::run_lck how) noexcept
{
	assert(!this->m_wq);	/* May not hold a workq lock. */

	for (;;) {
		switch (how) {
		case workq::RUN_SINGLE:
			this->m_wq_lck = what.lock_run();
			break;
		case workq::RUN_PARALLEL:
			this->m_wq_lck = what.lock_run_parallel();
			break;
		}

		if (this->m_wq_lck == how)
			break;		/* GUARD */

		what.unlock_run(this->m_wq_lck);
		std::this_thread::yield();
	}

	this->m_wq = &what;
}


} /* namespace ilias::workq_detail */


void
workq_detail::workq_int::wait_unreferenced() const noexcept
{
	while (this->int_refcnt.load(std::memory_order_acquire) > 0)
		std::this_thread::yield();
}


workq_service_ptr
new_workq_service() throw (std::bad_alloc)
{
	return workq_service_ptr(new workq_service());
}


workq_job::workq_job(workq_ptr wq, unsigned int type)
    throw (std::invalid_argument) :
	m_type(type),
	m_run_gen(0),
	m_state(0),
	m_wq(std::move(wq))
{
	if (!this->m_wq)
		throw std::invalid_argument("workq_job: null workq");
	if ((type & TYPE_ONCE) && (type & TYPE_PERSIST)) {
		throw std::invalid_argument("workq_job: "
		    "cannot create persistent job that only runs once");
	}
	if ((type & TYPE_MASK) != type) {
		throw std::invalid_argument("workq_job: "
		    "invalid type (unrecognized flags)");
	}
}

workq_job::~workq_job() noexcept
{
	assert(!(this->m_state.load(std::memory_order_relaxed) &
	    STATE_RUNNING));
}

void
workq_job::activate(unsigned int flags) noexcept
{
	const auto s = this->m_state.fetch_or(STATE_ACTIVE,
	    std::memory_order_relaxed);
	if (!(s & (STATE_RUNNING | STATE_ACTIVE)))
		this->get_workq()->job_to_runq(this);

	if (flags & ACT_IMMED && !(this->m_type & TYPE_NO_AID)) {
		if (get_wq_tls().stack_depth >= ACT_IMMED_MAX_STACK)
			return;

		workq_detail::wq_run_lock rlck(*this);
		if (rlck.is_locked()) {
			assert(rlck.get_wq_job().get() == this);
			rlck.commit();	/* XXX remove commit requirement? */
			wq_stack stack(std::move(rlck));
			this->run();
		}
	}
}

void
workq_job::deactivate() noexcept
{
	const auto gen = this->m_run_gen.load(std::memory_order_relaxed);
	auto s = this->m_state.fetch_and(~STATE_ACTIVE,
	    std::memory_order_release);

	if ((s & STATE_RUNNING) && get_wq_tls().find(*this))
		return;	/* Deactivated from within. */

	while ((s & STATE_RUNNING) &&
	    gen == this->m_run_gen.load(std::memory_order_relaxed)) {
		std::this_thread::yield();
		s = this->m_state.load(std::memory_order_relaxed);
	}
}

const workq_ptr&
workq_job::get_workq() const noexcept
{
	return this->m_wq;
}

const workq_service_ptr&
workq_job::get_workq_service() const noexcept
{
	return this->get_workq()->get_workq_service();
}

workq_job::run_lck
workq_job::lock_run() noexcept
{
	auto s = this->m_state.load(std::memory_order_relaxed);
	decltype(s) new_s;

	do {
		if (!(s & STATE_ACTIVE))
			return BUSY;
		if (s & STATE_RUNNING)
			return BUSY;
		if ((this->m_type & TYPE_ONCE) && (s & STATE_HAS_RUN))
			return BUSY;

		new_s = s | STATE_RUNNING;
		if (!(this->m_type & TYPE_PERSIST))
			new_s &= ~STATE_ACTIVE;
	} while (!this->m_state.compare_exchange_weak(s, new_s,
	    std::memory_order_acquire, std::memory_order_relaxed));

	this->m_run_gen.fetch_add(1, std::memory_order_acquire);
	return RUNNING;
}

void
workq_job::unlock_run(workq_job::run_lck rl) noexcept
{
	switch (rl) {
	case RUNNING:
		{
			auto s = this->m_state.fetch_and(~STATE_RUNNING,
			    std::memory_order_release);
			assert(s & STATE_RUNNING);
			if (this->m_type & TYPE_ONCE)
				return;
			if (s & STATE_ACTIVE)
				this->get_workq()->job_to_runq(this);
		}
		break;
	case BUSY:
		break;
	}
}


workq_detail::co_runnable::~co_runnable() noexcept
{
	return;
}

workq_detail::co_runnable::co_runnable(workq_ptr wq, unsigned int type)
    throw (std::invalid_argument)
:	workq_job(std::move(wq), type),
	m_runcount(0)
{
	/* Empty body. */
}

void
workq_detail::co_runnable::co_publish(std::size_t runcount) noexcept
{
	if (runcount > 0) {
		this->m_rlck = get_wq_tls().steal_lock(*this);
		this->m_runcount.exchange(runcount, std::memory_order_acq_rel);
		this->get_workq_service()->co_to_runq(this, runcount);
	} else {
		/* Not publishing co-runnable, not eating lock,
		 * co-runnable will unlock on return. */
	}
}

void
workq_detail::co_runnable::unlock_run(workq_job::run_lck rl) noexcept
{
	switch (rl) {
	case RUNNING:
		/* Handled by co_runnable::release,
		 * which will be called from co_run() as appropriate. */
		return;
	case BUSY:
		break;
	}
	this->workq_job::unlock_run(rl);
}

bool
workq_detail::co_runnable::release(std::size_t n) noexcept
{
	bool did_unlock = false;

	/*
	 * When release is called, the co-runnable cannot start more work.
	 * It must be unlinked from the co-runq.
	 *
	 * Note that this call will fail a lot, because multiple threads
	 * will attempt this operation, but only one will succeed
	 * (which is fine, we simply don't want it to keep appearing on the
	 * co-runq).
	 *
	 * Note: this call must complete before the co-runnable ceases to run,
	 * otherwise a race could cause co-runnable insertion to fail
	 * when it is next activated.
	 */
	this->get_workq_service()->m_co_runq.erase(
	    this->get_workq_service()->m_co_runq.iterator_to(*this));

	assert(this->m_rlck.is_locked());
	std::atomic_thread_fence(std::memory_order_release);
	if (get_wq_tls().steal_lock(*this).co_unlock()) {
		get_wq_tls().store(std::move(this->m_rlck));
		did_unlock = true;
		std::atomic_thread_fence(std::memory_order_acquire);
	}
	return did_unlock;
}


workq::workq(workq_service_ptr wqs) throw (std::invalid_argument)
:	m_wqs(std::move(wqs)),
	m_run_single(false),
	m_run_parallel(0)
{
	if (!this->m_wqs)
		throw std::invalid_argument("workq: null workq service");
}

workq::~workq() noexcept
{
	assert(this->m_runq.empty());
	assert(!this->m_run_single.load(std::memory_order_acquire));
	assert(this->m_run_parallel.load(std::memory_order_acquire) == 0);
}

const workq_service_ptr&
workq::get_workq_service() const noexcept
{
	return this->m_wqs;
}

workq_ptr
workq::get_current() noexcept
{
	return get_wq_tls().get_wq();
}

void
workq::job_to_runq(workq_detail::workq_intref<workq_job> j) noexcept
{
	bool activate = false;
	if ((j->m_type & workq_job::TYPE_PARALLEL) &&
	    this->m_p_runq.push_back(j))
		activate = true;
	if (this->m_runq.push_back(std::move(j)))
		activate = true;

	if (activate)
		this->get_workq_service()->wq_to_runq(this);
}

workq::run_lck
workq::lock_run() noexcept
{
	if (!this->m_run_single.exchange(true, std::memory_order_acquire))
		return RUN_SINGLE;
	this->m_run_parallel.fetch_add(1, std::memory_order_acquire);
	return RUN_PARALLEL;
}

workq::run_lck
workq::lock_run_parallel() noexcept
{
	this->m_run_parallel.fetch_add(1, std::memory_order_acquire);
	return RUN_PARALLEL;
}

void
workq::unlock_run(workq::run_lck rl) noexcept
{
	switch (rl) {
	case RUN_SINGLE:
		{
			const auto old_run_single =
			    this->m_run_single.exchange(false,
			    std::memory_order_release);
			assert(old_run_single);
		}
		break;
	case RUN_PARALLEL:
		{
			const auto old_run_parallel =
			    this->m_run_parallel.fetch_sub(1,
			    std::memory_order_release);
			assert(old_run_parallel > 0);
		}
		break;
	}
}

workq::run_lck
workq::lock_run_downgrade(workq::run_lck rl) noexcept
{
	switch (rl) {
	case RUN_SINGLE:
		{
			this->m_run_parallel.fetch_add(1,
			    std::memory_order_acquire);
			const auto old_run_single =
			    this->m_run_single.exchange(false,
			    std::memory_order_release);
			assert(old_run_single);

			rl = RUN_PARALLEL;
		}
		break;
	case RUN_PARALLEL:
		break;
	}
	return rl;
}

bool
workq::aid(unsigned int count) noexcept
{
	unsigned int i;
	for (i = 0; i < count; ++i) {
		workq_detail::wq_run_lock rlck(*this);
		if (!rlck.is_locked())
			break;

		rlck.commit();
		auto job = rlck.get_wq_job();
		wq_stack stack(std::move(rlck));
		job->run();
	}
	return (i > 0);
}


workq_service::threadpool_client::~threadpool_client() noexcept
{
	/* Empty body. */
}

bool
workq_service::threadpool_client::do_work() noexcept
{
	workq_detail::workq_intref<workq_service> wqs;
	{
		threadpool_client_lock lck{ *this };
		if (!this->has_client())
			return false;
		wqs = &this->m_self;
	}

	try {
		publish_wqs pub{ *wqs };
		return wqs->aid(32);
	} catch (const publish_wqs_busy&) {
		/* Disallow recursion. */
		return false;
	}
}

bool
workq_service::threadpool_client::has_work() noexcept
{
	threadpool_client_lock lck{ *this };
	return (this->has_client() && !this->m_self.empty());
}

workq_service::wq_runq::iterator&
workq_service::get_runq_iterpos() noexcept
{
	using runq_iterator = wq_runq::iterator;
	using tls_type = std::tuple<runq_iterator, workq_service*>;

#if HAS_THREAD_LOCAL
	static thread_local tls_type m_impl;
	tls_type& tls = m_impl;
#else
	static tls_cd<tls_type> m_impl;
	tls_type& tls = *m_impl;
#endif

	if (std::get<1>(tls) != this) {
		std::get<0>(tls) = this->m_wq_runq.begin();
		std::get<1>(tls) = this;
	}
	return std::get<0>(tls);
}

workq_service::workq_service()
{
	return;
}

workq_service::~workq_service() noexcept
{
	this->m_wq_runq.clear();
	this->m_co_runq.clear();

	atomic_store(&this->m_wakeup_cb, nullptr);
}

void
workq_service::wq_to_runq(workq_detail::workq_intref<workq> wq) noexcept
{
	/* Load insert position suitable for this thread. */
	auto ipos = this->get_runq_iterpos();
	this->m_wq_runq.insert(++ipos, wq);
	this->wakeup();
}

void
workq_service::co_to_runq(
    workq_detail::workq_intref<workq_detail::co_runnable> co,
    std::size_t max_threads) noexcept
{
	assert(max_threads > 0);
	const bool pushback_succeeded = this->m_co_runq.push_back(co);
	assert(pushback_succeeded);
	this->wakeup(max_threads);
}

void
workq_service::wakeup(std::size_t count) noexcept
{
	/*
	 * During this call, workq_service is guaranteed referenced by at least
	 * one workq_service_ptr, therefore the pointer can be safely
	 * constructed as argument to the callback.
	 */
	auto cb = atomic_load(&this->m_wakeup_cb);
	if (count > threadpool_client_intf::WAKE_ALL)
		count = threadpool_client_intf::WAKE_ALL;
	if (cb) {
		if (cb->has_service())
			cb->wakeup(count);
		else
			atomic_store(&this->m_wakeup_cb, nullptr);
	}
}

workq_ptr
workq_service::new_workq() throw (std::bad_alloc)
{
	return workq_ptr(new workq(this));
}

bool
workq_service::aid(unsigned int count) noexcept
{
	using std::begin;
	using std::end;
	using workq_detail::workq_intref;
	using workq_detail::co_runnable;

	unsigned int i;

	for (i = 0; i < count; ++i) {
		/* Run co-runnables before workqs. */
		if (!this->m_co_runq.empty()) {
			auto co = begin(this->m_co_runq);
			bool ran = false;
			while (co.get() && i < count) {
				/* Acquire lock and
				 * publish intent to execute. */
				wq_stack stack{ *co };

				if ((ran = co->co_run()))
					++i;
				++co;
			}

			/* Retest counter. */
			continue;
		}

		/* Run a workq. */
		workq_detail::wq_run_lock rlck{ *this };
		if (!rlck.is_locked()) {
			/* GUARD: No co-runnables, nor workqs available. */
			break;
		}

		{
			rlck.commit();
			auto job = rlck.get_wq_job();
			wq_stack stack(std::move(rlck));
			job->run();
		}
	}

	return (i > 0);
}

bool
workq_service::empty() const noexcept
{
	return (this->m_wq_runq.empty() && this->m_co_runq.empty());
}


namespace workq_detail {


void
wq_deleter::operator()(const workq_job* wqj) const noexcept
{
	wqj->get_workq()->m_runq.erase(
	    wqj->get_workq()->m_runq.iterator_to(
	    const_cast<workq_job&>(*wqj)));
	wqj->get_workq()->m_p_runq.erase(
	    wqj->get_workq()->m_p_runq.iterator_to(
	    const_cast<workq_job&>(*wqj)));
	const_cast<workq_job*>(wqj)->deactivate();

	/*
	 * If this job is being destroyed from within its own worker thread,
	 * inform the internal references that they are responsible
	 * for destruction.
	 */
	if (get_wq_tls().find(*wqj)) {
		wqj->int_suicide.store(true, std::memory_order_release);
		return;
	}

	/* Wait until the last reference to this job goes away. */
	wqj->wait_unreferenced();

	delete wqj;
}

void
wq_deleter::operator()(const workq* wq) const noexcept
{
	/*
	 * Workq are lazily destroyed: they hold no observable state.
	 * If the workq exists on a runq, the action of trying to run
	 * it will release the workq and cause it to be destroyed.
	 */
	workq_intref<const workq> wq_ptr{ wq };
	wq->int_suicide.store(true, std::memory_order_release);
}

void
wq_deleter::operator()(const workq_service* wqs) const noexcept
{
	/* Kill link to threadpool service provider. */
	atomic_store(&const_cast<workq_service*>(wqs)->m_wakeup_cb, nullptr);

	/*
	 * Check if this wqs is being destroyed from within
	 * its own worker thread, then perform special handling.
	 *
	 * Uses publish_wqs provided tls data.
	 */
	auto& tls = get_wq_tls();
	if (tls.wqs == wqs) {
		wqs->int_suicide.store(true, std::memory_order_release);
		return;
	}

	delete wqs;
}


} /* namespace ilias::workq_detail */


class ILIAS_ASYNC_LOCAL job_single
:	public workq_job
{
private:
	const std::function<void()> m_fn;

public:
	job_single(workq_ptr wq, std::function<void()> fn,
	    unsigned int type = 0) throw (std::invalid_argument)
	:	workq_job(std::move(wq), type),
		m_fn(std::move(fn))
	{
		if (!this->m_fn) {
			throw std::invalid_argument("workq_job: "
			    "functor invalid");
		}
	}

	virtual ~job_single() noexcept;
	virtual void run() noexcept override;
};

job_single::~job_single() noexcept
{
	return;
}

void
job_single::run() noexcept
{
	this->m_fn();
}

workq_job_ptr
workq::new_job(unsigned int type, std::function<void()> fn)
    throw (std::bad_alloc, std::invalid_argument)
{
	return new_workq_job<job_single>(workq_ptr(this), std::move(fn), type);
}


class ILIAS_ASYNC_LOCAL coroutine_job
:	public workq_detail::co_runnable
{
private:
	typedef std::vector<std::function<void()> > co_list;

	const co_list m_coroutines;
	std::atomic<co_list::size_type> m_co_idx;

public:
	coroutine_job(workq_ptr ptr, std::vector<std::function<void()> > fns,
	    unsigned int type) throw (std::invalid_argument)
	:	workq_detail::co_runnable(std::move(ptr), type),
		m_coroutines(std::move(fns))
	{
		/* Validate co-routines. */
		if (this->m_coroutines.empty()) {
			throw std::invalid_argument("workq coroutine job: "
			    "no functors");
		}
		std::for_each(this->m_coroutines.begin(),
		    this->m_coroutines.end(),
		    [](const std::function<void()>& fn) {
			if (!fn) {
				throw std::invalid_argument(
				    "workq coroutine job: invalid functor");
			}
		});
	}

	virtual ~coroutine_job() noexcept;
	virtual void run() noexcept override;
	virtual bool co_run() noexcept override;
};

coroutine_job::~coroutine_job() noexcept
{
	return;
}

void
coroutine_job::run() noexcept
{
	this->m_co_idx.exchange(0, std::memory_order_acq_rel);
	this->co_publish(this->m_coroutines.size());
}

bool
coroutine_job::co_run() noexcept
{
	std::size_t runcount = 0;
	co_list::size_type idx;
	while ((idx = this->m_co_idx.fetch_add(1, std::memory_order_acquire)) <
	    this->m_coroutines.size()) {
		++runcount;
		this->m_coroutines[idx]();
	}
	this->release(runcount);
	return runcount > 0;
}

workq_job_ptr
workq::new_job(unsigned int type, std::vector<std::function<void()> > fn)
    throw (std::bad_alloc, std::invalid_argument)
{
	if (fn.empty())
		throw std::invalid_argument("new_job: empty co-routine");
	if (fn.size() == 1) {
		/* Use simpler job type if there is only one function. */
		return this->new_job(type, std::move(fn.front()));
	}

	return new_workq_job<coroutine_job>(this, std::move(fn), type);
}


template<typename JobType>
class ILIAS_ASYNC_LOCAL job_once final :
	public JobType,
	public std::enable_shared_from_this<job_once<JobType> >
{
public:
	std::shared_ptr<job_once> m_self;

	template<typename FN>
	job_once(workq_ptr ptr, FN&& fn) :
		JobType(std::move(ptr), std::forward<FN>(fn),
		    workq_job::TYPE_ONCE)
	{
		assert(this->m_type & workq_job::TYPE_ONCE);
	}

	virtual void
	run() noexcept override
	{
		/* Release internal reference to self. */
		m_self = nullptr;
		/* Run the function. */
		this->JobType::run();
	}
};

void
workq::once(std::function<void()> fn)
    throw (std::bad_alloc, std::invalid_argument)
{
	/* Create a job that will run once and then kill itself. */
	auto j =
	    new_workq_job<job_once<job_single> >(this, std::move(fn));
	j->m_self = j;	/* Self reference, will be broken by run(). */

	/* May not throw past this point. */

	/* Activate this job, so it will run. */
	do_noexcept([&]() {
		j->activate();
	    });
}

void
workq::once(std::vector<std::function<void()> > fns)
    throw (std::bad_alloc, std::invalid_argument)
{
	/* Create a job that will run once and then kill itself. */
	auto j = new_workq_job<job_once<coroutine_job> >(this,
	    std::move(fns));
	j->m_self = j;	/* Self reference, will be broken by run(). */

	/* May not throw past this point. */

	/* Activate this job, so it will run. */
	do_noexcept([&]() {
		j->activate();	/* Never throws */
	    });
}


/*
 * Switch to destination workq at the specified run level.
 *
 * Returns the previous run level.
 */
workq_pop_state
workq_switch(const workq_pop_state& dst)
    throw (workq_deadlock, workq_stack_error)
{
	auto& tls = get_wq_tls();
	wq_stack*const head = tls.head();

	if (!head) {
		workq_stack_error::throw_me("workq_switch: "
		    "require active workq invocation to switch stacks "
		    "(otherwise it is impossible to know "
		    "when the stack frame ends)");	/* Nothing to lock. */
	}

	workq_detail::wq_run_lock& lck = head->lck;
	workq_pop_state rv(lck.get_wq(),
	    (lck.wq_is_single() ? workq::RUN_SINGLE : workq::RUN_PARALLEL));

	/* Request is to release the lock. */
	if (!dst.get_workq()) {
		lck.unlock_wq();
		return rv;
	}

	/*
	 * Request is to change the lock level of the workq.
	 * This can only deadlock if the current workq is changed from
	 * run-parallel to run-single.
	 */
	if (lck.get_wq() == dst.get_workq()) {
		if (dst.is_single() == lck.wq_is_single())
			return rv;	/* No change. */
		if (!dst.is_single()) {
			lck.wq_downgrade();
			return rv;	/* Downgrade never fails. */
		}
	}

	/*
	 * Check for deadlock: deadlock can occur if the request is to lock for
	 * run-single, while a previous invocation already has run-single.
	 */
	if (dst.is_single()) {
		for (const wq_stack* s = head->pred; s; s = s->pred) {
			if (s->get_wq() == dst.get_workq() &&
			    s->lck.wq_is_single()) {
				/* Recursive lock -> deadlock. */
				workq_deadlock::throw_me();
			}
		}
	}

	/* Deadlock has been checked and is guaranteed not to happen. */
	lck.unlock_wq();
	lck.lock_wq(*dst.get_workq(),
	    (dst.is_single() ? workq::RUN_SINGLE : workq::RUN_PARALLEL));

	return rv;
}


/*
 * Implement locking for libraries missing
 * atomic operations on shared pointers.
 */
#if !ILIAS_ASYNC_HAS_ATOMIC_SHARED_PTR
namespace workq_detail {
namespace {


const unsigned int N_ATOMS = 16;
std::atomic<bool> job_atomics[N_ATOMS];

unsigned int
job_atomics_idx(const void* p)
{
	return std::hash<void*>()(const_cast<void*>(p)) & (N_ATOMS - 1);
}

} /* namespace ilias::workq_detail::<unnamed> */

/*
 * Lock external spinlock for shared pointer,
 * if std::shared_ptr has no atomic operations.
 */
unsigned int
atom_lck::atomic_lock_jobptr(const void* p) noexcept
{
	/* Lookup index and global lock. */
	const unsigned int idx = job_atomics_idx(p);
	auto& atom = job_atomics[idx];

	bool expect = false;
	while (atom.compare_exchange_weak(expect, true,
	    std::memory_order_acquire, std::memory_order_relaxed))
		expect = false;

	return idx;
}

/*
 * Unlock external spinlock for shared pointer.
 */
void
atom_lck::atomic_unlock_jobptr(unsigned int idx) noexcept
{
	assert(idx < N_ATOMS);
	auto& atom = job_atomics[idx];

	auto orig = atom.exchange(false, std::memory_order_release);
	assert(orig);
}


} /* namespace ilias::workq_detail */
#endif /* !ILIAS_ASYNC_ATOMIC_SHARED_PTR */


template void threadpool_attach<workq_service, tp_aid_service>(
    workq_service&, tp_aid_service&);
template void threadpool_attach<workq_service, tp_service_multiplexer>(
    workq_service&, tp_service_multiplexer&);
template void threadpool_attach<workq_service, tp_client_multiplexer>(
    workq_service&, tp_client_multiplexer&);


} /* namespace ilias */
