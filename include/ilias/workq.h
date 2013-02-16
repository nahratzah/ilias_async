/*
 * Copyright (c) 2012 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef ILIAS_WORKQ_H
#define ILIAS_WORKQ_H

#include <ilias/ilias_async_export.h>
#include <ilias/ll.h>
#include <ilias/refcnt.h>
#include <ilias/threadpool.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <memory>
#include <utility>
#include <vector>


#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable: 4275 )
#pragma warning( disable: 4290 )
#endif


namespace ilias {


class workq_job;
class workq;
class workq_service;
class workq_pop_state;

typedef refpointer<workq> workq_ptr;
typedef refpointer<workq_service> workq_service_ptr;


class ILIAS_ASYNC_EXPORT workq_error :
	public std::runtime_error
{
public:
	explicit workq_error(const std::string& s) :
		std::runtime_error(s)
	{
		/* Empty body. */
	}

	explicit workq_error(const char* s) :
		std::runtime_error(s)
	{
		/* Empty body. */
	}

	virtual ~workq_error() noexcept;
};

class ILIAS_ASYNC_EXPORT workq_deadlock :
	public workq_error
{
public:
	workq_deadlock() :
		workq_error("workq deadlock detected")
	{
		/* Empty body. */
	}

	static void throw_me();
	virtual ~workq_deadlock() noexcept;
};

class ILIAS_ASYNC_EXPORT workq_stack_error :
	public workq_error
{
public:
	explicit workq_stack_error(const std::string& s) :
		workq_error(s)
	{
		/* Empty body. */
	}

	explicit workq_stack_error(const char* s) :
		workq_error(s)
	{
		/* Empty body. */
	}

	static void throw_me(const std::string&);
	static void throw_me(const char*);
	virtual ~workq_stack_error() noexcept;
};


ILIAS_ASYNC_EXPORT workq_service_ptr new_workq_service() throw (std::bad_alloc);
ILIAS_ASYNC_EXPORT workq_service_ptr new_workq_service(unsigned int threads) throw (std::bad_alloc);
ILIAS_ASYNC_EXPORT workq_pop_state workq_switch(const workq_pop_state&) throw (workq_deadlock, workq_stack_error);


namespace workq_detail {


struct runq_tag {};
struct coroutine_tag {};
struct parallel_tag {};


struct wq_deleter;
template<typename Type> struct workq_intref_mgr;

class workq_int
{
template<typename Type> friend struct workq_intref_mgr;
friend struct wq_deleter;

private:
	mutable std::atomic<std::uintptr_t> int_refcnt;
	mutable std::atomic<bool> int_suicide;

protected:
	workq_int() noexcept :
		int_refcnt(0),
		int_suicide(0)
	{
		return;
	}

	workq_int(const workq_int&) noexcept :
		workq_int()
	{
		return;
	}

	~workq_int() noexcept
	{
		assert(int_refcnt == 0);
	}

	workq_int&
	operator=(const workq_int&) noexcept
	{
		return *this;
	}

	ILIAS_ASYNC_LOCAL void wait_unreferenced() const noexcept;
};

template<typename Type>
struct workq_intref_mgr
{
	void
	acquire(const Type& v) noexcept
	{
		const workq_int& i = v;
		const auto o = i.int_refcnt.fetch_add(1, std::memory_order_acquire);
		assert(o + 1 != 0);
	}

	void
	release(const Type& v) noexcept
	{
		const workq_int& i = v;
		const auto o = i.int_refcnt.fetch_sub(1, std::memory_order_release);
		assert(o > 0);

		if (o == 0 && i.int_suicide.load(std::memory_order_acquire))	/* XXX consume? */
			delete &v;
	}
};

template<typename Type> using workq_intref = refpointer<Type, workq_intref_mgr<Type> >;

/* Deleter for workq and workq_service. */
struct wq_deleter
{
	ILIAS_ASYNC_EXPORT void operator()(const workq_job*) const noexcept;
	ILIAS_ASYNC_EXPORT void operator()(const workq*) const noexcept;
	ILIAS_ASYNC_EXPORT void operator()(const workq_service*) const noexcept;
};


class wq_run_lock;

#if !ILIAS_ASYNC_HAS_ATOMIC_SHARED_PTR
class atom_lck
{
private:
	const unsigned int m_idx;

