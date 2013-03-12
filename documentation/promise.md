Promises and futures
====================

C++11 introduces [promises and futures](http://www.stroustrup.com/C++11FAQ.html#std-future "std::future and std::promise") to handle deferred and asynchronous resolution.  The promises and futures in this library are an adaptation of the (pretty basic) promises in the standard.  These promises have events assigned which can be used to asynchronously resolve a promise.

A ```promise<T>``` is considered live (can be completed) if at least one thread holds an instance of this promise or if an event is installed, that can complete the promise.  A promise is broken once it is no longer live.  A ```promise<T>``` can be implicitly converted to a ```future<T>```, where the promise is used to publish a value and the future is used to read this value.  Note that a promise becomes immutable once it has been assigned a value.


Promise
-------

```class promise<T>``` declares the intent to yield a value of type ```T```.  A promise is immutable once assigned.  Instead of assigning a value you can assign exception.

	template<typename T>
	class promise
	{
	public:
		using value_type = T;

		/* Reference types are defined if T is non-void. */
		using reference = value_type&;
		using const_reference = const value_type&;
	};


A default constructed promise is uninitialized (thus allowing copying/moving existing promises into existing variables).  When an initialized promise is copied, both promises refer to the same shared state.  A new, initialized promise can be constructed by calling ```promise<Type>::create()``` or preferably by calling ```new_promise<Type>(...)```.

	template<typename T>
	class promise
	{
		/* Uninitialized promise. */
		promise();

		/* Initialized promise. */
		static promise<T> create() throw (std::bad_alloc);
	};


You can assign a value by calling the ```promise<T>::set()``` method, which performs in-place construction of the assigned value, using the provided arguments.  The set method will return if the assignment was succesful: if the assignment failed, the promise already holds a result.  If the constructor fails, the promise will be assigned the exception of the constructor instead *I may want to change that...*.

	template<typename T>
	class promise
	{
		template<typename... Args>
		bool
		set(Args&&... args)
		throw (uninitialized_promise,	// if the promise is used uninitialized
		    ... /* Any exception thrown by constructor of T. */ );
	};

Instead of assigning a value, you can also assign an exception, using the ```promise<T>::set_exception``` method.  Like ```promise<T>::set```, the ```promise<T>::set_exception``` method will return true only if the promise was not assigned to.

	template<typename T>
	class promise
	{
		bool
		set_exception(std::exception_ptr exception)
		throw (uninitialized_promise);	// if the promise is used uninitialized
	};


Futures
-------

A future represents the (asynchronous) outcome of a promise.  When a promise is assigned a value to, all futures bound to it will be able to access the assigned value.

The default constructor of future creates an uninitialized future (i.e. one not bound to (the shared state of) a promise).  Futures can be derived from promises either by invoking their constructor with the promise.

	promise<int> p = new_promise<int>();  // Create an initialized promise.
	future<int> f1;  // An uninitialized future.
	future<int> f2 = p;  // Promise bound to future p.
	f1 = p;  // Assign future corresponding to promise p.


A future can be used to read the value that the promise assigned.  ```future<T>::get()``` will return the value.  If the promise assigned an exception, the exception will be thrown instead.  The ```future<T>::get()``` method will block until the promise is ready.

	promise<int> p = new_promise<int>();
	future<int> f = p;
	p.set(42);
	f.get();  // Returns 42.

	promise<int> p = new_promise<int>();
	future<int> f = p;
	p.set_exception(std::make_exception_ptr(std::exception()));
	f.get();  // Throws std::exception.

	promise<int> p = new_promise<int>();
	future<int> f = p;
	f.get();  // Blocks until p is assigned to (which in this example takes forever).

	future<int> f = new_promise<int>();
	f.get();  // Throws ilias::broken_promise, since no assignment to the original promise is possible.

	future<int> f;
	f.get();  // Throws ilias::uninitialized_promise, since no promise is associated with the future.


Instead of calling ```future<T>::get()``` to wait for a promise to complete, the ```future<T>::wait()``` method can be used.  The ```future<T>::wait()``` method will not throw an exception and will not return the assigned value.  It will simply block execution until the promise completes, at which point it will return.


The ```future<T>::is_broken_promise()``` method can be used to test if a future refers to a broken promise.  A promise is considered broken when the last copy of the promise is destroyed while the promise was never assigned a value.  A broken promise usually indicates a bug in a program and is intended to be avoided.

	class future<T> {
		bool is_broken_promise() const noexcept;
	};


Testing the state of a promise or future
-----------------------------------------

```promise<T>``` and ```future<T>``` share a number of methods that can be used to test the state of the promise.

	class future<T> or class promise<T> {
		bool is_initialized() const noexcept;
		bool has_value() const noexcept;
		bool has_exception() const noexcept;
		bool ready() const noexcept;
	};

The ```is_initialized()``` method tests if the promise or future is initialized (i.e. if it has a shared state).  A promise that is not initialized, can be initialized by calling the static method ```new_promise<T>()``` to create an initialized promise.

The ```has_value()``` method tests if the promise has a value assigned.  If the promise has been assigned to succesfully (```promise<T>::set(...)``` completed sucesfully) the promise will hold a value and this method will return true.  Otherwise, false is returned.  If this method returns true, a call to ```future<T>::get()``` will not block and will yield the assigned value.

The ```has_exception()``` method tests if the promise has an exception assigned (via ```promise<T>::set_exception()```).  If the promise has been assigned an exception (either via ```promise<T>::set_exception()``` or by the throwing an exception during the ```promise<T>::set(...)``` invocation) this method will return true.  If this method returns true, a call to ```future<T>::get()``` will not block and will yield the assigned exception.  If the promise is broken, ```has_exception()``` will yield false (the promise was not assigned an exception, it was simply forgotten, hence returning false).

The ```ready()``` method tests if the promise has completed.  Once a promise has completed, it becomes immutable and can no longer be assigned to.  When ```ready()``` returns true, a call to ```future<T>::get()``` will not block.

Note that ```is_initialized()```, ```has_value()```, ```has_exception()``` and ```ready()``` will return false when used on an uninitialized promise or future.



TODO
----
- [ ] Describe promise callback
- [ ] Describe future callbacks
- [ ] Describe promise start commands
- [ ] Describe is_initialized, has_value, has_exception, ready methods on both promise and future
