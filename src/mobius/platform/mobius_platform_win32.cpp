// Windows implementation of mobius_platform.h.
//
// Fibers are OS fibers (CreateFiberEx), which allocate their own stacks:
// the thread information block must track the running fiber's stack, as
// SEH, stack probes and C++ exceptions require. The reactor's poller is
// WSAPoll, which waits on sockets only, woken through a loopback UDP
// socket. Paths are UTF-8 and converted for the wide-character APIs.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "platform/mobius_platform.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#pragma comment(lib, "Ws2_32.lib")

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string last_error_text() {
    DWORD code = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::string text = msg ? narrow(msg) : "error " + std::to_string(code);
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

} // namespace

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

uint64_t platform_monotonic_ns() {
    static const uint64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return (uint64_t)f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t t = (uint64_t)now.QuadPart;
    return (t / freq) * 1000000000ull + (t % freq) * 1000000000ull / freq;
}

std::string platform_executable_path() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
    if (n == 0 || n >= buf.size()) return "";
    buf.resize(n);
    return narrow(buf);
}

const char* platform_name() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "windows-aarch64";
#else
    return "windows-x86_64";
#endif
}

const char* platform_library_extension() { return ".dll"; }

const char* platform_core_library_name() { return "mobius-core.dll"; }

char platform_path_list_separator() { return ';'; }

uint64_t platform_process_id() { return (uint64_t)GetCurrentProcessId(); }

