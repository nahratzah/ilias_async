#ifndef ILIAS_DETAIL_LL_SIMPLE_LIST_H
#define ILIAS_DETAIL_LL_SIMPLE_LIST_H

#include <ilias/ilias_async_export.h>
#include <ilias/llptr.h>
#include <ilias/refcnt.h>
#include <tuple>

namespace ilias {
namespace ll_list_detail {


using refcount_t = unsigned int;
using flags_t = std::bitset<1>;
constexpr flags_t PRESENT{ 0UL };
constexpr flags_t DELETED{ 1UL };

enum class link_result : unsigned char {
	SUCCESS,
	INS0_LINKED,
	INS1_LINKED,
	INVALID_POS
};

class ll_simple_list
{
public:
	class elem;
	class elem_range;

	struct elem_refcnt_mgr {
		static inline void acquire(const elem&, refcount_t);
		static inline void release(const elem&, refcount_t);
	};

	using elem_ptr = refpointer<elem, elem_refcnt_mgr>;
	using elem_llptr = llptr<elem, elem_refcnt_mgr, 1>;

protected:

private:
	elem m_head_;
};

using data_t = ll_simple_list::elem_llptr::element_type;

class ll_simple_list::elem
{
friend struct ll_simple_list::elem_refcnt_mgr;
friend class ll_simple_list::elem_range;

public:
	inline elem() noexcept;
	inline elem(const elem&) noexcept;
	inline elem(elem&&) noexcept;
	inline ~elem() noexcept;
	inline bool is_linked();
	inline bool is_unused() const noexcept;
	inline void wait_unused() const noexcept;

	/* Predecessor/successor lookup. */
	ILIAS_ASYNC_EXPORT data_t succ() const noexcept;
	ILIAS_ASYNC_EXPORT data_t pred() const noexcept;

	/* Unlink operation. */
	ILIAS_ASYNC_EXPORT bool unlink() noexcept;

	/* Multi element insertion. */
	inline static link_result link_between(elem_range&&,
	    std::tuple<elem_ptr, elem_ptr>);
	inline static link_result link_before(elem_range&&, elem_ptr);
	inline static link_result link_after(elem_range&&, elem_ptr);

	/* Single element insertion. */
	inline static link_result link_between(
	    elem*, std::tuple<elem_ptr, elem_ptr>);
	inline static link_result link_before(
	    elem*, elem_ptr);
	inline static link_result link_after(
	    elem*, elem_ptr);

private:
	std::atomic<refcount_t> m_refcnt_;
	elem_llptr m_pred_, m_succ_;

	/* Multi element insertion (used via elem_range insertion). */
	inline static link_result link_between(
	    std::tuple<elem*, elem*>, std::tuple<elem_ptr, elem_ptr>);
	inline static link_result link_before(
	    std::tuple<elem*, elem*>, elem_ptr);
	inline static link_result link_after(
	    std::tuple<elem*, elem*>, elem_ptr);

	/* Internal implementation of link operations. */
	ILIAS_ASYNC_EXPORT static link_result link_between_(
	    std::tuple<elem*, elem*>, std::tuple<elem_ptr, elem_ptr>) noexcept;
	ILIAS_ASYNC_EXPORT static link_result link_before_(
	    std::tuple<elem*, elem*>, elem_ptr);
	ILIAS_ASYNC_EXPORT static link_result link_after_(
	    std::tuple<elem*, elem*>, elem_ptr);
};

class ll_simple_list::elem_range
{
friend class ll_simple_list::elem;

public:
	elem_range() = default;
	elem_range(const elem_range&) = delete;
	inline elem_range(elem_range&&) noexcept;
	template<typename Iter> inline elem_range(Iter b, Iter e);
	inline ~elem_range() noexcept;

	inline void push_front(elem*);
	inline void push_back(elem*);
	inline bool empty() const noexcept;

private:
	elem m_self_;

	class release_
	{
	public:
		inline release_(elem_range&) noexcept;
		release_(const release_&) = delete;
		release_(release_&&) = delete;
		release_& operator=(const release_&) = delete;
		inline ~release_() noexcept;

		inline std::tuple<elem*, elem*> get() const noexcept;
		inline void commit() noexcept;

	private:
		elem_range& self_;
		bool commited_;
		std::tuple<elem_ptr, elem_ptr> data_;
	};
};


}} /* namespace ilias::ll_list_detail */


#include <ilias/detail/ll_simple_list-inl.h>

#endif /* ILIAS_DETAIL_LL_SIMPLE_LIST_H */
