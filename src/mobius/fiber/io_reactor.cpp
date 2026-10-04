// Fiber-aware waiting for I/O and timers.
//
// A fiber that would block on a descriptor parks (JobSystem::beginPark /
// park) and registers with the reactor: one process-wide thread waiting on
// the registered descriptors through the platform's poller (epoll on Linux,
// kqueue on macOS, WSAPoll on Windows, where only sockets can be waited on),
// with the nearest deadline as its timeout. When a descriptor becomes ready,
// its deadline passes, the fiber is cancelled (fiber.cancel) or another
// fiber closes the descriptor (mobius_io_wake_fd), the reactor records the
// outcome and wakes the fiber. Workers never block on I/O and waiting
// fibers are never polled.
//
// Registrations are found by id under the reactor lock, so an event for a
// waiter that has already left is ignored.
//
// Outside a fiber (an embedding host calling in directly, or no job
// system) the wait falls back to a blocking poll on the calling thread.

#include <mobius/mobius_plugin.h>

#include "fiber/fiber.h"
#include "fiber/job_system.h"
#include "data/future.h"
#include "state/mobius_state.h"
#include "platform/mobius_platform.h"
#include "vm/vm.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <climits>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

static int64_t now_ms() { return (int64_t)(platform_monotonic_ns() / 1000000); }

static const int RESULT_PENDING = INT_MIN;

// Whether the reactor exists yet, so shutdown need not start it.
static std::atomic<bool> g_reactor_started{false};

struct IoWaiter;

struct TimerEntry {
    IoWaiter* waiter;
    JobSystem* js;
};

struct Registration {
    IoWaiter* waiter;
    JobSystem* js;  // copied: shutdown must not dereference waiter
    int index;      // which of the waiter's descriptors
    intptr_t orig_fd;
    intptr_t token; // the poller's handle for removing it
};

struct IoWaiter {
    MobiusFiber* fiber = nullptr;
    JobSystem* js = nullptr;
    FutureValue* future = nullptr;   // the fiber's future, for fiber.cancel
    int64_t deadline = -1;           // ms on the monotonic clock; -1 none
    std::vector<uint64_t> reg_ids;
    std::multimap<int64_t, TimerEntry>::iterator timer;
    bool has_timer = false;
    int result = RESULT_PENDING;
};

class IoReactor {
public:
    static IoReactor& get() {
        static IoReactor* r = new IoReactor();   // never destroyed: the thread outlives statics
        g_reactor_started.store(true, std::memory_order_release);
        return *r;
    }

    // Register `w` for `count` descriptors. False if the poller refused one.
    bool add(IoWaiter* w, const MobiusIoWait* waits, int count) {
        std::lock_guard<std::mutex> lock(mu_);
        for (int i = 0; i < count; i++) {
            uint64_t id = next_id_++;
            intptr_t token = platform_poller_add(poller_, waits[i].fd, waits[i].events, id);
            if (token < 0) {
                removeLocked(w);
                return false;
            }
            regs_[id] = Registration{w, w->js, i, waits[i].fd, token};
            by_fd_[waits[i].fd].insert(w);
            w->reg_ids.push_back(id);
        }
        if (w->deadline >= 0) {
            w->timer = timers_.emplace(w->deadline, TimerEntry{w, w->js});
            w->has_timer = true;
        }
        if (w->future) by_future_[w->future].insert(w);
        kick();
        return true;
    }

    void remove(IoWaiter* w) {
        std::lock_guard<std::mutex> lock(mu_);
        removeLocked(w);
    }

    void wakeFd(intptr_t fd) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = by_fd_.find(fd);
        if (it == by_fd_.end()) return;
        for (IoWaiter* w : it->second) completeLocked(w, MOBIUS_IO_CLOSED);
    }

    // Drop every waiter of `js` without waking it: the job system is going
    // away with those fibers still parked. Their stacks may already be
    // freed, so the waiters themselves are never dereferenced here.
    void forgetJobSystem(JobSystem* js) {
        std::lock_guard<std::mutex> lock(mu_);
        std::unordered_set<IoWaiter*> doomed;
        for (auto it = regs_.begin(); it != regs_.end();) {
            if (it->second.js != js) { ++it; continue; }
            doomed.insert(it->second.waiter);
            platform_poller_remove(poller_, it->second.token);
            it = regs_.erase(it);
        }
        for (auto it = timers_.begin(); it != timers_.end();) {
            if (it->second.js != js) { ++it; continue; }
            doomed.insert(it->second.waiter);
            it = timers_.erase(it);
        }
        auto prune = [&doomed](auto& index) {
            for (auto it = index.begin(); it != index.end();) {
                for (IoWaiter* w : doomed) it->second.erase(w);
                it = it->second.empty() ? index.erase(it) : std::next(it);
            }
        };
        prune(by_fd_);
        prune(by_future_);
    }

    void cancelFuture(FutureValue* f) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = by_future_.find(f);
        if (it == by_future_.end()) return;
        for (IoWaiter* w : it->second) completeLocked(w, MOBIUS_IO_CANCELLED);
    }

