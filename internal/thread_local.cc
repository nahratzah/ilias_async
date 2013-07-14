#include "thread_local.h"


namespace ilias {
namespace tls_cd_detail {


key_data::key_data(void (*destructor)(void*))
{
	int rv = ::pthread_key_create(&key, destructor);
	if (rv) {
		/*
		 * If too many keys are used, EAGAIN is returned,
		 * which technically is not a memory allocation
		 * failure, but we're going to claim it is
		 * anyway.
		 */
		throw std::bad_alloc{};
	}
}

key_data::~key_data()
{
	::pthread_key_delete(this->key);
}


}} /* namespace ilias::tls_cd_detail */
