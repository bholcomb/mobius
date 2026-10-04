#ifndef MOBIUS_PLATFORM_H
#define MOBIUS_PLATFORM_H

// Everything Mobius needs from the operating system. The build compiles
// the implementation for its target:
//
//   Linux    mobius_platform_posix.cpp + mobius_platform_linux.cpp
//   macOS    mobius_platform_posix.cpp + mobius_platform_macos.cpp
//   Windows  mobius_platform_win32.cpp
//
// The posix file holds what Linux and macOS share; their own files hold
// only what differs.
//
// No other Mobius code uses OS headers or tests which OS it is on. Paths are
// UTF-8 everywhere; a platform converts them as its APIs require.

#include <mobius/mobius_plugin.h>   // MobiusIoWait, MOBIUS_IO_*

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

// Monotonic clock, in nanoseconds.
uint64_t platform_monotonic_ns();

// The running executable's full path ("" if unknown).
std::string platform_executable_path();

// The platform name used for native module folders and module manifests:
// "<os>-<arch>", e.g. "linux-x86_64", "macos-aarch64", "windows-x86_64".
const char* platform_name();

// The file extension of shared libraries, with the dot (".so").
const char* platform_library_extension();

// The file name of the Mobius core library ("libmobius-core.so").
const char* platform_core_library_name();

// The separator in PATH-style lists such as MOBIUS_MODULE_PATH (':').
char platform_path_list_separator();

// The current process's id.
uint64_t platform_process_id();

// The per-user cache directory ("" if unknown), e.g. ~/.cache on Linux.
std::string platform_user_cache_dir();

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool platform_is_regular_file(const std::string& path);
bool platform_is_directory(const std::string& path);

// ---------------------------------------------------------------------------
// Shared libraries
// ---------------------------------------------------------------------------

// Load a shared library; null on failure, with *error set. `global`: make
// its symbols available to libraries loaded after it (a package's runtime
// libraries for its module). A library's own directory is searched for
// the libraries it depends on.
void* platform_library_open(const std::string& path, bool global, std::string* error);
void* platform_library_symbol(void* library, const char* name);
void  platform_library_close(void* library);

// ---------------------------------------------------------------------------
// Fibers
// ---------------------------------------------------------------------------

// An execution context with its own stack, switched to and from
// explicitly. A fiber may be switched to from any thread that has a thread
// fiber, but runs on one thread at a time.
struct PlatformFiber;

// A fiber with at least stack_size usable bytes of stack (and protection
// against overflowing it). Null if it cannot be allocated.
PlatformFiber* platform_fiber_create(size_t stack_size);

// Free a fiber. Never the running one.
void platform_fiber_destroy(PlatformFiber* fiber);

// Set what the fiber runs when next switched to: entry(arg), from the top of
// its stack. Whatever it was running is abandoned. entry must not return.
void platform_fiber_start(PlatformFiber* fiber, void (*entry)(void*), void* arg);

// Save the running context in `from` and run `to`. Returns when something
// switches back to `from`.
void platform_fiber_switch(PlatformFiber* from, PlatformFiber* to);

// The calling thread's own context, to switch from into fibers and back
// to. Each call must be paired with platform_thread_fiber_release() on the
// same thread; nested pairs share one context.
PlatformFiber* platform_thread_fiber();
void platform_thread_fiber_release();

// The bounds of the stack the caller is running on (the lowest usable
// address and the top), for overflow checks. False if unknown.
bool platform_stack_bounds(char** low, char** high);

// ---------------------------------------------------------------------------
// I/O readiness
// ---------------------------------------------------------------------------

// A poller for the I/O reactor: handles (file descriptors; sockets on
// Windows) registered for one readiness event each, under an id.
struct PlatformPoller;

// Null on failure.
PlatformPoller* platform_poller_create();

// Register `handle` for `events` (MOBIUS_IO_READ / MOBIUS_IO_WRITE), once:
// after it is reported ready it is not reported again. Any number of
// registrations may watch the same handle. Returns a token for
// platform_poller_remove, or -1 on failure.
intptr_t platform_poller_add(PlatformPoller* poller, intptr_t handle, int events, uint64_t id);

// Remove a registration (reported or not).
void platform_poller_remove(PlatformPoller* poller, intptr_t token);

// Wait up to timeout_ms (-1: no limit) and append the ids of registrations
// that became ready to `ready`. May return early with none (a wake, a
// signal).
void platform_poller_wait(PlatformPoller* poller, int timeout_ms, std::vector<uint64_t>& ready);

// Make a platform_poller_wait in progress on another thread return.
void platform_poller_wake(PlatformPoller* poller);

// Block the calling thread until one of `waits` is ready or timeout_ms
// passes (-1: no limit). Returns the index of a ready entry,
// MOBIUS_IO_TIMEOUT or MOBIUS_IO_ERROR.
int platform_poll(const MobiusIoWait* waits, int count, int64_t timeout_ms);

// ---------------------------------------------------------------------------
// Compiler support (not the OS)
// ---------------------------------------------------------------------------

// The address of the calling function's frame: the real stack position
// (a local's address may be on AddressSanitizer's heap "fake stack").
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#  define MOBIUS_FRAME_ADDRESS() ((char*)_AddressOfReturnAddress())
#else
#  define MOBIUS_FRAME_ADDRESS() ((char*)__builtin_frame_address(0))
#endif

#endif // MOBIUS_PLATFORM_H
