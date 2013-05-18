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




TODO:
- [ ] Describe what a ```workq_job``` is
- [ ] Describe what a ```workq_service``` is
- [ ] Describe how to control ```workq_service```
- [ ] Explain liveness of ```workq_service``` and ```workq```
- [ ] Explain co-routines
- [ ] Explain code hopping between ```workq``` instances
