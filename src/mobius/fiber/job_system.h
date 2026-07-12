#ifndef MOBIUS_JOB_SYSTEM_H
#define MOBIUS_JOB_SYSTEM_H

#include "fiber/fiber.h"
#include "fiber/fiber_pool.h"
#include "fiber/fiber_context.h"
#include <mobius/mobius.h>

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <thread>
#include <functional>
#include <cstdint>

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

    // Run `fn` as the main fiber. Blocks the calling thread until it completes.
    // Returns the int result from `fn`.
    int executeAsMainFiber(std::function<int()> fn);

    void shutdown();

    FiberPool* fiberPool() { return fiber_pool_; }
    MobiusFiber* currentFiber() const;

    MobiusMetrics& metrics() { return *metrics_; }

private:
    void workerThreadEntry();
    MobiusFiber* dequeueReadyFiber();
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
    static thread_local FiberContext t_scheduler_ctx_;
};

#endif // MOBIUS_JOB_SYSTEM_H
