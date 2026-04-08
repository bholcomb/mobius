#include "fiber/job_system.h"
#include "state/mobius_state.h"
#include "vm/vm.h"

#include <cstdio>
#include <chrono>
#include <algorithm>
#include <thread>


thread_local MobiusFiber* JobSystem::t_current_fiber_ = nullptr;
thread_local FiberContext JobSystem::t_scheduler_ctx_ = {};

struct FiberStartData {
    JobSystem* system;
    JobDecl job;
};

void JobSystem::fiberEntryTrampoline(void* arg) {
    FiberStartData* data = static_cast<FiberStartData*>(arg);
    JobDecl job = std::move(data->job);
    delete data;

    if (job.entry) {
        job.entry();
    }

    MobiusFiber* self = t_current_fiber_;
    self->state = FiberState::Dead;
    fiber_context_swap(&self->context, &t_scheduler_ctx_);

    fprintf(stderr, "FATAL: returned to completed fiber\n");
    abort();
}

JobSystem::JobSystem(MobiusState* owner)
    : owner_(owner), metrics_(&owner->metrics()),
      fiber_pool_(nullptr), initialized_(false),
      active_worker_count_(0),
      max_workers_(owner->config().max_worker_threads),
      shutdown_requested_(false) {
}

JobSystem::~JobSystem() {
    shutdown();
    delete fiber_pool_;
}

void JobSystem::ensureInitialized() {
    if (initialized_) return;
    initialized_ = true;

    const MobiusConfig& cfg = owner_->config();
    fiber_pool_ = new FiberPool(
        cfg.fiber_stack_size,
        cfg.initial_fiber_pool_size,
        cfg.max_fiber_pool_size
    );
}

void JobSystem::submit(JobDecl job) {
    ensureInitialized();

    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        pending_jobs_.push_back(std::move(job));
    }

    metrics_->total_fibers_spawned++;
    ready_cv_.notify_one();
    spawnWorkerIfNeeded();
}

void JobSystem::submitJobs(JobDecl* jobs, uint32_t count, AtomicCounter* counter) {
    ensureInitialized();

    if (counter) {
        for (uint32_t i = 0; i < count; i++) {
            counter->increment();
        }
    }

    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        for (uint32_t i = 0; i < count; i++) {
            pending_jobs_.push_back(jobs[i]);
        }
    }

    metrics_->total_fibers_spawned += count;
    ready_cv_.notify_all();
    spawnWorkerIfNeeded();
}

void JobSystem::submitFiber(MobiusFiber* fiber) {
    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        fiber->state = FiberState::Suspended;
        ready_queue_.push_back(fiber);
    }
    ready_cv_.notify_one();
}

void JobSystem::waitForCounter(AtomicCounter* counter, int32_t target_value) {
    if (counter->load() <= target_value) return;

    MobiusFiber* self = t_current_fiber_;
    if (!self) return;

    {
        std::lock_guard<std::mutex> lock(wait_mutex_);
        wait_list_.push_back({self, counter, target_value});
    }
    self->state = FiberState::Suspended;
    fiber_context_swap(&self->context, &t_scheduler_ctx_);
}

void JobSystem::yieldFiber() {
    MobiusFiber* self = t_current_fiber_;
    if (!self) return;

    self->state = FiberState::Suspended;
    fiber_context_swap(&self->context, &t_scheduler_ctx_);
}

MobiusFiber* JobSystem::currentFiber() const {
    return t_current_fiber_;
}

MobiusFiber* JobSystem::dequeueReadyFiber() {
    wakeWaiters();

    // Interleave: convert ONE pending job into a fiber each time we dequeue,
    // so pending jobs are never starved by the ready queue.
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (!pending_jobs_.empty()) {
            JobDecl job = std::move(pending_jobs_.front());
            pending_jobs_.erase(pending_jobs_.begin());

            MobiusFiber* f = fiber_pool_->acquire();
            if (!f) {
                pending_jobs_.insert(pending_jobs_.begin(), std::move(job));
                // Fall through to try the ready queue instead
            } else {
                size_t active = fiber_pool_->activeCount();
                if (active > metrics_->peak_fibers)
                    metrics_->peak_fibers = active;

                size_t page_size = 4096;
                void* stack_top = static_cast<char*>(f->stack_memory) + page_size;
                FiberStartData* data = new FiberStartData{this, std::move(job)};
                fiber_context_init(&f->context, stack_top, f->stack_size,
                                   fiberEntryTrampoline, data);
                f->state = FiberState::Running;

                // Also spawn more workers if there are still pending jobs
                spawnWorkerIfNeeded();

                return f;
            }
        }
    }

    // Check ready queue
    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        if (!ready_queue_.empty()) {
            MobiusFiber* f = ready_queue_.front();
            ready_queue_.erase(ready_queue_.begin());
            return f;
        }
    }

    return nullptr;
}

