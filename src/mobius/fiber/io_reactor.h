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

// Wake every fiber of `js` parked in the reactor with MOBIUS_IO_CANCELLED
// (mobius_abort).
void mobius_io_cancel_job_system(JobSystem* js);

// Host-side timers: `cb(arg)` runs on the reactor thread at `deadline_ms`
// (monotonic clock, mobius_io_now_ms), under the reactor's lock, so it must
// be quick. After mobius_io_cancel_timer returns, it neither runs nor will.
#include <cstdint>
int64_t mobius_io_now_ms();
uint64_t mobius_io_add_timer(int64_t deadline_ms, void (*cb)(void*), void* arg);
void mobius_io_cancel_timer(uint64_t id);

#endif // MOBIUS_FIBER_IO_REACTOR_H
