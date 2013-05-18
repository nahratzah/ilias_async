Workq
=====

The workq system implements concurrent event loops.

The system is divided into 3 layers:
- workq jobs
- the workq
- the workq service

This library is set up so using the workq system is optional.


Workq jobs
----------

A workq job is a single unit of work, that is to be executed in the event loop.
Each job can be activated and deactivated.


Workq
-----

A workq is a grouping of multiple workq jobs, which operate in the same eventloop.
No two jobs sharing the same workq will run concurrently (unless explicitly specified at the job).


Workq service
-------------

The workq service manages the collection of workq jobs and their workqs.
It distributes jobs that need to be run to available worker threads provided by a threadpool.


General workflow
================

Each job must be connected to a workq, while each workq must be connected to a workq service.
The usual flow to creating a workq job is like this:

	workq_service_ptr wqs = new_workq_service();
	workq_ptr wq = wqs->new_workq();
	auto job = wq->new_job([]() {
		std::cout << "Hello world!" << std::endl;
	  });

Activating a job
----------------

The job thusly created will print a friendly greeting each time it is run, but in order for it to run, we need to activate it:

	job->activate();

Each call to ```job->activate()``` will ensure the job runs at least once, at some unspecified time.

Multiple calls to ```job->activate()``` may be combined into a single run of the job:

	for (int i = 0; i < 1000; ++i)
		job->activate();

The above may will print ```Hello world!``` at least once and at most 1000 times.
This is because the activation sets a flag that the job should be invoked, which is cleared automatically when the job runs.

It is safe to activate a job from within itself: a job activating itself will simply run again after the current invocation is completed.

Deactivating a job
------------------

Jobs can also be deactivated.
A call to ```job->deactivate()``` will clear the activation state.
If the job is currently running, the ```job->deactivate()``` call will not return until the job completes, unless ```job->deactivate()``` is run from within the job.
I.e. if you deactivate a job from the outside, you can expect it to not be running when you complete (unless you have created other threads activating the job, ofcourse).

Concurrency
-----------

Multiple jobs on the same workq will not run in parallel.
If we implement a second job to the above workq and both are activated, like this:

	auto second_job = wq->new_job([]() {
		std::cout << "Second job saying hi." << std::endl;
	  });

	job->activate();
	second_job->activate();

Both jobs will print their text, but they will not do so at the same time.
Output may be:

	Hello world!
	Second job saying hi.

or may be:

	Second job saying hi.
	Hello world!

The system will never produce output that interleaves the two outputs, so you will never see something like:

	SHeeclolnod  wjoorbl ds!a
	ying hi.

because the system will never allow multiple threads to run jobs from the same workq.

Making the workq system actually run
------------------------------------

The above code samples neglect one important part: the above code does not actually produce any output.
This is because the workq service is not actually connected to a threadpool service.

One way to make the workq run, is to help it.
For example, to ask the workq to run up to 17 jobs in its queue, call:

	wqs->aid(17);

The ```workq_service::aid()``` function is intended for threads that are to help out only when they have no work to do, for example while a GUI event loop is idle.

Properly running the workq involves hooking it up to an threadpool service.

	#include <ilias/threadpool.h>

	threadpool tp;			// Creates a threadpool with #thread == #cpus.
	threadpool_attach(*wqs, tp);	// Connect the two together.

The lifetime of the threadpool and workq service are not bound together: either can be destroyed without affecting the other (other than putting the threadpool out of a job, or starving the workq_service from being executed).

Lifetime considerations
-----------------------

The main unit of interest, for lifetime considerations, is the workq job.
It uses a shared pointer (std::shared_ptr) and when the pointee (the job) is no longer reachable, the job will be deactivated and destroyed as soon as possible.
If the job is destroyed from within, it will be kept alive until it finishes running and the job destructoin will be performed right after.
Otherwise, the destruction of the job will happen immediately (i.e. the job will be gone after the shared pointer is cleared).

A job that has no shared pointer to it anymore, can no longer be active.
This means that pending activations will be canceled (as if by a call to ```workq_job::deactivate()```) and no new activations can happen.

The ```workq_ptr``` and ```workq_service_ptr``` are both also smart pointers.
The workq will be kept alive until there is no workq_ptr pointing at it, nor are there jobs that depend on it.
The same holds for the workq service: it is kept alive by its ```workq_service_ptr``` instances and any workq depending on it.
In other words: as long as there is any pointer to the stack, the entire stack above it will survive (this property turns out to be tremendously important to writing succesful event driven programs).





TODO
====
Just some points that really need to be discussed before this documentation is ready.

- [X] Describe what a ```workq_job``` is
- [X] Describe what a ```workq_service``` is
- [X] Describe how to control ```workq_service```
- [X] Explain liveness of ```workq_service``` and ```workq```
- [ ] Explain co-routines
- [ ] Explain code hopping between ```workq``` instances
