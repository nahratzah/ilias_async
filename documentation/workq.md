Workq
=====

The workq system implements event loops for multi-threading/multi-cpu.

A workq_job contains a single job in an event loop, which is executed on a workq (the event loop itself).  The workq_service instance shared between one or more workq schedules execution of workq.

This library is set up so using the workq system is optional.

TODO:
[ ] Describe what a ```workq_job``` is
[ ] Describe what a ```workq_service``` is
[ ] Describe how to control ```workq_service```
[ ] Explain liveness of ```workq_service``` and ```workq```
[ ] Explain co-routines
