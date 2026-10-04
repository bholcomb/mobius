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
#include "util/platform.h"
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

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "Ws2_32.lib")
#  endif
#elif defined(__APPLE__)
#  include <poll.h>
#  include <sys/event.h>
#  include <unistd.h>
#else
#  include <poll.h>
#  include <sys/epoll.h>
#  include <sys/eventfd.h>
#  include <unistd.h>
#endif

namespace {

static int64_t now_ms() { return (int64_t)(platform_monotonic_ns() / 1000000); }

// ---------------------------------------------------------------------------
// Pollers: register a descriptor for readiness (one shot) under an id,
// unregister it by the token add() returned, wait for ready ids, and wake
// a waiting wait() from another thread. add/remove are called under the
// reactor lock; wait runs without it.
// ---------------------------------------------------------------------------

#if defined(_WIN32)

// WSAPoll has no registrations: the poller keeps the set and passes it to
// each call. Only sockets can be waited on. A loopback UDP socket wakes it.
class Poller {
public:
    bool init() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        wake_rx_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        wake_tx_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (wake_rx_ == INVALID_SOCKET || wake_tx_ == INVALID_SOCKET) return false;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(wake_rx_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
        int len = sizeof(wake_addr_);
        if (getsockname(wake_rx_, (sockaddr*)&wake_addr_, &len) != 0) return false;
        u_long nonblocking = 1;
        ioctlsocket(wake_rx_, FIONBIO, &nonblocking);
        return true;
    }

    intptr_t add(intptr_t handle, int events, uint64_t id) {
        std::lock_guard<std::mutex> lock(mu_);
        Entry e;
        e.socket = (SOCKET)handle;
        e.events = 0;
        if (events & MOBIUS_IO_READ) e.events |= POLLRDNORM;
        if (events & MOBIUS_IO_WRITE) e.events |= POLLWRNORM;
        entries_[id] = e;
        return (intptr_t)id;
    }

    void remove(intptr_t token) {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.erase((uint64_t)token);
    }

    void wait(int timeout_ms, std::vector<uint64_t>& ready) {
        std::vector<WSAPOLLFD> fds;
        std::vector<uint64_t> ids;
        {
            std::lock_guard<std::mutex> lock(mu_);
            fds.push_back({wake_rx_, POLLRDNORM, 0});
            ids.push_back(0);
            for (auto& kv : entries_) {
                if (kv.second.fired) continue;
                fds.push_back({kv.second.socket, kv.second.events, 0});
                ids.push_back(kv.first);
            }
        }
        int n = WSAPoll(fds.data(), (ULONG)fds.size(), timeout_ms);
        if (n <= 0) return;
        std::lock_guard<std::mutex> lock(mu_);
        for (size_t i = 0; i < fds.size(); i++) {
            if (!fds[i].revents) continue;
            if (ids[i] == 0) {
                char drain[64];
                while (recv(wake_rx_, drain, sizeof(drain), 0) > 0) {}
                continue;
            }
            auto it = entries_.find(ids[i]);
            if (it == entries_.end()) continue;
            it->second.fired = true;   // one shot, like the other pollers
            ready.push_back(ids[i]);
        }
    }

    void wake() {
        char one = 1;
        sendto(wake_tx_, &one, 1, 0, (sockaddr*)&wake_addr_, sizeof(wake_addr_));
    }

private:
    struct Entry {
        SOCKET socket = INVALID_SOCKET;
        SHORT events = 0;
        bool fired = false;
    };
    std::mutex mu_;
    std::unordered_map<uint64_t, Entry> entries_;
    SOCKET wake_rx_ = INVALID_SOCKET, wake_tx_ = INVALID_SOCKET;
    sockaddr_in wake_addr_ = {};
};

#elif defined(__APPLE__)

// kqueue. Each registration watches a dup() of the descriptor, so any
// number of fibers can wait on one descriptor; closing the dup removes its
// events. EVFILT_USER wakes it.
class Poller {
public:
    bool init() {
        kq_ = kqueue();
        if (kq_ < 0) return false;
        struct kevent ev;
        EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        return kevent(kq_, &ev, 1, nullptr, 0, nullptr) == 0;
    }

    intptr_t add(intptr_t handle, int events, uint64_t id) {
        int dup_fd = dup((int)handle);
        if (dup_fd < 0) return -1;
        struct kevent changes[2];
        int n = 0;
        if (events & MOBIUS_IO_READ)
            EV_SET(&changes[n++], dup_fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void*)(uintptr_t)id);
        if (events & MOBIUS_IO_WRITE)
            EV_SET(&changes[n++], dup_fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, (void*)(uintptr_t)id);
        if (kevent(kq_, changes, n, nullptr, 0, nullptr) != 0) {
            close(dup_fd);
            return -1;
        }
        return dup_fd;
    }

    void remove(intptr_t token) { close((int)token); }

    void wait(int timeout_ms, std::vector<uint64_t>& ready) {
        struct kevent events[64];
        struct timespec ts;
        struct timespec* tsp = nullptr;
        if (timeout_ms >= 0) {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
            tsp = &ts;
        }
        int n = kevent(kq_, nullptr, 0, events, 64, tsp);
        for (int i = 0; i < n; i++) {
            if (events[i].filter == EVFILT_USER) continue;
            ready.push_back((uint64_t)(uintptr_t)events[i].udata);
        }
    }

