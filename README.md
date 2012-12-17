ilias_async
===========

Asynchronous message queues and events.


This library provides primitives for asynchronous code:
- workq - an strand of related jobs, none of which may execute concurrently (except for specific cases).
- workq_job - a routine which executes a small amount of work;  can be activated and deactivated.
- workq_service - a manager for multiple workqs, implements scheduling of work units.
- promise - an event-based promise (I may in the future replace this with boost code, if I can figure out the event handling)
- msg_queue - a message queue implementation (WIP)
- refcnt - reference counted pointers