	static unsigned int atomic_lock_jobptr(const void*) noexcept;
	static void atomic_unlock_jobptr(unsigned int) noexcept;

public:
	atom_lck() = delete;
	atom_lck(const atom_lck&) = delete;
	atom_lck& operator=(const atom_lck&) = delete;
	atom_lck(atom_lck&&) = delete;

	template<typename T>
	atom_lck(const std::shared_ptr<T>& ptr) noexcept
	:	m_idx(atomic_lock_jobptr(&ptr))
	{
		/* Empty body. */
	}

	~atom_lck() noexcept
	{
		atomic_unlock_jobptr(this->m_idx);
	}
};
#endif


} /* namespace ilias::workq_detail */


typedef std::shared_ptr<workq_job> workq_job_ptr;

/*
 * Atomically load shared pointer to job.
 */
template<typename T>
std::shared_ptr<T>
atomic_load_jobptr(const std::shared_ptr<T>& p,
    std::memory_order mo = std::memory_order_seq_cst) noexcept
{
#if ILIAS_ASYNC_HAS_ATOMIC_SHARED_PTR
	return std::atomic_load_explicit(&p, mo);
#else
	workq_detail::atom_lck lck{ p };
	return p;
#endif
}

/*
 * Atomically exchange shared pointer to job.
 */
template<typename T>
std::shared_ptr<T>
atomic_exchange_jobptr(std::shared_ptr<T>& p, std::shared_ptr<T> new_val,
    std::memory_order mo = std::memory_order_seq_cst) noexcept
{
#if ILIAS_ASYNC_HAS_ATOMIC_SHARED_PTR
	return std::atomic_exchange_explicit(&p, std::move(new_val), mo);
#else
	using std::swap;

	workq_detail::atom_lck lck{ p };
	swap(p, new_val);
	return new_val;
#endif
}

template<typename JobType, typename... Args>
std::shared_ptr<JobType>
new_workq_job(workq_ptr wq, Args&&... args)
{
	return std::shared_ptr<JobType>(new JobType(std::move(wq),
	    std::forward<Args>(args)...), workq_detail::wq_deleter());
}

/* Thread-safe job activation. */
template<typename JobType>
void
workq_activate(const std::shared_ptr<JobType>& j, unsigned int how = 0) noexcept
{
	auto j_ = atomic_load_jobptr(j, std::memory_order_relaxed);
	if (j_)
		j_->activate(how);
}

/* Thread-safe job deactivation. */
template<typename JobType>
void
workq_deactivate(const std::shared_ptr<JobType>& j) noexcept
{
	auto j_ = atomic_load_jobptr(j, std::memory_order_relaxed);
	if (j_)
		j_->deactivate();
}


