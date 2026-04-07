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

// A fiber waiting for a counter to reach a target value.
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

    // Lazily initialize the fiber pool on first use.
    void ensureInitialized();

    // Submit a single job for execution on a worker thread.
    void submit(JobDecl job);

    // Submit jobs. counter is incremented by count before jobs are enqueued.
    void submitJobs(JobDecl* jobs, uint32_t count, AtomicCounter* counter);

    // Submit a single fiber directly to the ready queue.
    void submitFiber(MobiusFiber* fiber);

    // Block the current fiber until counter reaches target_value.
    // Switches to the next ready fiber while waiting.
    void waitForCounter(AtomicCounter* counter, int32_t target_value);

    // Yield the current fiber, placing it back on the ready queue.
    void yieldFiber();

    // Enter the worker loop on the calling thread. Returns when
    // shutdown is requested and the calling thread's main fiber completes.
    void runWorkerLoop(MobiusFiber* main_fiber);

    // Signal all workers to stop. Called by MobiusState destructor.
    void shutdown();

    FiberPool* fiberPool() { return fiber_pool_; }
    MobiusFiber* currentFiber() const;

    MobiusMetrics& metrics() { return *metrics_; }

private:
    void workerThreadEntry();
    MobiusFiber* dequeueReadyFiber();
    void wakeWaiters();
    void spawnWorkerIfNeeded();
    void fiberEntry(MobiusFiber* fiber, JobDecl job);

    static void fiberEntryTrampoline(void* arg);

    MobiusState* owner_;
    MobiusMetrics* metrics_;
    FiberPool* fiber_pool_;
    bool initialized_;

    // Ready queue: fibers that are ready to run
    std::mutex ready_mutex_;
    std::condition_variable ready_cv_;
    std::vector<MobiusFiber*> ready_queue_;

    // Job queue: pending jobs without assigned fibers
    std::mutex job_mutex_;
    std::vector<JobDecl> pending_jobs_;

    // Wait list: fibers sleeping on counters
    std::mutex wait_mutex_;
    std::vector<WaitEntry> wait_list_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::mutex worker_mutex_;
    std::atomic<int> active_worker_count_;
    int max_workers_;

    std::atomic<bool> shutdown_requested_;

    // Per-thread current fiber
    static thread_local MobiusFiber* t_current_fiber_;
    // Per-thread scheduler context (the context to return to from a fiber)
    static thread_local FiberContext t_scheduler_ctx_;
};

#endif // MOBIUS_JOB_SYSTEM_H
