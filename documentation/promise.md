Promises and futures
====================

C++11 introduces [promises and futures](http://www.stroustrup.com/C++11FAQ.html#std-future "std::future and std::promise") to handle deferred and asynchronous resolution.
The promises and futures in this library are an adaptation of the (pretty basic) promises in the standard.
These promises have events assigned which can be used to asynchronously resolve a promise.
The promises and futures are designed to mirror the ones from the STL as close as possible.

A ```cb_promise<T>``` is considered live (can be completed) if at least one thread holds an instance of this promise or if an event is installed, that can complete the promise.  A promise is broken once it is no longer live.
A ```cb_future<T>``` can be acquired from ```cb_promise<T>```, where the promise is used to publish a value and the corresponding future is used to read this value.


Promise
-------

```class cb_promise<T>``` declares the intent to yield a value of type ```T```.
Instead of assigning a value you can assign exception.

	template<typename T> class cb_promise;


A default constructed promise is initialized with a shared state (used to communicate the result).
The construction may throw ```std::bad_alloc```.
Promises are moveable, but not copyable.

	template<typename T>
	class cb_promise {
	 public:
	  /* Construction, creates a shared state, may throw std::bad_alloc. */
	  cb_promise();
	  template<typename Alloc> cb_promise(allocator_arg_t, const Alloc&);

	  /* Moveable. */
	  cb_promise(cb_promise&&) noexcept;
	  cb_promise& operator=(cb_promise&&) noexcept;

	  /* Not copyable. */
	  cb_promise(const cb_promise&) = delete;
	  cb_promise& operator=(const cb_promise&) = delete;
	};


You can assign a value by calling the ```cb_promise<T>::set_value()``` method.
Instead of assigning a value, you can assign an exception using ```cb_promise<T>::set_exception()```.
These method may throw:
- ```future_error``` with ```future_errc::no_state``` if the promise has no state,
- ```future_error``` with ```future_errc::promise_already_satisfied``` if the promise already has a result assigned.
- any exception thrown by the move/copy constructor of ```T```.
If a promise is destroyed prior to assigning a result, the associated future will never complete.

	template<typename T>
	class cb_promise {
	 public:
	  void set_value(T_Argument);
	  void set_exception(std::exception_ptr);
	};

```T_Argument``` is:
- ```const T&``` or ```T``` for non-reference T,
- ```T&``` for reference T,
- abent for ```cb_promise<void>```.


To get the associated ```cb_future<T>``` for a ```cb_promise<T>```, use the ```cb_promise<T>::get_future()``` method.

	template<typename T>
	class cb_promise {
	 public:
	  cb_future<T> get_future();
	}


Futures
-------

A future represents the (asynchronous) outcome of a promise.
When a promise is assigned a result, all futures bound to it will be able to access the assigned value (or exception).

The default constructor of future creates an uninitialized future (i.e. one not bound to (the shared state of) a promise).
Initialized futures are acquired via the ```cb_promise<T>::get_future()``` method on a promise.

	template<typename T> class cb_future;

	cb_promise<int> p;  // Create an initialized promise.
	cb_future<int> f1;  // An uninitialized future.
	cb_future<int> f2 = p.get_future();  // Promise bound to future p.


A future can be used to read the value that the promise assigned.
```cb_future<T>::get()``` will return the value.
If the promise assigned an exception, the exception will be thrown instead.
The ```cb_future<T>::get()``` method will block until the promise is ready.

	cb_promise<int> p;
	cb_future<int> f = p.get_future();
	p.set(42);  // Assign result: 42.
	f.get();  // Returns 42.

	cb_promise<int> p;
	cb_future<int> f = p.get_future();
	p.set_exception(std::make_exception_ptr(std::exception()));  // Assign result.
	f.get();  // Throws std::exception.

	cb_promise<int> p;
	cb_future<int> f = p.get_future();
	f.get();  // Blocks until p is assigned to (which in this example takes forever).

	cb_future<int> f = cb_promise<int>().get_future();
	f.get();  // Blocks forever, since promise was broken.

	cb_future<int> f;  // Not initialized.
	f.get();  // Throws future_error, since the future has no association.


Instead of calling ```cb_future<T>::get()``` to wait for a promise to complete, the ```cb_future<T>::wait()``` method can be used.
The ```cb_future<T>::wait()``` method will not throw an exception and will not return the assigned value.
It will simply block execution until the promise completes, at which point it will return.

	class cb_future<T> {
	 public:
	  T get();  // Returns the result, using move construction.
	  void wait() const;  // Wait for the promise to complete.
	  future_status wait_for(const std::chrono::duration&) const;  // Timed-wait for the promise to complete.
	  future_status wait_until(const std::chrono::time_point&) const;  // Timed-wait for the promise to complete.
	};

The ```cb_future<T>::get()``` method will throw ```future_error``` with ```future_errc::future_already_retrieved``` after the first ```cb_future<T>::get()``` returns.


The ```cb_future<T>::valid()``` method can be used to test if a future holds an associated state.
Note that only the presence of a shared state is tested for, not if the future can actually complete (i.e. its associated promise still lives).

	class cb_future<T> {
	 public:
	  bool valid() const noexcept;
	};


Futures can be associated with a deferred promise.
A deferred promise is one that will be lazily started.
The ```cb_future<T>::start()``` method is used to explicitly start them.

	class cb_future<T> {
	 public:
	  void start() const;
	};

	cb_future<int> f = async_lazy([]() -> int { return 42; });
	// Note that the lambda above hasn't run yet.
	f.start();  // Start the lambda.
	f.start();  // No effect, since it has already started.
	int fourty_two = f.get();  // Block until the lambda completes, then returns 42.


A future can be shared, using ```shared_cb_future<T>```.
This can be achieved by using the constructor:

	template<typename T>
	class shared_cb_future {
	 public:
	  shared_cb_future();  // No state.
	  shared_cb_future(cb_future<T>&&);  // Construct from future.
	};

or alternatively, by using ```cb_future<T>::share()```.

	cb_future<T> f = ...;
	shared_cb_future<T> g = f.share();


Asynchronous future resulution
-------------------------------

Instead of using promise and future directly, they can be used via events, which are installed via the global ```callback()``` methods.
Callbacks are invoked as soon as the associated promise is assigned a result.

	template<typename T>
	void callback(cb_future<T>&& f,
	              std::function<void(cb_future<T>)> cb);
	template<typename T>
	void callback(shared_cb_future<T> f,
	              std::function<void(shared_cb_future<T>)> cb,
	              promise_start = promise_start::start);

Calling callback methods on uninitialized promises or futures will result in an ```future_error``` exception being thrown, with ```future_errc::no_state```.
The callback functions may fail if insufficient memory is available, in which case ```std::bad_alloc``` will be thrown.
The additional memory required for installing callbacks is allocated via the allocator initially used to create the associated promise.

If the promise is never assigned a value, the callback won't be invoked.
Otherwise, the callback will be invoked exactly once.
The future is guaranteed not to block.
The callback may not throw exceptions.
If the callback is installed on an already initialized promise, the callback will be invoked immediately.


Composition
-----------

Instead of creating a promise and then installing a callback, these two actions are best combined at promise construction, using the global ```async_lazy()``` function.
```async_lazy()``` will lazily evaluate its result (i.e. it won't start until either ```cb_future<T>::get()```, ```cb_future<T>::wait()``` or ```cb_future<T>::start()``` have been called).

	template<typename Fn, typename... Args>
	cb_future<...> async_lazy(Fn&&, Args&&...);

	cb_future<int> the_answer = async_lazy([](int x, int y) -> int {
	                                         return x * y;
	                                       },
	                                       6, 7);
	the_answer.get();  // Invoke callback and return 42.

The async functions can also be used to do compositing.
Any future arguments passed to them, will be resolved prior to running and their result substituted in the function arguments.

	cb_future<int> six = async_lazy([]() -> int { return 6; });
	cb_future<int> seven = async_lazy([]() -> int { return 7; });
	cb_future<int> the_answer = async_lazy([](int x, int y) -> int {
	                                         return x * y;
	                                       },
	                                       std::move(six),
	                                       std::move(seven));
	the_answer.get();  // Invoke all callbacks and return 42.

Compositing works with ```std::future<T>``` and ```std::shared_future<T>``` too.
However, because the futures from the STL don't support callbacks, their resolution will block.

This even works for the function argument.

	cb_promise<std::function<int(int, int)>> p;
	p.set_value([](int x, int y) -> int { return x * y; });
	cb_future<int> the_answer = async_lazy(p.get_future(), 6, 7);
	the_answer.get();  // Invoke all callbacks and return 42.


Instead of using async_lazy, you can build async functions using a workq.

	template<typename Fn, typename... Args>
	cb_future<...> async(workq_ptr, Fn&&, Args&&...);

	template<typename Fn, typename... Args>
	cb_future<...> async(workq_service_ptr, Fn&&, Args&&...);

	template<typename Fn, typename... Args>
	cb_future<...> async(workq_ptr, launch, Fn&&, Args&&...);

	template<typename Fn, typename... Args>
	cb_future<...> async(workq_service_ptr, launch, Fn&&, Args&&...);

Each of these will put use a workq to complete setting the result.

	workq_ptr my_workq = ...;
	cb_future<void> f = async(my_workq, []() { return; });
	f.get();  // Blocks until the workq completes the callback.


Advanced asynchronous promises
------------------------------

When combining promises and workqs, the need may arise to pass a promise around multiple workq jobs before assiging it a value.
The usual case (simply creating a promise) works well, but it takes away from the composition pattern.
In order to facilitate this without sacrificing composition, the ```pass_promise()``` decorator can be used on the functor.

	template<typename ResultType, typename Fn>
	pass_promise_t<T, Fn> pass_promise(Fn&&);

The ```T``` argument represents the type for the promise, i.e. ```cb_promise<T>``` argument.
This decorator informs the ```cb_promise<ResultType>``` to pass itself as the first argument of your functor.

	void do_something_with_promise(cb_promise<int>, long);

	cb_future<int> f = async_lazy(pass_promise<int>(&do_something_with_promise),
	                           async_lazy([]() -> long { return 42L; }));
	f.start();  // Invokes:  do_something_with_promise(cb_promise<int>(...), 42L);

Note that this callback (```do_something_with_promise()```) is now responsible for completing the promise.
If ```do_something_with_promise()``` throws an exception, it *will* be assigned to the promise.

Combining the decorator with a functor-future is not implemented (undefined behaviour).