class workq_job :
	public workq_detail::workq_int,
	public ll_base_hook<workq_detail::runq_tag>,
	public ll_base_hook<workq_detail::parallel_tag>
{
friend class workq;	/* Because MSVC and GCC cannot access private types in friend definitions. :P */
friend class workq_service;
friend class workq_detail::wq_run_lock;
friend struct workq_detail::workq_intref_mgr<workq_job>;
friend void workq_detail::wq_deleter::operator()(const workq_job*) const noexcept;

public:
	enum run_lck {
		RUNNING,
		BUSY
	};

	static const unsigned int STATE_RUNNING = 0x0001;
	static const unsigned int STATE_HAS_RUN = 0x0002;
	static const unsigned int STATE_ACTIVE = 0x0004;

	static const unsigned int TYPE_ONCE = 0x0001;
	static const unsigned int TYPE_PERSIST = 0x0002;
	static const unsigned int TYPE_PARALLEL = 0x0004;
	static const unsigned int TYPE_NO_AID = 0x0010;
	static const unsigned int TYPE_MASK = (TYPE_ONCE | TYPE_PERSIST | TYPE_PARALLEL | TYPE_NO_AID);

	static const unsigned int ACT_IMMED = 0x0001;

	const unsigned int m_type;

private:
	mutable std::atomic<unsigned int> m_run_gen;
	mutable std::atomic<unsigned int> m_state;
	const workq_ptr m_wq;

protected:
	ILIAS_ASYNC_EXPORT virtual run_lck lock_run() noexcept;
	ILIAS_ASYNC_EXPORT virtual void unlock_run(run_lck rl) noexcept;

	ILIAS_ASYNC_EXPORT workq_job(workq_ptr, unsigned int = 0) throw (std::invalid_argument);
	ILIAS_ASYNC_EXPORT virtual ~workq_job() noexcept;
	ILIAS_ASYNC_EXPORT virtual void run() noexcept = 0;

public:
	ILIAS_ASYNC_EXPORT void activate(unsigned int flags = 0) noexcept;
	ILIAS_ASYNC_EXPORT void deactivate() noexcept;
	ILIAS_ASYNC_EXPORT const workq_ptr& get_workq() const noexcept;
	ILIAS_ASYNC_EXPORT const workq_service_ptr& get_workq_service() const noexcept;

	/* Test if the running bit is set. */
	bool
	is_running() const noexcept
	{
		return (this->m_state.load(std::memory_order_relaxed) & STATE_RUNNING);
	}


	workq_job(const workq_job&) = delete;
	workq_job& operator=(const workq_job&) = delete;
};


class workq final :
	public workq_detail::workq_int,
	public ll_base_hook<workq_detail::runq_tag>,
	public refcount_base<workq, workq_detail::wq_deleter>
{
friend class workq_service;
friend class workq_detail::wq_run_lock;
friend struct workq_detail::workq_intref_mgr<workq>;
friend void workq_job::activate(unsigned int) noexcept;
friend void workq_job::unlock_run(workq_job::run_lck rl) noexcept;
friend void workq_detail::wq_deleter::operator()(const workq*) const noexcept;
friend void workq_detail::wq_deleter::operator()(const workq_job*) const noexcept;

public:
	enum run_lck {
		RUN_SINGLE,
		RUN_PARALLEL
	};

private:
	typedef ll_smartptr_list<workq_detail::workq_intref<workq_job>,
	    ll_base<workq_job, workq_detail::runq_tag>,
	    refpointer_acquire<workq_job, workq_detail::workq_intref_mgr<workq_job> >,
	    refpointer_release<workq_job, workq_detail::workq_intref_mgr<workq_job> > > job_runq;

	typedef ll_smartptr_list<workq_detail::workq_intref<workq_job>,
	    ll_base<workq_job, workq_detail::parallel_tag>,
	    refpointer_acquire<workq_job, workq_detail::workq_intref_mgr<workq_job> >,
	    refpointer_release<workq_job, workq_detail::workq_intref_mgr<workq_job> > > job_p_runq;

	job_runq m_runq;
	job_p_runq m_p_runq;
	const workq_service_ptr m_wqs;
	std::atomic<bool> m_run_single;
	std::atomic<unsigned int> m_run_parallel;

	ILIAS_ASYNC_LOCAL run_lck lock_run() noexcept;
	ILIAS_ASYNC_LOCAL run_lck lock_run_parallel() noexcept;
	ILIAS_ASYNC_LOCAL void unlock_run(run_lck rl) noexcept;
	ILIAS_ASYNC_LOCAL run_lck lock_run_downgrade(run_lck rl) noexcept;

	ILIAS_ASYNC_LOCAL workq(workq_service_ptr wqs) throw (std::invalid_argument);
	ILIAS_ASYNC_LOCAL ~workq() noexcept;

public:
	ILIAS_ASYNC_EXPORT const workq_service_ptr& get_workq_service() const noexcept;
	ILIAS_ASYNC_EXPORT static workq_ptr get_current() noexcept;

private:
	ILIAS_ASYNC_LOCAL void job_to_runq(workq_detail::workq_intref<workq_job>) noexcept;

public:
	ILIAS_ASYNC_EXPORT workq_job_ptr new_job(unsigned int type, std::function<void()>)
	    throw (std::bad_alloc, std::invalid_argument);
	ILIAS_ASYNC_EXPORT workq_job_ptr new_job(unsigned int type, std::vector<std::function<void()> >)
	    throw (std::bad_alloc, std::invalid_argument);
	ILIAS_ASYNC_EXPORT void once(std::function<void()>)
	    throw (std::bad_alloc, std::invalid_argument);
	ILIAS_ASYNC_EXPORT void once(std::vector<std::function<void()> >)
	    throw (std::bad_alloc, std::invalid_argument);

	workq_job_ptr
	new_job(std::function<void()> fn) throw (std::bad_alloc, std::invalid_argument)
	{
		return this->new_job(0U, std::move(fn));
	}

	workq_job_ptr
	new_job(std::vector<std::function<void()> > fns) throw (std::bad_alloc, std::invalid_argument)
	{
		return this->new_job(0U, std::move(fns));
	}

	template<typename... FN>
	workq_job_ptr
	new_job(unsigned int type, std::function<void()> fn0, std::function<void()> fn1, FN&&... fn)
	    throw (std::bad_alloc, std::invalid_argument)
	{
		std::vector<std::function<void()> > fns;
		fns.push_back(std::move(fn0), std::move(fn1), std::forward<FN>(fn)...);

		return this->new_job(type, std::move(fns));
	}

	template<typename... FN>
	workq_job_ptr
	new_job(std::function<void()> fn0, std::function<void()> fn1, FN&&... fn)
	    throw (std::bad_alloc, std::invalid_argument)
	{
		return this->new_job(0U, std::move(fn0), std::move(fn1), std::forward<FN>(fn)...);
	}

	template<typename... FN>
	void
	once(std::function<void()> fn0, std::function<void()> fn1, FN&&... fn)
	    throw (std::bad_alloc, std::invalid_argument)
	{
		std::vector<std::function<void()> > fns;
		fns.push_back(std::move(fn0), std::move(fn1), std::forward<FN>(fn)...);

		this->once(std::move(fns));
	}

	ILIAS_ASYNC_EXPORT bool aid(unsigned int = 1) noexcept;


	workq(const workq&) = delete;
	workq& operator=(const workq&) = delete;
};


