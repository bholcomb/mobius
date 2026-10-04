#ifndef MOBIUS_FIBER_IO_REACTOR_H
#define MOBIUS_FIBER_IO_REACTOR_H

// Core side of mobius_io_wait (see io_reactor.cpp and mobius_plugin.h).

class FutureValue;
class JobSystem;

// Wake the fibers of `future` parked in mobius_io_wait, so fiber.cancel
// interrupts a fiber waiting on I/O or sleeping.
void mobius_io_cancel_future(FutureValue* future);

// Unregister, without waking, every fiber of `js` still parked in the
// reactor. Called when the job system shuts down.
void mobius_io_forget_job_system(JobSystem* js);

#endif // MOBIUS_FIBER_IO_REACTOR_H
