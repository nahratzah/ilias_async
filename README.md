IliasAsync
==========

Asynchronous event-loops, promises and message queues.

This library provides primitives for asynchronous code:
- [workq](documentation/workq.md "workq documentation")
- [promise](documentation/promise.md "promise and future documentation")
- [message queues](documentation/msg_queue.md "message queue documentation")
- refcnt (intrusive reference counting pointers, used a lot inside the library)
- ll (a lock-free linked list, used internally to reduce the need for locking)

Each of the subsystems can be used on its own, be integrated in existing code bases or be used in combination with eachother.