namespace workq_detail {


class co_runnable;

/*
 * wq_run_lock: lock a workq and job for execution.
 *
 * This is an internal type that should only be passed around by reference.
 *
 * This forms the basis for locking a job to be executed.
 * If this is in the locked state, the job _must_ be run,
 * after which commit() is called.
 *
 * Destroying a locked, but uncommited wq_run_lock will
 * result in an assertion failure, since it would invalidate
 * the promise to a job (when the job is succesfully locked,
 * it is guaranteed to run and to unlock).
 */
class ILIAS_ASYNC_LOCAL wq_run_lock
{
friend class ilias::workq_service;
friend class co_runnable;	/* Can't get more specific, since the co_runnable requires wq_run_lock to be defined. */
friend void ilias::workq_job::activate(unsigned int) noexcept;
friend bool ilias::workq::aid(unsigned int) noexcept;
friend ILIAS_ASYNC_EXPORT workq_pop_state ilias::workq_switch(const workq_pop_state&) throw (workq_deadlock, workq_stack_error);

private:
	workq_intref<workq> m_wq;
	workq_intref<workq_job> m_wq_job;
	workq_intref<workq_detail::co_runnable> m_co;
	workq::run_lck m_wq_lck;
	workq_job::run_lck m_wq_job_lck;
	bool m_commited;

public:
	wq_run_lock() noexcept :
		m_wq(),
		m_wq_job(),
		m_co(),
		m_wq_lck(),
		m_wq_job_lck(),
		m_commited(false)
	{
		/* Empty body. */
	}

	~wq_run_lock() noexcept
	{
		this->unlock();
	}

