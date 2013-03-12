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

	template<typename Type>
	promise<Type> new_promise();


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

The default constructor of future creates an uninitialized future (i.e. one not bound to (the shared state of) a promise).  Futures can be derived from promises either by invoking their constructor with the promise or by calling ```new_promise``` with a callback.

	promise<int> p = new_promise<int>();  // Create an initialized promise.
	future<int> f1;  // An uninitialized future.
	future<int> f2 = p;  // Promise bound to future p.
	f1 = p;  // Assign future corresponding to promise p.

	future<int> f = new_promise<int>(my_callback);  // See callbacks on futures and promises.


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


Asynchronous promise resulution
-------------------------------

Instead of using promise and future directly, they can be used via events, which are installed via the global ```callback()``` methods.  This enables asynchronous or lazy resolution of a promise.

	template<typename Type, typename Functor>
	void callback(promise<Type>& f, Functor&& cb);

	class promise<T> {
		void start() noexcept;
	};
	class future<T> {
		void start() noexcept;
	};

Calling callback methods on uninitialized promises or futures will result in an ```uninitialized_promise``` exception being thrown.  The callback functions may fail if insufficient memory is available, in which case ```std::bad_alloc``` will be thrown.

A callback on a promise is used to asynchronously resolve a promise.  The callback must accept invocation as: ```void callback(promise<T> p);```.  The callback will be called at most once, if the promise is started.  Note that the promise callback, in contrast to the asynchronous resolution in the standard, does not return the to-be-assigned value.  By not requiring the to-be-assigned value to be specified immediately, the callback can defer the actual assignment to a later time, or possibly to another thread, simply by passing its ```promise<T>``` argument around.

If the promise callback throws an exception, the exception is assigned to the promise.  Likewise, if the callback returns without assigning a value, the promise will be considered broken once the last reference goes away.


The ```start()``` method on a promise or future, can be used to initiate asynchronous resolution.  The order in which a callback is installed and the promise is started makes no difference:

	void
	install_and_start()
	{
		using the_answer = promise<int>;
		the_answer p;
		callback(p, [](promise<int> arg) { p.set(42); });
		p.start();  // Invokes installed callback.
	}

	void
	start_and_install()
	{
		/* Installing the callback on a started promise will invoke the callback as soon as it is installed. */
		using the_answer = promise<int>;
		the_answer p;
		p.start();
		callback(p, [](promise<int> arg) { p.set(42); });  // Invoked immediately, since the promise was already started.
	}

Calling ```future<T>::start()``` will have the same effect as calling ```promise<T>::start()```, both will start resolution of the promise.  The ```future<T>::wait()``` and ```future<T>::get()``` methods will implicitly start their associated promise.  When a promise completes (i.e. is assigned to) the promise callback will be destroyed and no new callback can be installed.


Instead of creating a promise and then installing a callback, these two actions are best combined at promise construction, using the global ```new_promise(...)``` function.

	template<typename Type, typename... CallbackArgs>
	future<Type> new_promise(CallbackArgs&&... cb_args)
	{
		promise<Type> p = new_promise<Type>();
		callback(p, std::forward<CallbackArgs>(cb_args)...);
		return future<Type>{ p };
	}

	future<int> the_answer = new_promise<int>([](promise<int> p) {
		p.set(42);
	  });


Future events
-------------

The way promises can be resolved asynchronously, also applies to futures.  By installing a callback on a future, the callback will be invoked once the promise completes.  Callbacks installed on futures will be invoked exactly once, when the promise is ready.

	enum promise_start {
		PROM_START,
		PROM_DEFER
	};

	template<typename Type, typename Functor>
	void callback(future<Type>& f, Functor&& cb, promise_start ps = PROM_START);

The callback must have a signature ```void callback(future<Type> f) noexcept```.  The callback will be destroyed after it completes.

Contrary to a promise, a future can have multiple callbacks installed which each will fire.

The ```promise_start``` argument is used to determine if the promise must be started.  ```PROM_START``` (the default) will start the promise, as if by calling ```future<T>::start()```.  ```PROM_DEFER``` will not start the promise.

If a callback is installed on a future that is ready, the callback will be invoked immediately:

	promise<int> p = new_promise<int>();
	future<int> f = p;
	p.set(42);
	callback(f, [](future<int> cb_arg) {
		try {
			std::cout << "The answer to the life, the universe and everything is " << cb_arg.get() << "." << std::endl;
		} catch (...) {
			std::cerr << "Failed to find the answer to the life, the universe and everything." << std::endl;
		}
	  });


Composing promises
------------------

Promises increase their power by combining them.  This process is called *promise composition*.

The combine method handles all details behind the scenes:

	template<typename Type, typename Functor, typename... Futures>
	future<Type> combine(Functor&& functor, Futures&&... input_futures);

	template<typename Type, typename Functor, typename... Futures>
	future<Type> combine(Functor&& functor, std::tuple<Futures...>&& input_futures);

This function will create a ```future<Type>```.  The promise associated with the returned future will have a promise callback installed, which will execute once the returned future is started and each of the input futures completes.  None of the input futures will be started until the returned future is started.

The functor is user-provided code that will calculate the promised value from the input futures.  Its prototype must be: ```void functor(promise<Type>, std::tuple<...> input_futures)```.

Suppose we have a document, that may only be read by an authenticated user.  We need to ask the user for their login and password and only produce the document if it matches.

	using login_promise = promise<std::string>;
	using password_promise = promise<std::string>;
	using document_promise = promise<document_type>;
	using login_future = future<std::string>;
	using password_future = future<std::string>;
	using document_future = future<document_type>;

	/* Declare base promises: login, password and document. */
	login_future login = new_promise<std::string>([](login_promise l) {
		l.set(ask_user_for_login());
	  });
	password_future password = new_promise<std::string>([](password_promise p) {
		p.set(ask_user_for_password());
	  });
	document_future doc = new_promise<document_type>([](document_promise d) {
		d.set(load_document());
	  });

	/* Authenticate future: yields login name iff authentication is succesful. */
	future<std::string> authenticate = combine<std::string>(
	  [](promise<std::string> auth, std::tuple<login_future, password_future> lp) {
		using std::get;

		auto& login = std::get<0>(lp);
		auto& password = std::get<1>(lp);
		if (authenticate_user(login, password))
			auth.set(std::get<0>(lp));
		else
			throw authentication_failed();
	  },
	  login, password);

	/* Create a document promise that only returns the document on succesful authentication. */
	document_future auth_doc = combine<document_type>(
	  [](document_promise out, std::tuple<future<std::string>, document_future> auth_and_doc) {
		using std::get;

		auto& login = std::get<0>(auth_and_doc);
		auto& doc = std::get<1>(auth_and_doc);

		/*
		 * Test if authentication was succesful,
		 * by invoking get() method
		 * (it will throw and thus cascade
		 * the exception on authentication faulure).
		 */
		login.get();

		out.set(doc.get());
	  },
	  authenticate, doc);

	/*
	 * The auth_doc future will hold the document iff
	 * - a login name was provided
	 * - a password was provided
	 * - authentication was succesful
	 * - the document could be loaded
	 */
