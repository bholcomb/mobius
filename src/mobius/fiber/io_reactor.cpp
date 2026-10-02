// Fiber-aware waiting for I/O and timers.
//
// A fiber that would block on a descriptor parks (JobSystem::beginPark /
// park) and registers with the reactor: one process-wide thread running
// epoll_wait over the waited descriptors, with the nearest deadline as its
// timeout. When a descriptor becomes ready, its deadline passes, the fiber
// is cancelled (fiber.cancel) or another fiber closes the descriptor
// (mobius_io_wake_fd), the reactor records the outcome and wakes the fiber.
// Workers never block on I/O and waiting fibers are never polled.
//
// Each registration adds a dup() of the descriptor to epoll, so any number
// of fibers can wait on one descriptor (epoll accepts a descriptor once).
// Registrations are found by id under the reactor lock, so an event for a
// waiter that has already left is ignored.
//
// Outside a fiber (an embedding host calling in directly, or no job
// system) the wait falls back to a blocking poll() on the calling thread.

#include <mobius/mobius_plugin.h>

#include "fiber/fiber.h"
#include "fiber/job_system.h"
#include "data/future.h"
#include "state/mobius_state.h"
#include "vm/vm.h"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {

static const int RESULT_PENDING = INT_MIN;

// Whether the reactor exists yet, so shutdown need not start it.
static std::atomic<bool> g_reactor_started{false};

static int64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct IoWaiter;

struct TimerEntry {
    IoWaiter* waiter;               // a fiber's timeout, or
    JobSystem* js;
    void (*callback)(void*);        // a host-side callback timer
    void* callback_arg;
    uint64_t callback_id;
};

struct Registration {
    IoWaiter* waiter;
    JobSystem* js;  // copied: shutdown must not dereference waiter
    int index;      // which of the waiter's descriptors
    int orig_fd;
    int dup_fd;
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

    // Register `w` for `count` descriptors. False if epoll refused one.
    bool add(IoWaiter* w, const MobiusIoWait* waits, int count) {
        std::lock_guard<std::mutex> lock(mu_);
        for (int i = 0; i < count; i++) {
            int dup_fd = dup(waits[i].fd);
            if (dup_fd < 0) { removeLocked(w); return false; }
            uint64_t id = next_id_++;
            struct epoll_event ev = {};
            ev.events = EPOLLONESHOT;
            if (waits[i].events & MOBIUS_IO_READ) ev.events |= EPOLLIN | EPOLLRDHUP;
            if (waits[i].events & MOBIUS_IO_WRITE) ev.events |= EPOLLOUT;
            ev.data.u64 = id;
            if (epoll_ctl(epfd_, EPOLL_CTL_ADD, dup_fd, &ev) != 0) {
                close(dup_fd);
                removeLocked(w);
                return false;
            }
            regs_[id] = Registration{w, w->js, i, waits[i].fd, dup_fd};
            by_fd_[waits[i].fd].insert(w);
            w->reg_ids.push_back(id);
        }
        if (w->deadline >= 0) {
            w->timer = timers_.emplace(w->deadline, TimerEntry{w, w->js, nullptr, nullptr, 0});
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

    void wakeFd(int fd) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = by_fd_.find(fd);
        if (it == by_fd_.end()) return;
        for (IoWaiter* w : it->second) completeLocked(w, MOBIUS_IO_CLOSED);
    }

    // Call `cb(arg)` on the reactor thread at `deadline` (ms, monotonic),
    // under the reactor lock: it must be quick and not call into the
    // reactor. Returns an id for cancelTimer.
    uint64_t addTimer(int64_t deadline, void (*cb)(void*), void* arg) {
        std::lock_guard<std::mutex> lock(mu_);
        uint64_t id = next_id_++;
        auto it = timers_.emplace(deadline, TimerEntry{nullptr, nullptr, cb, arg, id});
        callback_timers_[id] = it;
        kick();
        return id;
    }

    // After this returns the callback is not running and will not run.
    void cancelTimer(uint64_t id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = callback_timers_.find(id);
        if (it == callback_timers_.end()) return;
        timers_.erase(it->second);
        callback_timers_.erase(it);
    }

    // Wake every fiber of `js` parked here with MOBIUS_IO_CANCELLED (abort).
    void cancelJobSystem(JobSystem* js) {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& kv : regs_) if (kv.second.js == js) completeLocked(kv.second.waiter, MOBIUS_IO_CANCELLED);
        for (auto& kv : timers_) if (kv.second.waiter && kv.second.js == js) completeLocked(kv.second.waiter, MOBIUS_IO_CANCELLED);
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
            epoll_ctl(epfd_, EPOLL_CTL_DEL, it->second.dup_fd, nullptr);
            close(it->second.dup_fd);
            it = regs_.erase(it);
        }
        for (auto it = timers_.begin(); it != timers_.end();) {
            if (!it->second.waiter || it->second.js != js) { ++it; continue; }
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
        epfd_ = epoll_create1(EPOLL_CLOEXEC);
        evfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        struct epoll_event ev = {};
        ev.events = EPOLLIN;
        ev.data.u64 = 0;   // id 0: the wakeup eventfd
        epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev);
        std::thread(&IoReactor::loop, this).detach();
    }

    // Wake epoll_wait so it picks up a new (possibly earlier) deadline.
    void kick() {
        uint64_t one = 1;
        ssize_t r = write(evfd_, &one, sizeof(one));
        (void)r;
    }

    void completeLocked(IoWaiter* w, int result) {
        if (w->result != RESULT_PENDING) return;
        w->result = result;
        w->js->wakeFiber(w->fiber);
    }

    void removeLocked(IoWaiter* w) {
        for (uint64_t id : w->reg_ids) {
            auto it = regs_.find(id);
            if (it == regs_.end()) continue;
            epoll_ctl(epfd_, EPOLL_CTL_DEL, it->second.dup_fd, nullptr);
            close(it->second.dup_fd);
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
        struct epoll_event events[64];
        while (true) {
            int timeout = -1;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!timers_.empty()) {
                    int64_t wait = timers_.begin()->first - now_ms();
                    timeout = wait < 0 ? 0 : (wait > INT_MAX ? INT_MAX : (int)wait);
                }
            }
            int n = epoll_wait(epfd_, events, 64, timeout);
            std::lock_guard<std::mutex> lock(mu_);
            for (int i = 0; i < n; i++) {
                uint64_t id = events[i].data.u64;
                if (id == 0) {
                    uint64_t drain;
                    ssize_t r = read(evfd_, &drain, sizeof(drain));
                    (void)r;
                    continue;
                }
                auto it = regs_.find(id);
                if (it == regs_.end()) continue;   // waiter already left
                completeLocked(it->second.waiter, it->second.index);
            }
            int64_t now = now_ms();
            while (!timers_.empty() && timers_.begin()->first <= now) {
                TimerEntry entry = timers_.begin()->second;
                timers_.erase(timers_.begin());
                if (entry.callback) {
                    callback_timers_.erase(entry.callback_id);
                    entry.callback(entry.callback_arg);
                    continue;
                }
                entry.waiter->has_timer = false;
                completeLocked(entry.waiter, MOBIUS_IO_TIMEOUT);
            }
        }
    }

    std::mutex mu_;
    int epfd_ = -1;
    int evfd_ = -1;
    uint64_t next_id_ = 1;
    std::unordered_map<uint64_t, Registration> regs_;
    std::unordered_map<int, std::unordered_set<IoWaiter*>> by_fd_;
    std::unordered_map<FutureValue*, std::unordered_set<IoWaiter*>> by_future_;
    std::multimap<int64_t, TimerEntry> timers_;
    std::unordered_map<uint64_t, std::multimap<int64_t, TimerEntry>::iterator> callback_timers_;
};