	wq_run_lock(wq_run_lock&& o) noexcept :
		m_wq(std::move(o.m_wq)),
		m_wq_job(std::move(o.m_wq_job)),
		m_co(std::move(o.m_co)),
		m_wq_lck(o.m_wq_lck),
		m_wq_job_lck(o.m_wq_job_lck),
		m_commited(o.m_commited)
	{
		/* Empty body. */
	}

private:
	wq_run_lock(workq_service& wqs) noexcept :
		wq_run_lock()
	{
		this->lock(wqs);
	}

	wq_run_lock(workq& wq) noexcept :
		wq_run_lock()
	{
		this->lock(wq);
	}

	wq_run_lock(workq_job& wqj) noexcept :
		wq_run_lock()
	{
		this->lock(wqj);
	}

	wq_run_lock(workq_detail::co_runnable& co) noexcept;

public:
	wq_run_lock&
	operator=(wq_run_lock&& o) noexcept
	{
		assert(!this->m_wq && !this->m_wq_job);
		this->m_wq = std::move(o.m_wq);
		this->m_wq_job = std::move(o.m_wq_job);
		this->m_co = std::move(o.m_co);
		this->m_wq_lck = o.m_wq_lck;
		this->m_wq_job_lck = o.m_wq_job_lck;
		this->m_commited = o.m_commited;
		return *this;
	}

	const workq_intref<workq>&
	get_wq() const noexcept
	{
		return this->m_wq;
	}

	const workq_intref<workq_job>&
	get_wq_job() const noexcept
	{
		return this->m_wq_job;
	}

	const workq_intref<workq_detail::co_runnable>&
	get_co() const noexcept
	{
		return this->m_co;
	}

	bool
	wq_is_single() const noexcept
	{
		return (this->m_wq && this->m_wq_lck == workq::RUN_SINGLE);
	}

private:
	void
	commit() noexcept
	{
		assert(this->is_locked() && !this->is_commited());
		this->m_commited = true;
	}

	void unlock() noexcept;
	void unlock_wq() noexcept;
	bool co_unlock() noexcept;
	bool lock(workq& what) noexcept;
	bool lock(workq_job& what) noexcept;
	bool lock(workq_service& wqs) noexcept;
	void lock_wq(workq& what, workq::run_lck how) noexcept;

	void
	wq_downgrade() noexcept
	{
		assert(this->get_wq() && this->m_wq_lck == workq::RUN_SINGLE);

		this->m_wq->lock_run_downgrade(this->m_wq_lck);
		this->m_wq_lck = workq::RUN_PARALLEL;
	}

public:
	bool
	is_commited() const noexcept
	{
		return this->m_commited;
	}

	bool
	is_locked() const noexcept
	{
		return (this->m_wq_job_lck != workq_job::BUSY && this->get_wq_job());
	}


	wq_run_lock(const wq_run_lock&) = delete;
	wq_run_lock& operator=(const wq_run_lock&) = delete;
};


class co_runnable :
	public ll_base_hook<coroutine_tag>,
	public workq_job
{
friend class ilias::workq_service;
friend class wq_run_lock;

private:
	wq_run_lock m_rlck;
	std::atomic<std::size_t> m_runcount;

public:
	ILIAS_ASYNC_EXPORT virtual ~co_runnable() noexcept;

protected:
	ILIAS_ASYNC_EXPORT co_runnable(workq_ptr, unsigned int = 0) throw (std::invalid_argument);

	ILIAS_ASYNC_EXPORT virtual void unlock_run(run_lck rl) noexcept override;

	ILIAS_ASYNC_EXPORT void co_publish(std::size_t) noexcept;
	ILIAS_ASYNC_EXPORT bool release(std::size_t) noexcept;

	ILIAS_ASYNC_EXPORT virtual bool co_run() noexcept = 0;
};


} /* namespace ilias::workq_detail */