private:
    IoReactor() {
        poller_ = platform_poller_create();
        if (!poller_) {
            fprintf(stderr, "FATAL: cannot start the I/O reactor\n");
            abort();
        }
        std::thread(&IoReactor::loop, this).detach();
    }

    // Wake the poller so it picks up new registrations and deadlines.
    void kick() { platform_poller_wake(poller_); }

    void completeLocked(IoWaiter* w, int result) {
        if (w->result != RESULT_PENDING) return;
        w->result = result;
        w->js->wakeFiber(w->fiber);
    }

    void removeLocked(IoWaiter* w) {
        for (uint64_t id : w->reg_ids) {
            auto it = regs_.find(id);
            if (it == regs_.end()) continue;
            platform_poller_remove(poller_, it->second.token);
            auto fit = by_fd_.find(it->second.orig_fd);
            if (fit != by_fd_.end()) {
                fit->second.erase(w);
                if (fit->second.empty()) by_fd_.erase(fit);
            }
            regs_.erase(it);
        }
        w->reg_ids.clear();
        if (w->has_timer) {
            timers_.erase(w->timer);
            w->has_timer = false;
        }
        if (w->future) {
            auto it = by_future_.find(w->future);
            if (it != by_future_.end()) {
                it->second.erase(w);
                if (it->second.empty()) by_future_.erase(it);
            }
        }
    }

    void loop() {
        std::vector<uint64_t> ready;
        while (true) {
            int timeout = -1;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!timers_.empty()) {
                    int64_t wait = timers_.begin()->first - now_ms();
                    timeout = wait < 0 ? 0 : (wait > INT_MAX ? INT_MAX : (int)wait);
                }
            }
            ready.clear();
            platform_poller_wait(poller_, timeout, ready);
            std::lock_guard<std::mutex> lock(mu_);
            for (uint64_t id : ready) {
                auto it = regs_.find(id);
                if (it == regs_.end()) continue;   // waiter already left
                completeLocked(it->second.waiter, it->second.index);
            }
            int64_t now = now_ms();
            while (!timers_.empty() && timers_.begin()->first <= now) {
                IoWaiter* w = timers_.begin()->second.waiter;
                timers_.erase(timers_.begin());
                w->has_timer = false;
                completeLocked(w, MOBIUS_IO_TIMEOUT);
            }
        }
    }

    std::mutex mu_;
    PlatformPoller* poller_ = nullptr;
    uint64_t next_id_ = 1;
    std::unordered_map<uint64_t, Registration> regs_;
    std::unordered_map<intptr_t, std::unordered_set<IoWaiter*>> by_fd_;
    std::unordered_map<FutureValue*, std::unordered_set<IoWaiter*>> by_future_;
    std::multimap<int64_t, TimerEntry> timers_;
};

} // namespace

MOBIUS_API int mobius_io_wait(MobiusState* state, const MobiusIoWait* waits, int count,
                              int64_t timeout_ms) {
    if (count < 0) return MOBIUS_IO_ERROR;
    JobSystem* js = state ? state->jobSystem() : nullptr;
    MobiusFiber* fiber = js ? js->currentFiber() : nullptr;
    // A fiber running a host function must not park (see host_call_depth):
    // it waits like a thread.
    if (!fiber || fiber->host_call_depth > 0) {
        if (count == 0) {
            if (timeout_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            return MOBIUS_IO_TIMEOUT;
        }
        return platform_poll(waits, count, timeout_ms);
    }

    MobiusVM* vm = MobiusVM::t_current_vm;
    FutureValue* future = vm ? vm->future_ : nullptr;
    if (future && future->isCancelled()) return MOBIUS_IO_CANCELLED;

    // Already ready (or a zero timeout): no need to park.
    if (count > 0) {
        int r = platform_poll(waits, count, 0);
        if (r >= 0 || r == MOBIUS_IO_ERROR) return r;
    }
    if (timeout_ms == 0) return MOBIUS_IO_TIMEOUT;

    IoWaiter w;
    w.fiber = fiber;
    w.js = js;
    w.future = future;
    w.deadline = timeout_ms > 0 ? now_ms() + timeout_ms : -1;
    IoReactor& reactor = IoReactor::get();
    js->beginPark();
    if (!reactor.add(&w, waits, count)) {
        js->cancelPark();
        return MOBIUS_IO_ERROR;
    }
    js->park();
    reactor.remove(&w);
    if (w.result == RESULT_PENDING) return MOBIUS_IO_ERROR;   // should not happen
    return w.result;
}

MOBIUS_API void mobius_io_wake_fd(intptr_t fd) {
    IoReactor::get().wakeFd(fd);
}

void mobius_io_forget_job_system(JobSystem* js) {
    if (!g_reactor_started.load(std::memory_order_acquire)) return;
    IoReactor::get().forgetJobSystem(js);
}

// Wake fibers of `future` parked in mobius_io_wait (fiber.cancel).
void mobius_io_cancel_future(FutureValue* future) {
    IoReactor::get().cancelFuture(future);
}
