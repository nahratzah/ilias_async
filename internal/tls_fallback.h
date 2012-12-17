#ifndef ILIAS_TLS_FALLBACK_H
#define ILIAS_TLS_FALLBACK_H

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>


namespace ilias {
namespace {

/*
 * Thread-local fallback.
 * It basically behaves like a smart pointer, with lazy initialization.
 *
 * This code is very hackish and not a good example of nice and clean design.
 */
template<typename Type>
class tls
{
public:
	typedef Type value_type;
	typedef value_type* pointer;
	typedef value_type& reference;
	typedef const value_type* const_pointer;

private:
	struct elem
	{
		tls* self;
		value_type data;

		elem(tls* self) :
			self(self),
			data()
		{
			assert(self != nullptr);
		}


#if HAS_DELETED_FN
		elem() = delete;
		elem(const elem&) = delete;
		elem& operator=(const elem&) = delete;
#else
		elem();
		elem(const elem&);
		elem& operator=(const elem&);
#endif
	};

	struct ip_delete
	{
		void
		operator()(elem* p) const ILIAS_NET2_NOTHROW
		{
			p->~elem();
			std::free(p);
		}
	};

	typedef std::unique_ptr<elem, ip_delete> internal_pointer;

	pthread_key_t key;
	std::mutex mtx;
	std::vector<internal_pointer> instances;

	static void
	destructor(void* p_) ILIAS_NET2_NOTHROW
	{
		elem* p = static_cast<elem*>(p_);

		tls* self = p->self;
		std::lock_guard<std::mutex> lck(self->mtx);
		auto f = std::find_if(self->instances.begin(), self->instances.end(), [p](const internal_pointer& ip) -> bool {
			return ip.get() == p;
		    });
		assert(f != self->instances.end());
		self->instances.erase(f);
	}

public:
	tls()
	{
		int rv = ::pthread_key_create(&key, &tls::destructor);
		if (rv) {
			/*
			 * Well, if too many keys are used, EAGAIN is returned,
			 * which is technically not a memory allocation failure,
			 * but some other kind of resource.
			 */
			throw std::bad_alloc();
		}
	}

	~tls() ILIAS_NET2_NOTHROW
	{
		::pthread_key_delete(this->key);
	}

	/*
	 * Yes, this is ugly.
	 * I'm not going to model it nicely, it's a hackish solution that will
	 * be phased out once bitrig has tls anyway.
	 */
	pointer
	get()
	{
		void* p = ::pthread_getspecific(this->key);
		if (p)
			return &static_cast<elem*>(p)->data;

		internal_pointer ip;
		p = std::malloc(sizeof(elem));
		if (!p)
			throw std::bad_alloc();
		memset(p, 0, sizeof(elem));
		try {
			ip.reset(new (p) elem(this));
		} catch (...) {
			std::free(p);
			throw;
		}

		std::lock_guard<std::mutex> lck(this->mtx);

		int rv = ::pthread_setspecific(key, p);
		if (rv)
			throw std::bad_alloc();

		try {
			this->instances.push_back(std::move(ip));
		} catch (...) {
			::pthread_setspecific(key, nullptr);
			throw;
		}

		/* Note that if everything went well, ip currently holds null. */
		return &static_cast<elem*>(p)->data;
	}

	pointer
	operator->()
	{
		return this->get();
	}

	reference
	operator*()
	{
		return *this->get();
	}


#if HAS_DELETED_FN
	tls(const tls&) = delete;
	tls& operator=(const tls&) = delete;
#else
private:
	tls(const tls&);
	tls& operator=(const tls&);
#endif
};


}} /* namespace ilias::<unnamed> */

#endif /* ILIAS_TLS_FALLBACK_H */
