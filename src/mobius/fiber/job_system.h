#ifndef MOBIUS_JOB_SYSTEM_H
#define MOBIUS_JOB_SYSTEM_H

#include "fiber/fiber.h"
#include "fiber/fiber_pool.h"
#include "platform/mobius_platform.h"
#include <mobius/mobius.h>

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <thread>
#include <functional>
#include <cstdint>
#include <chrono>
#include <string>

struct JobDecl {
    std::function<void()> entry;
};

class AtomicCounter {
public:
    explicit AtomicCounter(int32_t initial = 0) : value_(initial) {}
    int32_t decrement() { return value_.fetch_sub(1, std::memory_order_acq_rel) - 1; }
    int32_t increment() { return value_.fetch_add(1, std::memory_order_acq_rel) + 1; }
    int32_t load() const { return value_.load(std::memory_order_acquire); }
    void store(int32_t v) { value_.store(v, std::memory_order_release); }
private:
    std::atomic<int32_t> value_;
};

struct WaitEntry {
    MobiusFiber* fiber;
    AtomicCounter* counter;
    int32_t target_value;
};

class JobSystem {
public:
    explicit JobSystem(MobiusState* owner);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void ensureInitialized();

    void submit(JobDecl job);

    // Spawned jobs that are queued or still running (excludes the main
    // fiber). Zero means no other thread can be mutating the script heap —
    // the GC's quiescence signal.
    int outstandingJobs() const { return outstanding_jobs_.load(std::memory_order_acquire); }
    void submitJobs(JobDecl* jobs, uint32_t count, AtomicCounter* counter);

    void submitFiber(MobiusFiber* fiber);
    void waitForCounter(AtomicCounter* counter, int32_t target_value);
    void yieldFiber();

    // True on a fiber that is running a host function (see
    // MobiusFiber::host_call_depth): it must not wait for other fibers.
    static bool inHostFunction() {
        return t_current_fiber_ && t_current_fiber_->host_call_depth > 0;
    }
    static constexpr const char* kWaitInHostFunctionMessage =
        "cannot wait for another fiber inside a host function";

    // Parking: a fiber that must wait for an event leaves the ready queue
    // instead of being polled. beginPark() before registering the wake
    // source (so a wake that arrives during registration is not lost),
    // then park() to switch out; cancelPark() if registration failed.
    // wakeFiber() (from any thread) puts a parked fiber back in the ready
    // queue; waking a fiber that is not parked does nothing.
    void beginPark();
    void park();
    void cancelPark();
    void wakeFiber(MobiusFiber* fiber);

    // Run `fn` as the main fiber. Blocks the calling thread until it completes.
    // Returns the int result from `fn`.
    int executeAsMainFiber(std::function<int()> fn);

    void shutdown();

    FiberPool* fiberPool() { return fiber_pool_; }
    MobiusFiber* currentFiber() const;

    // Fibers blocked waiting on other fibers (await, fiber.all/any, channel
    // send/recv); see BlockingWait.
    void enterBlockingWait() { blocked_fibers_.fetch_add(1, std::memory_order_acq_rel); }
    void leaveBlockingWait() { blocked_fibers_.fetch_sub(1, std::memory_order_acq_rel); }

    // True when spawned work can never start: jobs are queued, the fiber
    // pool is at max_fiber_pool_size with no free fiber, and every live
    // fiber is blocked waiting. Nothing can then free a fiber, so the
    // waiters would spin forever (e.g. more than 256 nested awaits).
    bool fiberLimitDeadlock();
    // The error to raise in a waiter when fiberLimitDeadlock() holds.
    std::string fiberLimitDeadlockMessage();

    MobiusMetrics& metrics() { return *metrics_; }

private:
    void workerThreadEntry();
    MobiusFiber* dequeueReadyFiber();
    void runFiber(MobiusFiber* fiber);
    bool mainFiberDone();
    void wakeWaiters();
    void spawnWorkerIfNeeded();

    static void fiberEntryTrampoline(void* arg);

    MobiusState* owner_;
    MobiusMetrics* metrics_;
    FiberPool* fiber_pool_;
    bool initialized_;

    std::mutex ready_mutex_;
    std::condition_variable ready_cv_;
    std::vector<MobiusFiber*> ready_queue_;

    std::mutex job_mutex_;
    std::vector<JobDecl> pending_jobs_;

    std::mutex wait_mutex_;
    std::vector<WaitEntry> wait_list_;

    std::vector<std::thread> workers_;
    std::mutex worker_mutex_;
    std::atomic<int> active_worker_count_;
    std::atomic<int> outstanding_jobs_{0};
    std::atomic<int> blocked_fibers_{0};
    int max_workers_;

    std::atomic<bool> shutdown_requested_;

    // Main fiber tracking for executeAsMainFiber
    std::mutex main_done_mutex_;
    std::condition_variable main_done_cv_;
    MobiusFiber* main_fiber_ = nullptr;
    int main_fiber_result_ = 0;
    bool main_fiber_done_ = false;

    // Dedicated, larger-stacked fiber for the top-level script (see
    // executeAsMainFiber). Created lazily, reused across calls, freed in dtor.
    MobiusFiber* dedicated_main_fiber_ = nullptr;

    static thread_local MobiusFiber* t_current_fiber_;
    static thread_local PlatformFiber* t_scheduler_ctx_;   // this thread's own context
};

// Scope guard for a fiber's wait loop. Registers the fiber as blocked for
// the deadlock check, and reports a deadlock only once the condition has
// held for 500 ms: a waiter whose value has just arrived still counts as
// blocked until it polls again, while a real deadlock lasts forever.
class BlockingWait {
public:
    explicit BlockingWait(JobSystem* js) : js_(js) {
        if (js_) js_->enterBlockingWait();
    }
    ~BlockingWait() {
        if (js_) js_->leaveBlockingWait();
    }
    BlockingWait(const BlockingWait&) = delete;
    BlockingWait& operator=(const BlockingWait&) = delete;

    // Call once per iteration of the wait loop.
    bool deadlocked() {
        if (!js_ || !js_->fiberLimitDeadlock()) {
            suspect_ = false;
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        if (!suspect_) {
            suspect_ = true;
            since_ = now;
            return false;
        }
        return now - since_ >= std::chrono::milliseconds(500);
    }

private:
    JobSystem* js_;
    bool suspect_ = false;
    std::chrono::steady_clock::time_point since_;
};

#endif // MOBIUS_JOB_SYSTEM_H
