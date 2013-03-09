Promises and futures
====================

C++11 introduces [promises and futures](http://www.stroustrup.com/C++11FAQ.html#std-future "std::future and std::promise") to handle deferred and asynchronous resolution.  The promises and futures in this library are an adaptation, which attach events to promises/futures.

A ```promise<T>``` is considered live (can be completed) if at least one thread holds an instance of this promise or if an event is installed, that can complete the promise.  A promise is broken once it is no longer live.  A ```promise<T>``` can be implicitly converted to a ```future<T>```, where the promise is used to publish a value and the future is used to read this value.  Note that a promise becomes immutable once it has been assigned a value.


Promise
-------

'''class promise<T>''' declares the intent to yield a value of type '''T'''.  A promise is immutable once assigned.  Instead of assigning a value you can assign exception.

	template<typename T>
	class promise
	{
	public:
		using value_type = T;

		/* Reference types are defined if T is non-void. */
		using reference = value_type&;
		using const_reference = const value_type&;
	};


A default constructed promise is uninitialized (thus allowing copying/moving existing promises into existing variables).  When an initialized promise is copied, both promises refer to the same shared state.  A new, initialized promise can be constructed by calling '''promise<Type>::create()'''.

	template<typename T>
	class promise
	{
		/* Uninitialized promise. */
		promise();

		/* Initialized promise. */
		static promise<T> create() throw (std::bad_alloc);
	};


You can assign a value by calling the '''promise<T>::set()''' method, which performs in-place construction of the assigned value, using the provided arguments.  The set method will return if the assignment was succesful: if the assignment failed, the promise already holds a result.  If the constructor fails, the promise will be assigned the exception of the constructor instead *I may want to change that...*.

	template<typename T>
	class promise
	{
		template<typename... Args>
		bool
		set(Args&&... args)
		throw (uninitialized_promise,	// if the promise is used uninitialized
		    ... /* Any exception thrown by constructor of T. */ );
	};

Instead of assigning a value, you can also assign an exception, using the '''promise<T>::set_exception''' method.  Like '''promise<T>::set''', the '''promise<T>::set_exception''' method will return true only if the promise was not assigned to.

	template<typename T>
	class promise
	{
		bool
		set_exception(std::exception_ptr exception)
		throw (uninitialized_promise);	// if the promise is used uninitialized
	};


TODO
----
- [ ] Describe promise callback
- [ ] Describe future callbacks
- [ ] Describe promise start commands
- [ ] Describe is_initialized, has_value, has_exception, ready methods on both promise and future
