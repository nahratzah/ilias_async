#ifndef VAL_ITER_H
#define VAL_ITER_H

#include <ilias/msg_queue.h>
#include <iterator>

/*
 * Create a small iterator type that iterates over a range of values.
 */
template<typename Type>
class val_iter
:	std::iterator<std::bidirectional_iterator_tag, const Type>
{
public:
	using typename std::iterator<std::bidirectional_iterator_tag,
	    const Type>::pointer;
	using typename std::iterator<std::bidirectional_iterator_tag,
	    const Type>::reference;

private:
	Type m_val;

public:
	constexpr val_iter(Type v = Type(0))
	:	m_val(v)
	{
		/* Empty body. */
	}

	bool
	operator==(const val_iter& o) const noexcept
	{
		return this->m_val == o.m_val;
	}

	bool
	operator!=(const val_iter& o) const noexcept
	{
		return this->m_val != o.m_val;
	}

	bool
	operator<(const val_iter& o) const noexcept
	{
		return this->m_val < o.m_val;
	}

	bool
	operator>(const val_iter& o) const noexcept
	{
		return this->m_val > o.m_val;
	}

	bool
	operator<=(const val_iter& o) const noexcept
	{
		return this->m_val <= o.m_val;
	}

	bool
	operator>=(const val_iter& o) const noexcept
	{
		return this->m_val >= o.m_val;
	}

	pointer
	operator->() const noexcept
	{
		return &m_val;
	}

	reference
	operator*() const noexcept
	{
		return m_val;
	}

	val_iter&
	operator++(int) noexcept
	{
		++m_val;
		return *this;
	}

	val_iter&
	operator--(int) noexcept
	{
		--m_val;
		return *this;
	}

	val_iter
	operator++() noexcept
	{
		val_iter clone = *this;
		++*this;
		return clone;
	}

	val_iter
	operator--() noexcept
	{
		val_iter clone = *this;
		--*this;
		return clone;
	}
};

#endif /* VAL_ITER_H */