    void wake() {
        struct kevent ev;
        EV_SET(&ev, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        kevent(kq_, &ev, 1, nullptr, 0, nullptr);
    }

private:
    int kq_ = -1;
};

#else

// epoll. Each registration watches a dup() of the descriptor, so any
// number of fibers can wait on one descriptor (epoll accepts a descriptor
// once). An eventfd wakes it.
class Poller {
public:
    bool init() {
        epfd_ = epoll_create1(EPOLL_CLOEXEC);
        evfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (epfd_ < 0 || evfd_ < 0) return false;
        struct epoll_event ev = {};
        ev.events = EPOLLIN;
        ev.data.u64 = 0;   // id 0: the wakeup eventfd
        return epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == 0;
    }

    intptr_t add(intptr_t handle, int events, uint64_t id) {
        int dup_fd = dup((int)handle);
        if (dup_fd < 0) return -1;
        struct epoll_event ev = {};
        ev.events = EPOLLONESHOT;
        if (events & MOBIUS_IO_READ) ev.events |= EPOLLIN | EPOLLRDHUP;
        if (events & MOBIUS_IO_WRITE) ev.events |= EPOLLOUT;
        ev.data.u64 = id;
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, dup_fd, &ev) != 0) {
            close(dup_fd);
            return -1;
        }
        return dup_fd;
    }

    void remove(intptr_t token) {
        epoll_ctl(epfd_, EPOLL_CTL_DEL, (int)token, nullptr);
        close((int)token);
    }

    void wait(int timeout_ms, std::vector<uint64_t>& ready) {
        struct epoll_event events[64];
        int n = epoll_wait(epfd_, events, 64, timeout_ms);
        for (int i = 0; i < n; i++) {
            uint64_t id = events[i].data.u64;
            if (id == 0) {
                uint64_t drain;
                ssize_t r = read(evfd_, &drain, sizeof(drain));
                (void)r;
                continue;
            }
            ready.push_back(id);
        }
    }

    void wake() {
        uint64_t one = 1;
        ssize_t r = write(evfd_, &one, sizeof(one));
        (void)r;
    }

private:
    int epfd_ = -1;
    int evfd_ = -1;
};

#endif

// Blocking readiness check outside the reactor (no fiber, or a quick
// "already ready?" check): index of a ready descriptor, or an MOBIUS_IO_ code.
static int poll_wait(const MobiusIoWait* waits, int count, int64_t timeout_ms) {
#if defined(_WIN32)
    std::vector<WSAPOLLFD> fds((size_t)count);
    for (int i = 0; i < count; i++) {
        fds[i].fd = (SOCKET)waits[i].fd;
        fds[i].events = 0;
        if (waits[i].events & MOBIUS_IO_READ) fds[i].events |= POLLRDNORM;
        if (waits[i].events & MOBIUS_IO_WRITE) fds[i].events |= POLLWRNORM;
        fds[i].revents = 0;
    }
#else
    std::vector<struct pollfd> fds((size_t)count);
    for (int i = 0; i < count; i++) {
        fds[i].fd = (int)waits[i].fd;
        fds[i].events = 0;
        if (waits[i].events & MOBIUS_IO_READ) fds[i].events |= POLLIN;
        if (waits[i].events & MOBIUS_IO_WRITE) fds[i].events |= POLLOUT;
        fds[i].revents = 0;
    }
#endif
    int64_t deadline = timeout_ms >= 0 ? now_ms() + timeout_ms : -1;
    while (true) {
        int t = -1;
        if (deadline >= 0) {
            int64_t left = deadline - now_ms();
            t = left < 0 ? 0 : (left > INT_MAX ? INT_MAX : (int)left);
        }
#if defined(_WIN32)
        int r = WSAPoll(fds.data(), (ULONG)count, t);
        if (r < 0) return MOBIUS_IO_ERROR;
#else
        int r = poll(fds.data(), (nfds_t)count, t);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return MOBIUS_IO_ERROR;
#endif
        if (r == 0) return MOBIUS_IO_TIMEOUT;
        for (int i = 0; i < count; i++) if (fds[i].revents) return i;
    }
}

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
            intptr_t token = poller_.add(waits[i].fd, waits[i].events, id);
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
            poller_.remove(it->second.token);
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
        if (!poller_.init()) {
            fprintf(stderr, "FATAL: cannot start the I/O reactor\n");
            abort();
        }
        std::thread(&IoReactor::loop, this).detach();
    }

    // Wake the poller so it picks up new registrations and deadlines.
    void kick() { poller_.wake(); }

    void completeLocked(IoWaiter* w, int result) {
        if (w->result != RESULT_PENDING) return;
        w->result = result;
        w->js->wakeFiber(w->fiber);
    }

    void removeLocked(IoWaiter* w) {
        for (uint64_t id : w->reg_ids) {
            auto it = regs_.find(id);
            if (it == regs_.end()) continue;
            poller_.remove(it->second.token);
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
            poller_.wait(timeout, ready);
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
    Poller poller_;
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
        return poll_wait(waits, count, timeout_ms);
    }

    MobiusVM* vm = MobiusVM::t_current_vm;
    FutureValue* future = vm ? vm->future_ : nullptr;
    if (future && future->isCancelled()) return MOBIUS_IO_CANCELLED;

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
