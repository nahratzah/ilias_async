#include <ilias/refcnt.h>
#include <atomic>
#include <functional>
#include <thread>


namespace ilias {
namespace refpointer_detail {
namespace {


constexpr unsigned int SPIN = 100;

inline void
spinwait(unsigned int& spin) noexcept
{
	if (spin-- != 0) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
		/* MS-compiler x86/x86_64 assembly. */
		__asm {
			__asm pause
		};
#elif (defined(__GNUC__) || defined(__clang__)) &&			\
    (defined(__amd64__) || defined(__x86_64__) ||			\
     defined(__i386__) || defined(__ia64__))
		/* GCC/clang assembly. */
		__asm __volatile("pause":::"memory");
#else
		std::this_thread::yield();
#endif
	} else {
		spin = SPIN;
		std::this_thread::yield();
	}
}

} /* namespace ilias::refpointer_detail::<unnamed> */


struct atom_lck::impl
{
	std::atomic<unsigned int> m_ticket{ 0U };
	std::atomic<unsigned int> m_start{ 0U };

	void
	lock() noexcept
	{
		const auto hwcc = std::thread::hardware_concurrency();
		unsigned int spin = SPIN;
		const auto ticket =
		    this->m_ticket.fetch_add(1U, std::memory_order_acquire);
		auto start = this->m_start.load(std::memory_order_relaxed);

		/*
		 * Yield while more threads want the lock
		 * than cpus are available.
		 */
		while (ticket - start >= std::max(1U, hwcc)) {
			std::this_thread::yield();

			/* Reload start value. */
			start = this->m_start.load(std::memory_order_relaxed);
		}

		/*
		 * Spin-look wait to minimize time between release and acquire.
		 */
		while (ticket != start) {
			spinwait(spin);

			/* Reload start value. */
			start = this->m_start.load(std::memory_order_relaxed);
		}

		std::atomic_thread_fence(std::memory_order_acquire);
	}

	void
	unlock() noexcept
	{
		this->m_start.fetch_add(1U, std::memory_order_release);
	}
};


namespace {

constexpr unsigned int N_ATOMS = 16;
atom_lck::impl lck[N_ATOMS];

} /* namespace ilias::refpointer_detail::<unnamed> */


atom_lck::atom_lck(const void* addr) noexcept
:	m_impl(lck[std::hash<const void*>()(addr) % N_ATOMS])
{
	this->m_impl.lock();
}

atom_lck::~atom_lck() noexcept
{
	this->m_impl.unlock();
}


}} /* namespace ilias::refpointer_detail */
