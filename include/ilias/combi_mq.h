namespace ilias {
namespace combiner_detail {


/*
 * Optional value, used by combiner.
 *
 * It is not a pointer, but does implement the behaviour of one.
 */
template<typename Type>
class combiner_opt
{
public:
	using element_type = Type;
	using pointer = element_type*;
	using const pointer = const element_type*;
	using reference = element_type&;
	using const reference = const element_type&;

private:
	union impl
	{
		element_type v;

		~impl() noexcept {}
	};

	bool m_isset{ false };
	impl m_impl{};

	static constexpr bool noexcept_copy =
	    std::is_nothrow_copy_constructible<element_type>::value;
	static constexpr bool noexcept_move =
	    std::is_nothrow_move_constructible<element_type>::value;
	static constexpr bool noexcept_destroy =
	    std::is_nothrow_destructible<element_type>::value;
	static constexpr bool moveable =
	    std::is_move_constructible<element_type>::value;
	static constexpr bool noexcept_swap =
	    (noexcept_move || noexcept_copy) && noexcept_destroy;

public:
	explicit operator bool() const noexcept
	{
		return this->m_isset;
	}

	pointer
	get() noexcept
	{
		return (*this ? &this->m_impl.v : nullptr);
	}

	const_pointer
	get() const noexcept
	{
		return (*this ? &this->m_impl.v : nullptr);
	}

	pointer
	operator-> () noexcept
	{
		return this->get();
	}

	const_pointer
	operator-> () const noexcept
	{
		return this->get();
	}

	reference
	operator* () noexcept
	{
		return *this->get();
	}

	const_reference
	operator* () const noexcept
	{
		return *this->get();
	}

	void
	reset() noexcept(noexcept_destroy)
	{
		if (this->m_isset) {
			this->m_isset = false;
			this->m_impl.v.~element_type();
		}
	}

	friend void
	swap(combiner_opt& p, combiner_opt& q) noexcept(noexcept_swap)
	{
		if (p && q) {
			swap(p.m_impl.v, q.m_impl.v);
			return;
		}

		if (p) {
			new (&q.m_impl.v) element_type(
			    std::move_if_noexcept(p.m_impl.v));
			q.m_isset = true;
			p.reset();
			return;
		}

		if (q) {
			new (&p.m_impl.v) element_type(
			    std::move_if_noexcept(q.m_impl.v));
			p.m_isset = true;
			q.reset();
			return;
		}
	}

	combiner_opt() = default;

	/* Destructor. */
	~combiner_opt()
	    noexcept(std::is_nothrow_destructible<element_type>::value)
	{
		this->reset();
	}

	/* Copy constructor. */
	combiner_opt(const combiner_opt& o) noexcept(noexcept_copy)
	{
		if (o.m_isset) {
			new (&this->m_impl.v) element_type(o.m_impl.v);
			this->m_isset = true;
		}
	}

	/*
	 * Implement move constructor
	 * iff element_type is move constructible.
	 */
	combiner_opt(const std::enable_if<moveable>::value,
	    combiner_opt&>::type o) noexcept(noexcept_move && noexcept_destroy)
	{
		if (o.m_isset) {
			new (&this->m_impl.v) element_type(
			    std::move(o.m_impl.v));
			this->m_isset = true;

			o.reset();
		}
	}

	combiner_opt&
	operator= (combiner_opt o) noexcept(noexcept_swap)
	{
		swap(*this, o);
		return *this;
	}

	bool
	operator== (const combiner_opt& o) const
	    noexcept(noexcept(m_impl.v == o.m_impl.v))
	{
		return (!*this || !o ?
		    *this == o :
		    this->impl.v == o.m_impl.v);
	}

	bool
	operator!= (const combiner_opt& o) const
	    noexcept(noexcept(m_impl.v != o.m_impl.v))
	{
		return (!*this || !o ?
		    *this != o :
		    this->impl.v != o.m_impl.v);
	}
};


} /* namespace ilias::combiner_detail */


template<typename... MQ>
class combiner
:	public msg_queue_events
{
public:
	using element_type = std::tuple<typename MQ::element_type...>;

private:
	std::mutex m_mtx;
	std::bitset<sizeof...(MQ)> m_ready;

	std::tuple<combiner_detail::combiner_opt<typename MQ::element_type>...>
	    m_val;
	std::tuple<MQ&...> m_mq;

public:
	template<typename AllocArgs>
	combiner(MQ&... mq, AllocArgs&&... alloc_args) noexcept
	:	m_mq(mq),
		m_drain(std::forward<AllocArgs>(alloc_args)...)
	{
		/* Empty body. */
	}

	bool
	empty() const noexcept
	{
		return !this->m_ready.all();
	}

	template<typename... Args>
	auto
	dequeue(Args&&... args) noexcept ->
	    decltype(m_drain.dequeue(std::forward<Args>(args)...))
	{
		return this->m_drain.dequeue(std::forward<Args>(args)...);
	}

	drain_type&
	impl() noexcept
	{
		return this->m_drain;
	}

	const drain_type&
	impl() const noexcept
	{
		return this->m_drain;
	}

	template<typename... Args>
	friend void
	output_callback(combiner& self, Args&&... args)
	{
		output_callback(self.impl(), std::forward<Args>(args)...);
	}

	template<typename... Args>
	friend void
	empty_callback(combiner& self, Args&&... args)
	{
		empty_callback(self.impl(), std::forward<Args>(args)...);
	}
};


} /* namespace ilias */