std::string platform_user_cache_dir() {
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    if (local && *local) return narrow(local);
    return "";
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool platform_is_regular_file(const std::string& path) {
    DWORD attrs = GetFileAttributesW(widen(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool platform_is_directory(const std::string& path) {
    DWORD attrs = GetFileAttributesW(widen(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------------------------------------------------------------------------
// Shared libraries
// ---------------------------------------------------------------------------

void* platform_library_open(const std::string& path, bool global, std::string* error) {
    (void)global;   // each DLL resolves its own imports
    // The DLL's own directory satisfies its dependencies (a package's
    // runtime libraries sit beside its module).
    HMODULE h = LoadLibraryExW(widen(path).c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h && error) *error = last_error_text();
    return (void*)h;
}

void* platform_library_symbol(void* library, const char* name) {
    return library ? (void*)GetProcAddress((HMODULE)library, name) : nullptr;
}

void platform_library_close(void* library) {
    if (library) FreeLibrary((HMODULE)library);
}

// ---------------------------------------------------------------------------
// Fibers
// ---------------------------------------------------------------------------

struct PlatformFiber {
    void* handle = nullptr;     // the OS fiber
    size_t stack_size = 0;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    bool converted = false;     // thread fiber: Mobius converted the thread
};

namespace {

thread_local PlatformFiber* t_thread_fiber = nullptr;
thread_local int t_thread_fiber_refs = 0;
// The context running on this thread, for platform_stack_bounds.
thread_local PlatformFiber* t_running = nullptr;

void WINAPI fiber_proc(LPVOID param) {
    PlatformFiber* f = static_cast<PlatformFiber*>(param);
    f->entry(f->arg);
    fprintf(stderr, "FATAL: fiber entry function returned\n");
    abort();
}

} // namespace

PlatformFiber* platform_fiber_create(size_t stack_size) {
    // The OS fiber is made by platform_fiber_start: its entry is fixed at
    // creation.
    PlatformFiber* f = new PlatformFiber();
    f->stack_size = stack_size;
    return f;
}

void platform_fiber_destroy(PlatformFiber* fiber) {
    if (!fiber) return;
    if (fiber->handle) DeleteFiber(fiber->handle);
    delete fiber;
}

void platform_fiber_start(PlatformFiber* fiber, void (*entry)(void*), void* arg) {
    if (fiber->handle) DeleteFiber(fiber->handle);
    fiber->entry = entry;
    fiber->arg = arg;
    // Reserves stack_size; pages are committed as the stack grows.
    fiber->handle = CreateFiberEx(0, fiber->stack_size, FIBER_FLAG_FLOAT_SWITCH, fiber_proc, fiber);
    if (!fiber->handle) {
        fprintf(stderr, "FATAL: CreateFiberEx failed: %s\n", last_error_text().c_str());
        abort();
    }
}

void platform_fiber_switch(PlatformFiber* from, PlatformFiber* to) {
    (void)from;   // the OS knows the running fiber
    t_running = to;
    SwitchToFiber(to->handle);
}

PlatformFiber* platform_thread_fiber() {
    if (t_thread_fiber_refs++ == 0) {
        PlatformFiber* f = new PlatformFiber();
        if (IsThreadAFiber()) {
            f->handle = GetCurrentFiber();   // the host made it a fiber
        } else {
            f->handle = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
            if (!f->handle) {
                fprintf(stderr, "FATAL: ConvertThreadToFiberEx failed: %s\n", last_error_text().c_str());
                abort();
            }
            f->converted = true;
        }
        t_thread_fiber = f;
        t_running = f;
    }
    return t_thread_fiber;
}

void platform_thread_fiber_release() {
    if (t_thread_fiber_refs > 0 && --t_thread_fiber_refs == 0) {
        if (t_thread_fiber->converted) ConvertFiberToThread();
        delete t_thread_fiber;
        t_thread_fiber = nullptr;
        t_running = nullptr;
    }
}

bool platform_stack_bounds(char** low, char** high) {
    PlatformFiber* f = t_running;
    if (!f || f == t_thread_fiber) return false;   // a thread's own stack: not tracked
    // The thread information block describes the running fiber's stack.
    ULONG_PTR lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    if (lo == 0) return false;
    *low = (char*)lo;
    *high = (char*)hi;
    return true;
}

// ---------------------------------------------------------------------------
// I/O readiness: WSAPoll. It has no registrations, so the poller keeps the
// set and passes it to each call; reported entries stay in the set, marked
// fired, until removed. Only sockets can be waited on. A loopback UDP
// socket (id 0) wakes a wait.
// ---------------------------------------------------------------------------

struct PlatformPoller {
    struct Entry {
        SOCKET socket = INVALID_SOCKET;
        SHORT events = 0;
        bool fired = false;
    };
    std::mutex mu;
    std::unordered_map<uint64_t, Entry> entries;
    SOCKET wake_rx = INVALID_SOCKET;
    SOCKET wake_tx = INVALID_SOCKET;
    sockaddr_in wake_addr = {};
};

PlatformPoller* platform_poller_create() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return nullptr;
    PlatformPoller* p = new PlatformPoller();
    p->wake_rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    p->wake_tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int len = sizeof(p->wake_addr);
    if (p->wake_rx == INVALID_SOCKET || p->wake_tx == INVALID_SOCKET ||
        bind(p->wake_rx, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        getsockname(p->wake_rx, (sockaddr*)&p->wake_addr, &len) != 0) {
        if (p->wake_rx != INVALID_SOCKET) closesocket(p->wake_rx);
        if (p->wake_tx != INVALID_SOCKET) closesocket(p->wake_tx);
        delete p;
        return nullptr;
    }
    u_long nonblocking = 1;
    ioctlsocket(p->wake_rx, FIONBIO, &nonblocking);
    return p;
}

intptr_t platform_poller_add(PlatformPoller* poller, intptr_t handle, int events, uint64_t id) {
    std::lock_guard<std::mutex> lock(poller->mu);
    PlatformPoller::Entry e;
    e.socket = (SOCKET)handle;
    if (events & MOBIUS_IO_READ) e.events |= POLLRDNORM;
    if (events & MOBIUS_IO_WRITE) e.events |= POLLWRNORM;
    poller->entries[id] = e;
    return (intptr_t)id;
}

void platform_poller_remove(PlatformPoller* poller, intptr_t token) {
    std::lock_guard<std::mutex> lock(poller->mu);
    poller->entries.erase((uint64_t)token);
}

void platform_poller_wait(PlatformPoller* poller, int timeout_ms, std::vector<uint64_t>& ready) {
    std::vector<WSAPOLLFD> fds;
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(poller->mu);
        fds.push_back({poller->wake_rx, POLLRDNORM, 0});
        ids.push_back(0);
        for (auto& kv : poller->entries) {
            if (kv.second.fired) continue;
            fds.push_back({kv.second.socket, kv.second.events, 0});
            ids.push_back(kv.first);
        }
    }
    int n = WSAPoll(fds.data(), (ULONG)fds.size(), timeout_ms);
    if (n <= 0) return;
    std::lock_guard<std::mutex> lock(poller->mu);
    for (size_t i = 0; i < fds.size(); i++) {
        if (!fds[i].revents) continue;
        if (ids[i] == 0) {
            char drain[64];
            while (recv(poller->wake_rx, drain, sizeof(drain), 0) > 0) {}
            continue;
        }
        auto it = poller->entries.find(ids[i]);
        if (it == poller->entries.end()) continue;   // removed meanwhile
        it->second.fired = true;
        ready.push_back(ids[i]);
    }
}

void platform_poller_wake(PlatformPoller* poller) {
    char one = 1;
    sendto(poller->wake_tx, &one, 1, 0, (sockaddr*)&poller->wake_addr, sizeof(poller->wake_addr));
}

int platform_poll(const MobiusIoWait* waits, int count, int64_t timeout_ms) {
    std::vector<WSAPOLLFD> fds((size_t)count);
    for (int i = 0; i < count; i++) {
        fds[i].fd = (SOCKET)waits[i].fd;
        fds[i].events = 0;
        if (waits[i].events & MOBIUS_IO_READ) fds[i].events |= POLLRDNORM;
        if (waits[i].events & MOBIUS_IO_WRITE) fds[i].events |= POLLWRNORM;
        fds[i].revents = 0;
    }
    int t = timeout_ms < 0 ? -1 : (timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms);
    int r = WSAPoll(fds.data(), (ULONG)count, t);
    if (r < 0) return MOBIUS_IO_ERROR;
    if (r == 0) return MOBIUS_IO_TIMEOUT;
    for (int i = 0; i < count; i++) if (fds[i].revents) return i;
    return MOBIUS_IO_TIMEOUT;
}
