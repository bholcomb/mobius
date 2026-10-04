#ifndef MOBIUS_MODULES_PROCESS_PLATFORM_H
#define MOBIUS_MODULES_PROCESS_PLATFORM_H

// What the process module needs from the operating system. The build
// compiles the implementation for its target:
//
//   Linux    process_platform_posix.cpp + process_platform_linux.cpp
//   macOS    process_platform_posix.cpp + process_platform_macos.cpp
//   Windows  process_platform_win32.cpp
//
// Pipes to a child are read and written through io_platform.h. Handles
// are as there: file descriptors, or HANDLEs on Windows; -1 means none.

#include <mobius/mobius_plugin.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// A started child process.
struct ProcChild;

// Where one of the child's standard streams goes.
enum class StdioKind {
    inherit,    // share ours
    pipe,       // a pipe; we get the other end
    null_dev,   // nothing (reads end at once, writes are discarded)
    file,       // a file: read (stdin), or written, replaced or appended to
    handle,     // an open handle of ours (an io stream's)
    merge,      // stderr only: wherever stdout goes
};

struct StdioSpec {
    StdioKind kind = StdioKind::inherit;
    std::string path;       // file
    bool append = false;    // file, for output
    intptr_t handle = -1;   // handle
};

struct SpawnRequest {
    // The program and its arguments, passed exactly; or with `shell`, one
    // command line run by the platform's shell (/bin/sh -c, cmd.exe /c).
    std::vector<std::string> argv;
    bool shell = false;
    std::map<std::string, std::string> env;   // the child's whole environment
    bool has_cwd = false;
    std::string cwd;
    StdioSpec stdio[3];
};

// Start a child. A program name without a directory is looked up in the
// PATH of the child's environment. parent[i] receives our end of each
// piped stream (-1 for others). Null on failure, with *err describing it.
ProcChild* proc_spawn(const SpawnRequest& req, intptr_t parent[3], std::string* err);

// This process's environment.
void proc_environment(std::map<std::string, std::string>* env);

int64_t proc_pid(const ProcChild* child);

// Check (without waiting) whether the child has exited; once it has, its
// exit status is kept and this stays true.
bool proc_try_reap(ProcChild* child);

// After exit: the exit code, or -N if a signal N ended it.
int64_t proc_exit_code(const ProcChild* child);

// Wait (parked, in a fiber) until the child may have exited or timeout_ms
// passes (-1: no limit). Returns a MOBIUS_IO_ code: >= 0 or
// MOBIUS_IO_TIMEOUT to check again with proc_try_reap,
// MOBIUS_IO_CANCELLED if the fiber was cancelled.
int proc_wait_exit(MobiusState* state, ProcChild* child, int64_t timeout_ms);

enum class ProcSignal { term, kill, interrupt, hangup };

// Signal a running child (Windows: any signal terminates it). False if it
// could not be sent.
bool proc_signal(ProcChild* child, ProcSignal sig);

// Free a child. Reaps it if it has exited; a child still running keeps
// running.
void proc_release(ProcChild* child);

// Process-wide setup when the module loads.
void proc_init();

#endif // MOBIUS_MODULES_PROCESS_PLATFORM_H