// Blocking fallback outside a fiber.
static int poll_wait(const MobiusIoWait* waits, int count, int64_t timeout_ms) {
    std::vector<struct pollfd> fds((size_t)count);
    for (int i = 0; i < count; i++) {
        fds[i].fd = waits[i].fd;
        fds[i].events = 0;
        if (waits[i].events & MOBIUS_IO_READ) fds[i].events |= POLLIN;
        if (waits[i].events & MOBIUS_IO_WRITE) fds[i].events |= POLLOUT;
        fds[i].revents = 0;
    }
    int64_t deadline = timeout_ms >= 0 ? now_ms() + timeout_ms : -1;
    while (true) {
        int t = -1;
        if (deadline >= 0) {
            int64_t left = deadline - now_ms();
            t = left < 0 ? 0 : (left > INT_MAX ? INT_MAX : (int)left);
        }
        int r = poll(fds.data(), (nfds_t)count, t);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return MOBIUS_IO_ERROR;
        if (r == 0) return MOBIUS_IO_TIMEOUT;
        for (int i = 0; i < count; i++) if (fds[i].revents) return i;
    }
}

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
            if (timeout_ms > 0) {
                struct timespec ts = {(time_t)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000000L};
                nanosleep(&ts, nullptr);
            }
            return MOBIUS_IO_TIMEOUT;
        }
        return poll_wait(waits, count, timeout_ms);
    }

    MobiusVM* vm = MobiusVM::t_current_vm;
    FutureValue* future = vm ? vm->future_ : nullptr;
    if ((future && future->isCancelled()) || (vm && vm->state_->abortRequested()))
        return MOBIUS_IO_CANCELLED;

    // Already ready (or a zero timeout): no need to park.
    if (count > 0) {
        int r = poll_wait(waits, count, 0);
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

MOBIUS_API void mobius_io_wake_fd(int fd) {
    IoReactor::get().wakeFd(fd);
}

int64_t mobius_io_now_ms() { return now_ms(); }

uint64_t mobius_io_add_timer(int64_t deadline_ms, void (*cb)(void*), void* arg) {
    return IoReactor::get().addTimer(deadline_ms, cb, arg);
}

void mobius_io_cancel_timer(uint64_t id) {
    if (!id || !g_reactor_started.load(std::memory_order_acquire)) return;
    IoReactor::get().cancelTimer(id);
}

void mobius_io_cancel_job_system(JobSystem* js) {
    if (!g_reactor_started.load(std::memory_order_acquire)) return;
    IoReactor::get().cancelJobSystem(js);
}

void mobius_io_forget_job_system(JobSystem* js) {
    if (!g_reactor_started.load(std::memory_order_acquire)) return;
    IoReactor::get().forgetJobSystem(js);
}

// Wake fibers of `future` parked in mobius_io_wait (fiber.cancel).
void mobius_io_cancel_future(FutureValue* future) {
    IoReactor::get().cancelFuture(future);
}
