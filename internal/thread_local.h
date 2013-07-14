#ifndef ILIAS_THREAD_LOCAL_H
#define ILIAS_THREAD_LOCAL_H

#include <ilias/ilias_async_export.h>
#include <type_traits>
#include <new>
#include <pthread.h>

namespace ilias {
namespace tls_cd_detail {


class key_data
{
protected:
	::pthread_key_t key;

public:
	ILIAS_ASYNC_LOCAL key_data(void (*)(void*));
	ILIAS_ASYNC_LOCAL ~key_data();

	key_data(const key_data&) = delete;
	key_data(key_data&&) = delete;
	key_data& operator=(const key_data&) = delete;
	key_data& operator=(key_data&&) = delete;
};


} /* namespace ilias::tls_cd_detail */


namespace {


/*
 * Thread local storage with constructor and destructor.
 * It's a fallback for c++ implementations without the thread_local keyword
 * implemented.
 *
 * Con: each instance uses a pthread_key instance.
 */
template<typename T>
class tls_cd
:	private tls_cd_detail::key_data
{
private:
	using storage_type = typename std::aligned_storage<sizeof(T),
	    std::alignment_of<T>::value>::type;

#if HAS_TLS
	struct data {
		storage_type store;
		bool inited;
	};

	T&
	get_tls() const noexcept
	{
		static THREAD_LOCAL data m_data;
		if (!m_data.inited) {
			new (&m_data.store) T{};

			int err = ::pthread_setspecific(this->key, &m_data);
			if (err)
				throw std::bad_alloc();

			m_data.inited = true;
		}

		return *static_cast<T*>(&m_data.store);
	}

	static void
	destructor(void* v) noexcept
	{
		data& d = *static_cast<data*>(v);
		if (d.inited)
			static_cast<T*>(&m_data.store)->~T();
	}
#else
	T&
	get_tls() const noexcept
	{
		T* rv;
		void* v = ::pthread_getspecific(this->key);

		if (v) {
			rv = static_cast<T*>(v);
		} else {
			rv = new T{};
			v = rv;
			int err = ::pthread_setspecific(this->key, v);
			if (err)
				throw std::bad_alloc();
		}

		assert(rv);
		return *rv;
	}

	static void
	destructor(void* v) noexcept
	{
		T* rv = static_cast<T*>(v);
		delete rv;
	}
#endif

public:
	tls_cd() noexcept
	:	tls_cd_detail::key_data{ &tls_cd::destructor }
	{
		/* Empty body. */
	}

	T*
	get() const noexcept
	{
		return &this->get_tls();
	}

	T*
	operator->() const noexcept
	{
		return this->get();
	}

	T&
	operator*() const noexcept
	{
		return *this->get();
	}
};


}} /* namespace ilias::<unnamed> */

#endif /* ILIAS_THREAD_LOCAL_H */