class workq_service final :
	public workq_detail::workq_int,
	public refcount_base<workq_service, workq_detail::wq_deleter>
{
friend class workq_detail::wq_run_lock;
friend ILIAS_ASYNC_EXPORT workq_service_ptr new_workq_service() throw (std::bad_alloc);
friend ILIAS_ASYNC_EXPORT workq_service_ptr new_workq_service(unsigned int) throw (std::bad_alloc);
friend void workq_detail::wq_deleter::operator()(const workq*) const noexcept;
friend void workq_detail::wq_deleter::operator()(const workq_service*) const noexcept;
friend void workq_detail::co_runnable::co_publish(std::size_t) noexcept;
friend bool workq_detail::co_runnable::release(std::size_t n) noexcept;
friend void workq::job_to_runq(workq_detail::workq_intref<workq_job>) noexcept;

private:
	typedef ll_smartptr_list<workq_detail::workq_intref<workq>,
	    ll_base<workq, workq_detail::runq_tag>,
	    refpointer_acquire<workq, workq_detail::workq_intref_mgr<workq> >,
	    refpointer_release<workq, workq_detail::workq_intref_mgr<workq> > > wq_runq;

	typedef ll_smartptr_list<workq_detail::workq_intref<workq_detail::co_runnable>,
	    ll_base<workq_detail::co_runnable, workq_detail::coroutine_tag>,
	    refpointer_acquire<workq_detail::co_runnable, workq_detail::workq_intref_mgr<workq_detail::co_runnable> >,
	    refpointer_release<workq_detail::co_runnable, workq_detail::workq_intref_mgr<workq_detail::co_runnable> > > co_runq;

	wq_runq m_wq_runq;
	co_runq m_co_runq;
	threadpool m_workers;	/* Must be the last member variable in this class. */

	ILIAS_ASYNC_LOCAL workq_service();
	ILIAS_ASYNC_LOCAL explicit workq_service(unsigned int threads);
	ILIAS_ASYNC_LOCAL ~workq_service() noexcept;

	ILIAS_ASYNC_LOCAL void wq_to_runq(workq_detail::workq_intref<workq>) noexcept;
	ILIAS_ASYNC_LOCAL void co_to_runq(workq_detail::workq_intref<workq_detail::co_runnable>, std::size_t) noexcept;
	ILIAS_ASYNC_LOCAL void wakeup(std::size_t = 1) noexcept;

	bool
	threadpool_pred() noexcept
	{
		return !this->m_wq_runq.empty() || !this->m_co_runq.empty();
	}

	ILIAS_ASYNC_LOCAL bool threadpool_work() noexcept;

public:
	ILIAS_ASYNC_EXPORT workq_ptr new_workq() throw (std::bad_alloc);
	ILIAS_ASYNC_EXPORT bool aid(unsigned int = 1) noexcept;


	workq_service(const workq_service&) = delete;
	workq_service& operator=(const workq_service&) = delete;
};

class workq_pop_state
{
private:
	workq_ptr m_wq;
	workq::run_lck m_lck;

public:
	workq_pop_state() noexcept :
		m_wq(),
		m_lck()
	{
		/* Empty body. */
	}

	workq_pop_state(const workq_pop_state& o) noexcept :
		m_wq(o.m_wq),
		m_lck(o.m_lck)
	{
		/* Empty body. */
	}

	workq_pop_state(workq_pop_state&& o) noexcept :
		m_wq(std::move(o.m_wq)),
		m_lck(std::move(o.m_lck))
	{
		/* Empty body. */
	}

	workq_pop_state(workq_ptr wq, workq::run_lck lck = workq::RUN_SINGLE) noexcept :
		m_wq(wq),
		m_lck(lck)
	{
		/* Empty body. */
	}

	workq_pop_state&
	operator=(workq_pop_state o) noexcept
	{
		this->swap(o);
		return *this;
	}

	void
	swap(workq_pop_state& o) noexcept
	{
		using std::swap;

		swap(this->m_wq, o.m_wq);
		swap(this->m_lck, o.m_lck);
	}

	friend void
	swap(workq_pop_state& a, workq_pop_state& b) noexcept
	{
		a.swap(b);
	}

	const workq_ptr&
	get_workq() const noexcept
	{
		return this->m_wq;
	}

	bool
	is_single() const noexcept
	{
		return (this->m_wq && this->m_lck == workq::RUN_SINGLE);
	}
};


} /* namespace ilias */


#ifdef _MSC_VER
#pragma warning( pop )
#endif

#endif /* ILIAS_WORKQ_H */
