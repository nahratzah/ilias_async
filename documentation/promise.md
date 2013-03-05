Promises and futures
====================

C++11 introduces [promises and futures](http://www.stroustrup.com/C++11FAQ.html#std-future "std::future and std::promise") to handle deferred and asynchronous resolution.  The promises and futures in this library are an adaptation, which attach events to promises/futures.

A ```promise<T>``` is considered live (can be completed) if at least one thread holds an instance of this promise or if an event is installed, that can complete the promise.  A promise is broken once it is no longer live.  A ```promise<T>``` can be implicitly converted to a ```future<T>```, where the promise is used to publish a value and the future is used to read this value.  Note that a promise becomes immutable once it has been assigned a value.


TODO
- [ ] Describe promise callback
- [ ] Describe future callbacks
- [ ] Describe promise start commands