void JobSystem::wakeWaiters() {
    std::lock_guard<std::mutex> lock(wait_mutex_);
    auto it = wait_list_.begin();
    while (it != wait_list_.end()) {
        if (it->counter->load() <= it->target_value) {
            MobiusFiber* f = it->fiber;
            it = wait_list_.erase(it);
            std::lock_guard<std::mutex> rlock(ready_mutex_);
            f->state = FiberState::Suspended;
            ready_queue_.push_back(f);
        } else {
            ++it;
        }
    }
}

void JobSystem::spawnWorkerIfNeeded() {
    int current = active_worker_count_.load(std::memory_order_relaxed);
    if (current >= max_workers_) return;

    if (active_worker_count_.compare_exchange_strong(
            current, current + 1, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        workers_.emplace_back(&JobSystem::workerThreadEntry, this);

        size_t count = workers_.size() + 1;
        if (count > metrics_->peak_worker_threads)
            metrics_->peak_worker_threads = count;
    }
}

void JobSystem::workerThreadEntry() {
    fiber_context_convert_thread(&t_scheduler_ctx_);

    auto idle_start = std::chrono::steady_clock::now();
    const auto idle_timeout = std::chrono::milliseconds(1000);

    while (!shutdown_requested_.load(std::memory_order_relaxed)) {
        MobiusFiber* fiber = dequeueReadyFiber();

        if (fiber) {
            idle_start = std::chrono::steady_clock::now();

            t_current_fiber_ = fiber;
            fiber->state = FiberState::Running;
            if (fiber->vm) MobiusVM::t_current_vm = fiber->vm;
            fiber_context_swap(&t_scheduler_ctx_, &fiber->context);
            t_current_fiber_ = nullptr;
            MobiusVM::t_current_vm = nullptr;

            if (fiber->state == FiberState::Dead) {
                metrics_->total_jobs_executed++;
                if (fiber->peak_stack_bytes > metrics_->peak_fiber_stack_bytes)
                    metrics_->peak_fiber_stack_bytes = fiber->peak_stack_bytes;

                // If this was the main fiber, signal the calling thread
                if (fiber == main_fiber_) {
                    std::lock_guard<std::mutex> lock(main_done_mutex_);
                    main_fiber_done_ = true;
                    main_done_cv_.notify_one();
                } else {
                    fiber_pool_->release(fiber);
                }
            } else if (fiber->state == FiberState::Suspended) {
                submitFiber(fiber);
            }
        } else {
            std::unique_lock<std::mutex> lock(ready_mutex_);
            ready_cv_.wait_for(lock, std::chrono::milliseconds(10));

            auto now = std::chrono::steady_clock::now();
            if (now - idle_start > idle_timeout) {
                int count = active_worker_count_.load(std::memory_order_relaxed);
                if (count > 1) {
                    active_worker_count_.fetch_sub(1, std::memory_order_relaxed);
                    return;
                }
            }
        }
    }

    active_worker_count_.fetch_sub(1, std::memory_order_relaxed);
}

int JobSystem::executeAsMainFiber(std::function<int()> fn) {
    ensureInitialized();

    main_fiber_done_ = false;
    main_fiber_result_ = 0;

    MobiusFiber* fiber = fiber_pool_->acquire();
    if (!fiber) {
        fprintf(stderr, "FATAL: cannot acquire fiber for main script\n");
        return -1;
    }
    main_fiber_ = fiber;

    // Wrap fn so we can capture its return value
    int* result_ptr = &main_fiber_result_;
    JobDecl job;
    job.entry = [fn = std::move(fn), result_ptr]() {
        *result_ptr = fn();
    };

    size_t page_size = 4096;
    void* stack_top = static_cast<char*>(fiber->stack_memory) + page_size;
    FiberStartData* data = new FiberStartData{this, std::move(job)};
    fiber_context_init(&fiber->context, stack_top, fiber->stack_size,
                       fiberEntryTrampoline, data);
    fiber->state = FiberState::Suspended;

    // Put the main fiber on the ready queue
    submitFiber(fiber);
    metrics_->total_fibers_spawned++;

    // Ensure at least one worker thread
    spawnWorkerIfNeeded();

    // Block the calling thread until the main fiber completes
    {
        std::unique_lock<std::mutex> lock(main_done_mutex_);
        main_done_cv_.wait(lock, [this] { return main_fiber_done_; });
    }

    fiber_pool_->release(main_fiber_);
    main_fiber_ = nullptr;

    return main_fiber_result_;
}

void JobSystem::shutdown() {
    shutdown_requested_.store(true, std::memory_order_release);
    ready_cv_.notify_all();

    std::lock_guard<std::mutex> lock(worker_mutex_);
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}
