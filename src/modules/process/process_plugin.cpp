// process: run programs directly (no shell unless asked), with control over
// their standard streams, environment and working directory.
//
// Linux/POSIX implementation on posix_spawn. Pipes to a child are ordinary
// `io` streams (see io_stream.h). run() and communicate() feed stdin and
// drain stdout and stderr in one loop, so a child that fills one pipe
// while we are writing the other can't deadlock.
//
// Every wait (pipe readiness, and the child's exit through a pidfd) goes
// through mobius_io_wait: a fiber waiting on a child parks and its worker
// thread runs other fibers. fiber.cancel interrupts the wait with a
// CancellationError; a child started by run() is then killed (nothing else
// could reach it), one from start() is left running.

#include <mobius/mobius_plugin.h>

#include "../io/io_stream.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

static const char* CHILD_TYPE = "process_child";
static const int TERM_GRACE_MS = 1000;   // SIGTERM, then SIGKILL after this

// The child method table, registered once by process.mob
// (__set_child_methods) and copied onto every child start() returns.
// Held per state, in a hidden global (C++ statics would be shared between
// states, and a value reference belongs to one state).
static const char* CHILD_METHODS_GLOBAL = "__process_child_methods";
static const char* CHILD_METHODS[] = {"wait", "poll", "kill", "communicate"};

struct ChildHandle {
    FiberMutex mu;     // held across waits: a fiber waiting for it yields
    pid_t pid = -1;
    int pidfd = -1;    // readable once the child exits; -1 if unsupported
    bool reaped = false;
    int status = 0;
};

static void child_destructor(void* ptr) {
    ChildHandle* c = static_cast<ChildHandle*>(ptr);
    if (!c) return;
    // Reap if it has already finished; a child still running is left to
    // run (it is not killed because the script dropped its handle).
    if (!c->reaped && c->pid > 0) {
        int st = 0;
        if (waitpid(c->pid, &st, WNOHANG) == c->pid) c->reaped = true;
    }
    if (c->pidfd >= 0) close(c->pidfd);
    delete c;
}

static int64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Exit status as Mobius reports it: the exit code, or -N when the child
// was killed by signal N.
static int64_t exit_code_of(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -(int64_t)WTERMSIG(status);
    return -1;
}

static void mark_reaped(ChildHandle* c, int status) {
    c->reaped = true;
    c->status = status;
    if (c->pidfd >= 0) { close(c->pidfd); c->pidfd = -1; }   // no longer needed
}

// Non-blocking reap. Returns true once the child has exited.
static bool try_reap(ChildHandle* c) {
    if (c->reaped) return true;
    int st = 0;
    pid_t r = waitpid(c->pid, &st, WNOHANG);
    if (r == c->pid) {
        mark_reaped(c, st);
        return true;
    }
    if (r < 0 && errno == ECHILD) {   // reaped elsewhere; status unknown
        mark_reaped(c, 0);
        return true;
    }
    return false;
}

enum class WaitResult { exited, timeout, cancelled };

// Wait until exit or until deadline_ms (<0: no deadline). With
// `cancellable` false the wait ignores fiber.cancel (used to finish
// killing a child).
static WaitResult wait_until(MobiusState* state, ChildHandle* c, int64_t deadline_ms,
                             bool cancellable = true) {
    int pause = 1;
    while (!try_reap(c)) {
        int64_t left = -1;
        if (deadline_ms >= 0) {
            left = deadline_ms - now_ms();
            if (left <= 0) return WaitResult::timeout;
        }
        int rc;
        if (c->pidfd >= 0) {
            MobiusIoWait w = {c->pidfd, MOBIUS_IO_READ};
            rc = mobius_io_wait(state, &w, 1, left);
        } else {
            // No pidfd (old kernel): sleep in short steps.
            rc = mobius_io_wait(state, nullptr, 0, left >= 0 && left < pause ? left : pause);
            if (pause < 20) pause *= 2;
        }
        if (rc == MOBIUS_IO_CANCELLED && cancellable) return WaitResult::cancelled;
    }
    return WaitResult::exited;
}

// Kill a child nobody can reach any more and reap it.
static void kill_and_reap(MobiusState* state, ChildHandle* c) {
    if (!try_reap(c)) kill(c->pid, SIGKILL);
    wait_until(state, c, -1, false);
}

// ---------------------------------------------------------------------------
// Argument and option helpers. Stack indices are absolute.
// ---------------------------------------------------------------------------

static int top(MobiusState* state) { return (int)mobius_stack_size(state) - 1; }

static bool get_string_at(MobiusState* state, int idx, std::string* out) {
    if (!mobius_stack_isString(state, idx)) return false;
    size_t len = 0;
    const char* data = mobius_stack_getStringData(state, idx, &len);
    out->assign(data, len);
    return true;
}

// Field `key` of the options table (or nil when there is no table); pushes
// it on the stack: the caller pops.
static void push_option(MobiusState* state, int opts_idx, const char* key) {
    if (opts_idx < 0) mobius_stack_pushNil(state);
    else mobius_stack_getTableField(state, opts_idx, key);
}

static bool option_bool(MobiusState* state, int opts_idx, const char* key, bool dflt) {
    push_option(state, opts_idx, key);
    bool v = dflt;
    if (mobius_stack_isBool(state, top(state))) v = mobius_stack_getBool(state, top(state));
    mobius_stack_pop(state, 1);
    return v;
}

static bool option_int(MobiusState* state, int opts_idx, const char* key, int64_t* out, std::string* err) {
    push_option(state, opts_idx, key);
    bool ok = true;
    int t = top(state);
    if (mobius_stack_isInteger(state, t)) *out = mobius_stack_getInt64(state, t);
    else if (!mobius_stack_isNil(state, t)) { ok = false; *err = std::string(key) + " must be an integer"; }
    mobius_stack_pop(state, 1);
    return ok;
}

static IoStream* get_stream(MobiusState* state, int idx) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, STREAM_TYPE) != 0) return nullptr;
    return static_cast<IoStream*>(ptr);
}

enum class StdioKind { inherit, pipe, null_dev, file, fd, merge };

struct StdioSpec {
    StdioKind kind = StdioKind::inherit;
    std::string path;      // file
    bool append = false;   // file (output)
    int fd = -1;           // fd: an io stream's descriptor
};

// Parse the stdin/stdout/stderr option: "inherit", "pipe" or "capture",
// "null", {file: path [, append: bool]}, an io stream, or (stderr only)
// "stdout".
static bool parse_stdio(MobiusState* state, int opts_idx, const char* key, int target_fd,
                        StdioKind dflt, StdioSpec* spec, std::string* err) {
    push_option(state, opts_idx, key);
    int t = top(state);
    bool ok = true;
    std::string word;
    if (mobius_stack_isNil(state, t)) {
        spec->kind = dflt;
    } else if (get_string_at(state, t, &word)) {
        if (word == "inherit") spec->kind = StdioKind::inherit;
        else if (word == "pipe" || word == "capture") spec->kind = StdioKind::pipe;
        else if (word == "null") spec->kind = StdioKind::null_dev;
        else if (word == "stdout" && target_fd == 2) spec->kind = StdioKind::merge;
        else {
            ok = false;
            *err = std::string(key) + ": unknown setting '" + word + "' (use inherit, pipe, capture, null, " +
                   (target_fd == 2 ? "stdout, " : "") + "{file: path} or an io stream)";
        }
    } else if (mobius_stack_isTable(state, t)) {
        mobius_stack_getTableField(state, t, "file");
        if (!get_string_at(state, top(state), &spec->path)) {
            ok = false;
            *err = std::string(key) + ": a table setting needs {file: path}";
        }
        mobius_stack_pop(state, 1);
        mobius_stack_getTableField(state, t, "append");
        if (mobius_stack_isBool(state, top(state))) spec->append = mobius_stack_getBool(state, top(state));
        mobius_stack_pop(state, 1);
        spec->kind = StdioKind::file;
    } else if (IoStream* s = get_stream(state, t)) {
        std::lock_guard<FiberMutex> lock(s->mu);
        if (s->closed) {
            ok = false;
            *err = std::string(key) + ": the io stream is closed";
        } else if (s->waitable) {
            spec->kind = StdioKind::fd;
            spec->fd = s->fd;
        } else {
            fflush(s->fp);   // our buffered output goes first
            spec->kind = StdioKind::fd;
            spec->fd = fileno(s->fp);
        }
    } else {
        ok = false;
        *err = std::string(key) + ": expected a string, {file: path} or an io stream";
    }
    mobius_stack_pop(state, 1);
    return ok;
}

// A program name without a '/' is looked up in the PATH the child will see.
static bool resolve_program(const std::string& prog, const std::string& path_env, std::string* out) {
    if (prog.find('/') != std::string::npos) { *out = prog; return true; }
    std::string path = path_env.empty() ? "/usr/local/bin:/usr/bin:/bin" : path_env;
    size_t start = 0;
    while (start <= path.size()) {
        size_t colon = path.find(':', start);
        std::string dir = path.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (dir.empty()) dir = ".";
        std::string candidate = dir + "/" + prog;
        if (access(candidate.c_str(), X_OK) == 0) { *out = candidate; return true; }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

// A pipe to the child as a waitable io stream (non-blocking: we own it).
static void push_stream_for_fd(MobiusState* state, int fd, bool for_writing, const char* name) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    IoStream* s = new IoStream();
    s->waitable = true;
    s->fd = fd;
    s->nonblocking = true;
    s->name = name;
    s->readable = !for_writing;
    s->writable = for_writing;
    mobius_stack_pushUserdata(state, s, stream_destructor, STREAM_TYPE, sizeof(IoStream));
}

static void set_field(MobiusState* state, int tbl, const char* key) {
    mobius_stack_setTableField(state, tbl, key);
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

struct SpawnResult {
    ChildHandle* child = nullptr;
    int parent_fd[3] = {-1, -1, -1};   // our ends of the pipes
};

// Spawn from args (stack index 0) and options (opts_idx or -1) with the
// given stdio defaults. On failure returns false and fills *err.
static bool do_spawn(MobiusState* state, int opts_idx, const char* fname,
                     StdioKind dflt_in, StdioKind dflt_out, StdioKind dflt_err,
                     SpawnResult* result, std::string* err) {
    // Program and arguments.
    std::vector<std::string> argv;
    bool shell = option_bool(state, opts_idx, "shell", false);
    if (shell) {
        std::string cmd;
        if (!get_string_at(state, 0, &cmd)) { *err = "with shell: true, the command must be a string"; return false; }
        argv = {"/bin/sh", "-c", cmd};
    } else {
        if (!mobius_stack_isArray(state, 0)) {
            *err = "args must be an array of strings (or pass {shell: true} with a command string)";
            return false;
        }
        size_t n = mobius_stack_getArrayLength(state, 0);
        if (n == 0) { *err = "args must not be empty"; return false; }
        for (size_t i = 0; i < n; i++) {
            mobius_stack_getArrayElement(state, 0, i);
            std::string a;
            bool is_str = get_string_at(state, top(state), &a);
            mobius_stack_pop(state, 1);
            if (!is_str) { *err = "args[" + std::to_string(i) + "] must be a string"; return false; }
            argv.push_back(a);
        }
    }
    for (const std::string& a : argv) {
        if (a.find('\0') != std::string::npos) { *err = "arguments must not contain NUL bytes"; return false; }
    }

    // Environment: the parent's, unless env_clear, plus the env table (a
    // nil value removes a variable).
    std::map<std::string, std::string> env;
    if (!option_bool(state, opts_idx, "env_clear", false)) {
        for (char** e = environ; e && *e; e++) {
            const char* eq = strchr(*e, '=');
            if (eq) env[std::string(*e, eq - *e)] = eq + 1;
        }
    }
    push_option(state, opts_idx, "env");
    int env_idx = top(state);
    if (mobius_stack_isTable(state, env_idx)) {
        mobius_stack_getTableKeys(state, env_idx);
        int keys_idx = top(state);
        size_t nk = mobius_stack_getArrayLength(state, keys_idx);
        for (size_t i = 0; i < nk; i++) {
            mobius_stack_getArrayElement(state, keys_idx, i);
            std::string key;
            bool key_ok = get_string_at(state, top(state), &key);
            mobius_stack_pop(state, 1);
            if (!key_ok) { mobius_stack_pop(state, 2); *err = "env keys must be strings"; return false; }
            mobius_stack_getTableField(state, env_idx, key.c_str());
            std::string val;
            int vt = top(state);
            if (mobius_stack_isNil(state, vt)) env.erase(key);
            else if (get_string_at(state, vt, &val)) env[key] = val;
            else { mobius_stack_pop(state, 3); *err = "env['" + key + "'] must be a string or nil"; return false; }
            mobius_stack_pop(state, 1);
        }
        mobius_stack_pop(state, 1);   // keys
    } else if (!mobius_stack_isNil(state, env_idx)) {
        mobius_stack_pop(state, 1);
        *err = "env must be a table";
        return false;
    }
    mobius_stack_pop(state, 1);   // env option

    std::string cwd;
    push_option(state, opts_idx, "cwd");
    bool has_cwd = get_string_at(state, top(state), &cwd);
    bool cwd_bad = !has_cwd && !mobius_stack_isNil(state, top(state));
    mobius_stack_pop(state, 1);
    if (cwd_bad) { *err = "cwd must be a string"; return false; }

    StdioSpec spec[3];
    if (!parse_stdio(state, opts_idx, "stdin", 0, dflt_in, &spec[0], err)) return false;
    if (!parse_stdio(state, opts_idx, "stdout", 1, dflt_out, &spec[1], err)) return false;
    if (!parse_stdio(state, opts_idx, "stderr", 2, dflt_err, &spec[2], err)) return false;

    std::string program;
    auto path_it = env.find("PATH");
    if (!resolve_program(argv[0], path_it == env.end() ? "" : path_it->second, &program)) {
        *err = "cannot run '" + argv[0] + "': not found in PATH";
        return false;
    }

    // Set up the child's descriptors. Files are opened here (not in the
    // child) so a bad path gets its own error message.
    int child_fd[3] = {-1, -1, -1};     // descriptors to dup2 onto 0/1/2
    int to_close[6]; int n_close = 0;    // our temporaries, closed after spawn
    auto fail = [&](const std::string& msg) {
        for (int i = 0; i < n_close; i++) close(to_close[i]);
        for (int i = 0; i < 3; i++) if (result->parent_fd[i] >= 0) { close(result->parent_fd[i]); result->parent_fd[i] = -1; }
        *err = msg;
        return false;
    };
    for (int i = 0; i < 3; i++) {
        const char* names[3] = {"stdin", "stdout", "stderr"};
        switch (spec[i].kind) {
            case StdioKind::inherit:
            case StdioKind::merge:
                break;
            case StdioKind::pipe: {
                int p[2];
                if (pipe2(p, O_CLOEXEC) != 0) return fail(std::string("pipe: ") + strerror(errno));
                // stdin: child reads p[0], we write p[1]; outputs the reverse.
                child_fd[i] = (i == 0) ? p[0] : p[1];
                result->parent_fd[i] = (i == 0) ? p[1] : p[0];
                to_close[n_close++] = child_fd[i];
                break;
            }
            case StdioKind::null_dev: {
                int fd = open("/dev/null", (i == 0 ? O_RDONLY : O_WRONLY) | O_CLOEXEC);
                if (fd < 0) return fail(std::string("/dev/null: ") + strerror(errno));
                child_fd[i] = fd;
                to_close[n_close++] = fd;
                break;
            }
            case StdioKind::file: {
                int flags = (i == 0) ? O_RDONLY
                                     : (O_WRONLY | O_CREAT | (spec[i].append ? O_APPEND : O_TRUNC));
                int fd = open(spec[i].path.c_str(), flags | O_CLOEXEC, 0644);
                if (fd < 0) return fail(std::string(names[i]) + " file " + spec[i].path + ": " + strerror(errno));
                child_fd[i] = fd;
                to_close[n_close++] = fd;
                break;
            }
            case StdioKind::fd:
                child_fd[i] = spec[i].fd;
                break;
        }
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int i = 0; i < 3; i++) {
        if (child_fd[i] >= 0) posix_spawn_file_actions_adddup2(&actions, child_fd[i], i);
    }
    if (spec[2].kind == StdioKind::merge) posix_spawn_file_actions_adddup2(&actions, 1, 2);
    if (has_cwd) posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());

    // The child starts with default signal handling and nothing blocked
    // (the interpreter ignores SIGPIPE, and worker threads may block
    // signals); both would otherwise be inherited.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    std::vector<char*> cargv;
    for (std::string& a : argv) cargv.push_back(&a[0]);
    cargv.push_back(nullptr);
    std::vector<std::string> env_strings;
    for (auto& kv : env) env_strings.push_back(kv.first + "=" + kv.second);
    std::vector<char*> cenv;
    for (std::string& e : env_strings) cenv.push_back(&e[0]);
    cenv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawn(&pid, program.c_str(), &actions, &attr, cargv.data(), cenv.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        std::string where = has_cwd && rc == ENOENT ? " (or cwd '" + cwd + "' does not exist)" : "";
        return fail("cannot run '" + argv[0] + "': " + strerror(rc) + where);
    }
    for (int i = 0; i < n_close; i++) close(to_close[i]);

    ChildHandle* c = new ChildHandle();
    c->pid = pid;
#ifdef SYS_pidfd_open
    c->pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (c->pidfd >= 0) fcntl(c->pidfd, F_SETFD, FD_CLOEXEC);
#endif
    result->child = c;
    (void)fname;
    return true;
}

// Push {handle, pid, stdin, stdout, stderr} for a spawned child.
static void push_spawn_table(MobiusState* state, SpawnResult& r) {
    mobius_stack_pushNewTable(state, 8);
    int tbl = top(state);
    pid_t pid = r.child->pid;
    mobius_stack_pushUserdata(state, r.child, child_destructor, CHILD_TYPE, sizeof(ChildHandle));
    set_field(state, tbl, "__handle");
    mobius_stack_pushInt64(state, (int64_t)pid);
    set_field(state, tbl, "pid");
    const char* keys[3] = {"stdin", "stdout", "stderr"};
    const char* names[3] = {"<child stdin>", "<child stdout>", "<child stderr>"};
    for (int i = 0; i < 3; i++) {
        if (r.parent_fd[i] >= 0) push_stream_for_fd(state, r.parent_fd[i], i == 0, names[i]);
        else mobius_stack_pushNil(state);
        set_field(state, tbl, keys[i]);
    }
    mobius_stack_getGlobal(state, CHILD_METHODS_GLOBAL);
    int methods = top(state);
    if (mobius_stack_isTable(state, methods)) {
        for (const char* name : CHILD_METHODS) {
            mobius_stack_getTableField(state, methods, name);
            set_field(state, tbl, name);
        }
    }
    mobius_stack_pop(state, 1);
}

// __set_child_methods(table): the methods every child gets.
static int set_child_methods(MobiusState* state, int arg_count) {
    if (arg_count != 1 || !mobius_stack_isTable(state, 0))
        return mobius_error(state, "__set_child_methods expects a table");
    mobius_stack_setGlobal(state, CHILD_METHODS_GLOBAL);   // pops the table
    mobius_stack_pushNil(state);
    return 1;
}

// ---------------------------------------------------------------------------
// Feeding stdin and draining stdout/stderr together.
// ---------------------------------------------------------------------------

struct CommunicateResult {
    std::string out, err;
    bool timed_out = false;
    bool cancelled = false;
};

// Take the descriptor out of a pipe stream: it is consumed by communicate.
// A fiber blocked reading the stream is woken (it sees "stream is closed").
// Bytes the stream had already read but not returned go to `pending`.
static int take_stream_fd(IoStream* s, std::string* pending) {
    if (!s) return -1;
    if (s->waitable && s->fd >= 0) {
        s->closing.store(true, std::memory_order_release);
        mobius_io_wake_fd(s->fd);
    }
    std::lock_guard<FiberMutex> lock(s->mu);
    if (s->closed || !s->waitable || s->fd < 0) return -1;
    int fd = s->fd;
    if (pending) pending->append(s->rbuf, s->rpos, std::string::npos);
    s->rbuf.clear();
    s->rpos = 0;
    s->fd = -1;
    s->closed = true;
    return fd;
}

static void communicate(MobiusState* state, ChildHandle* c, int in_fd, int out_fd, int err_fd,
                        const char* input, size_t input_len, int64_t timeout_ms,
                        CommunicateResult* res) {
    int64_t deadline = timeout_ms > 0 ? now_ms() + timeout_ms : -1;
    int64_t kill_at = -1;   // after SIGTERM, when to send SIGKILL
    size_t written = 0;
    for (int fd : {in_fd, out_fd, err_fd})
        if (fd >= 0) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    if (in_fd >= 0 && input_len == 0) { close(in_fd); in_fd = -1; }
    char buf[65536];
    // Every descriptor is non-blocking, so each pass tries all of them and
    // then waits (parked, in a fiber) until one is ready.
    bool try_io = true;
    while (in_fd >= 0 || out_fd >= 0 || err_fd >= 0) {
        if (try_io) {
            if (in_fd >= 0) {
                ssize_t w = write(in_fd, input + written, input_len - written);
                if (w > 0) written += (size_t)w;
                if (w < 0 && errno != EAGAIN && errno != EINTR) written = input_len;   // EPIPE: child stopped reading
                if (written >= input_len) { close(in_fd); in_fd = -1; }
            }
            int* fds[2] = {&out_fd, &err_fd};
            std::string* outs[2] = {&res->out, &res->err};
            for (int i = 0; i < 2; i++) {
                while (*fds[i] >= 0) {
                    ssize_t r = read(*fds[i], buf, sizeof(buf));
                    if (r > 0) { outs[i]->append(buf, (size_t)r); continue; }
                    if (r < 0 && errno == EINTR) continue;
                    if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { close(*fds[i]); *fds[i] = -1; }
                    break;
                }
            }
            if (in_fd < 0 && out_fd < 0 && err_fd < 0) break;
        }

        MobiusIoWait waits[3];
        int nw = 0;
        if (in_fd >= 0)  waits[nw++] = {in_fd, MOBIUS_IO_WRITE};
        if (out_fd >= 0) waits[nw++] = {out_fd, MOBIUS_IO_READ};
        if (err_fd >= 0) waits[nw++] = {err_fd, MOBIUS_IO_READ};
        int64_t wait_ms = -1;
        int64_t now = now_ms();
        if (kill_at >= 0) wait_ms = std::max<int64_t>(0, kill_at - now);
        else if (deadline >= 0) wait_ms = std::max<int64_t>(0, deadline - now);
        int pr = mobius_io_wait(state, waits, nw, wait_ms);
        if (pr == MOBIUS_IO_CANCELLED) { res->cancelled = true; break; }
        if (pr == MOBIUS_IO_ERROR) break;
        try_io = pr >= 0;

        now = now_ms();
        if (deadline >= 0 && now >= deadline && kill_at < 0 && !c->reaped) {
            kill(c->pid, SIGTERM);
            res->timed_out = true;
            kill_at = now + TERM_GRACE_MS;
        }
        if (kill_at >= 0 && now >= kill_at) {
            kill(c->pid, SIGKILL);
            kill_at = now + 1000000;   // once
            // Stop waiting for output: grandchildren may still hold the pipes.
            if (in_fd >= 0) { close(in_fd); in_fd = -1; }
            if (out_fd >= 0) { close(out_fd); out_fd = -1; }
            if (err_fd >= 0) { close(err_fd); err_fd = -1; }
            break;
        }
    }
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    if (err_fd >= 0) close(err_fd);
    if (res->cancelled) return;

    // Outputs are closed; now wait for the exit, still honoring the timeout.
    WaitResult wr;
    while ((wr = wait_until(state, c, kill_at >= 0 ? kill_at : deadline)) != WaitResult::exited) {
        if (wr == WaitResult::cancelled) { res->cancelled = true; return; }
        int64_t now = now_ms();
        if (kill_at < 0) {
            kill(c->pid, SIGTERM);
            res->timed_out = true;
            kill_at = now + TERM_GRACE_MS;
        } else {
            kill_and_reap(state, c);
            break;
        }
    }
}

static ChildHandle* child_from_self(MobiusState* state, int self_idx) {
    if (!mobius_stack_isTable(state, self_idx)) return nullptr;
    mobius_stack_getTableField(state, self_idx, "__handle");
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, top(state), &type_name);
    mobius_stack_pop(state, 1);
    if (!ptr || !type_name || strcmp(type_name, CHILD_TYPE) != 0) return nullptr;
    return static_cast<ChildHandle*>(ptr);
}

static IoStream* child_stream(MobiusState* state, int self_idx, const char* key) {
    mobius_stack_getTableField(state, self_idx, key);
    IoStream* s = get_stream(state, top(state));
    mobius_stack_pop(state, 1);
    return s;
}

// Results table: {exit_code, stdout, stderr, timed_out}; stdout/stderr are
// strings (or buffers with binary) when captured, otherwise nil.
static void push_result(MobiusState* state, ChildHandle* c, const CommunicateResult& r,
                        bool captured_out, bool captured_err, bool binary) {
    mobius_stack_pushNewTable(state, 6);
    int tbl = top(state);
    mobius_stack_pushInt64(state, exit_code_of(c->status));
    set_field(state, tbl, "exit_code");
    mobius_stack_pushBool(state, r.timed_out);
    set_field(state, tbl, "timed_out");
    const std::string* data[2] = {&r.out, &r.err};
    bool captured[2] = {captured_out, captured_err};
    const char* keys[2] = {"stdout", "stderr"};
    for (int i = 0; i < 2; i++) {
        if (!captured[i]) mobius_stack_pushNil(state);
        else if (binary) mobius_stack_pushBufferCopy(state, data[i]->data(), data[i]->size());
        else mobius_stack_pushStringLength(state, data[i]->data(), data[i]->size());
        set_field(state, tbl, keys[i]);
    }
}

static bool input_bytes(MobiusState* state, int idx, std::string* out) {
    if (mobius_stack_isNil(state, idx)) return true;
    if (mobius_stack_isBuffer(state, idx)) {
        size_t len = 0;
        const char* d = static_cast<const char*>(mobius_stack_getBufferData(state, idx, &len));
        out->assign(d, len);
        return true;
    }
    return get_string_at(state, idx, out);
}

static std::string describe_args(MobiusState* state) {
    std::string cmd;
    if (get_string_at(state, 0, &cmd)) return cmd;
    if (mobius_stack_isArray(state, 0) && mobius_stack_getArrayLength(state, 0) > 0) {
        mobius_stack_getArrayElement(state, 0, 0);
        get_string_at(state, top(state), &cmd);
        mobius_stack_pop(state, 1);
    }
    return cmd;
}

// ---------------------------------------------------------------------------
// process.run(args [, options])
// ---------------------------------------------------------------------------

static int process_run(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "process.run expects (args [, options])");
    int opts_idx = -1;
    if (arg_count == 2) {
        if (!mobius_stack_isTable(state, 1) && !mobius_stack_isNil(state, 1))
            return mobius_error(state, "process.run: options must be a table");
        if (mobius_stack_isTable(state, 1)) opts_idx = 1;
    }
    std::string err;
    int64_t timeout_ms = 0;
    if (!option_int(state, opts_idx, "timeout_ms", &timeout_ms, &err))
        return mobius_error(state, ("process.run: " + err).c_str());
    bool binary = option_bool(state, opts_idx, "binary", false);
    bool check = option_bool(state, opts_idx, "check", false);

    std::string input;
    push_option(state, opts_idx, "input");
    bool has_input = !mobius_stack_isNil(state, top(state));
    bool input_ok = input_bytes(state, top(state), &input);
    mobius_stack_pop(state, 1);
    if (!input_ok) return mobius_error(state, "process.run: input must be a string or buffer");

    SpawnResult sr;
    if (!do_spawn(state, opts_idx, "process.run",
                  has_input ? StdioKind::pipe : StdioKind::inherit,
                  StdioKind::pipe, StdioKind::pipe, &sr, &err)) {
        return mobius_error(state, ("process.run: " + err).c_str());
    }
    if (has_input && sr.parent_fd[0] < 0) {
        // stdin was redirected elsewhere explicitly; the input has nowhere to go.
        for (int i = 0; i < 3; i++) if (sr.parent_fd[i] >= 0) close(sr.parent_fd[i]);
        kill_and_reap(state, sr.child);
        child_destructor(sr.child);
        return mobius_error(state, "process.run: input was given but stdin is not a pipe");
    }
    if (sr.parent_fd[0] >= 0 && !has_input) {   // stdin: "pipe" with no input means EOF
        close(sr.parent_fd[0]);
        sr.parent_fd[0] = -1;
    }
    bool captured_out = sr.parent_fd[1] >= 0, captured_err = sr.parent_fd[2] >= 0;

    CommunicateResult res;
    communicate(state, sr.child, sr.parent_fd[0], sr.parent_fd[1], sr.parent_fd[2],
                input.data(), input.size(), timeout_ms, &res);
    ChildHandle* c = sr.child;
    if (res.cancelled) {
        kill_and_reap(state, c);   // nothing else holds this child
        child_destructor(c);
        return mobius_error(state, "CancellationError: fiber was cancelled");
    }
    int64_t code = exit_code_of(c->status);

    if (check && (code != 0 || res.timed_out)) {
        std::string what = describe_args(state);
        std::string msg = "process.run: '" + what + "' " +
            (res.timed_out ? std::string("timed out") : "exited with status " + std::to_string(code));
        child_destructor(c);
        return mobius_error(state, msg.c_str());
    }
    mobius_stack_pop(state, arg_count);
    push_result(state, c, res, captured_out, captured_err, binary);
    child_destructor(c);
    return 1;
}

// ---------------------------------------------------------------------------
// process.start(args [, options]) and the child methods
// ---------------------------------------------------------------------------

static int process_start(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "process.start expects (args [, options])");
    int opts_idx = -1;
    if (arg_count == 2) {
        if (!mobius_stack_isTable(state, 1) && !mobius_stack_isNil(state, 1))
            return mobius_error(state, "process.start: options must be a table");
        if (mobius_stack_isTable(state, 1)) opts_idx = 1;
    }
    std::string err;
    SpawnResult sr;
    if (!do_spawn(state, opts_idx, "process.start", StdioKind::inherit, StdioKind::inherit,
                  StdioKind::inherit, &sr, &err)) {
        return mobius_error(state, ("process.start: " + err).c_str());
    }
    mobius_stack_pop(state, arg_count);
    push_spawn_table(state, sr);   // process.mob adds the methods
    return 1;
}

// child:wait([timeout_ms]): the exit code, or nil if the timeout passed
// first.
static int child_wait(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "child:wait expects ([timeout_ms])");
    ChildHandle* c = child_from_self(state, 0);
    if (!c) return mobius_error(state, "child:wait: self is not a child process");
    int64_t timeout_ms = -1;
    if (arg_count == 2 && !mobius_stack_isNil(state, 1)) {
        if (!mobius_stack_isInteger(state, 1)) return mobius_error(state, "child:wait: timeout_ms must be an integer");
        timeout_ms = mobius_stack_getInt64(state, 1);
    }
    std::lock_guard<FiberMutex> lock(c->mu);
    WaitResult wr = wait_until(state, c, timeout_ms >= 0 ? now_ms() + timeout_ms : -1);
    if (wr == WaitResult::cancelled) return mobius_error(state, "CancellationError: fiber was cancelled");
    mobius_stack_pop(state, arg_count);
    if (wr == WaitResult::exited) mobius_stack_pushInt64(state, exit_code_of(c->status));
    else mobius_stack_pushNil(state);
    return 1;
}

// child:poll(): the exit code if the child has finished, otherwise nil.
static int child_poll(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "child:poll expects no arguments");
    ChildHandle* c = child_from_self(state, 0);
    if (!c) return mobius_error(state, "child:poll: self is not a child process");
    std::lock_guard<FiberMutex> lock(c->mu);
    bool done = try_reap(c);
    mobius_stack_pop(state, arg_count);
    if (done) mobius_stack_pushInt64(state, exit_code_of(c->status));
    else mobius_stack_pushNil(state);
    return 1;
}

// child:kill([signal]): "term" (default), "kill", "int" or "hup". Returns
// false if the child had already exited.
static int child_kill(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "child:kill expects ([signal])");
    ChildHandle* c = child_from_self(state, 0);
    if (!c) return mobius_error(state, "child:kill: self is not a child process");
    int sig = SIGTERM;
    if (arg_count == 2 && !mobius_stack_isNil(state, 1)) {
        std::string name;
        if (!get_string_at(state, 1, &name)) return mobius_error(state, "child:kill: signal must be a string");
        if (name == "term") sig = SIGTERM;
        else if (name == "kill") sig = SIGKILL;
        else if (name == "int") sig = SIGINT;
        else if (name == "hup") sig = SIGHUP;
        else return mobius_error(state, ("child:kill: unknown signal '" + name + "' (use term, kill, int or hup)").c_str());
    }
    std::lock_guard<FiberMutex> lock(c->mu);
    bool sent = !try_reap(c) && kill(c->pid, sig) == 0;
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, sent);
    return 1;
}

// child:communicate([input [, timeout_ms]]): write input to stdin (if
// piped) and close it, read stdout/stderr (if piped) to the end, wait for
// the exit. Returns {exit_code, stdout, stderr, timed_out}. The child's
// pipe streams are consumed (closed) by this call.
static int child_communicate(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 3) return mobius_error(state, "child:communicate expects ([input [, timeout_ms]])");
    ChildHandle* c = child_from_self(state, 0);
    if (!c) return mobius_error(state, "child:communicate: self is not a child process");
    std::string input;
    if (arg_count >= 2 && !input_bytes(state, 1, &input))
        return mobius_error(state, "child:communicate: input must be a string or buffer");
    bool has_input = arg_count >= 2 && !mobius_stack_isNil(state, 1);
    int64_t timeout_ms = 0;
    if (arg_count == 3 && !mobius_stack_isNil(state, 2)) {
        if (!mobius_stack_isInteger(state, 2)) return mobius_error(state, "child:communicate: timeout_ms must be an integer");
        timeout_ms = mobius_stack_getInt64(state, 2);
    }
    IoStream* in = child_stream(state, 0, "stdin");
    IoStream* out = child_stream(state, 0, "stdout");
    IoStream* errs = child_stream(state, 0, "stderr");
    if (has_input && (!in || in->closed)) return mobius_error(state, "child:communicate: input was given but stdin is not an open pipe");

    std::lock_guard<FiberMutex> lock(c->mu);
    CommunicateResult res;
    int in_fd = take_stream_fd(in, nullptr);
    int out_fd = take_stream_fd(out, &res.out);
    int err_fd = take_stream_fd(errs, &res.err);
    communicate(state, c, in_fd, out_fd, err_fd, input.data(), input.size(), timeout_ms, &res);
    if (res.cancelled) return mobius_error(state, "CancellationError: fiber was cancelled");
    mobius_stack_pop(state, arg_count);
    push_result(state, c, res, out_fd >= 0, err_fd >= 0, false);
    return 1;
}

static int process_init(MobiusState* state) {
    (void)state;
    // Writing to a pipe whose reader has exited raises SIGPIPE, which would
    // kill the interpreter; ignore it so the write fails with EPIPE instead.
    // Children get the default disposition back (see do_spawn).
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

} // namespace

static MobiusPluginFunction process_functions[] = {
    {"run", process_run, SIZE_MAX, MOBIUS_VAL_TABLE, "Run a program to completion: process.run(args [, options])"},
    {"start", process_start, SIZE_MAX, MOBIUS_VAL_TABLE, "Start a program: process.start(args [, options])"},
    {"__set_child_methods", set_child_methods, 1, MOBIUS_VAL_NIL, "Internal: register child methods"},
    {"__wait", child_wait, SIZE_MAX, MOBIUS_VAL_UNKNOWN, "Internal child method"},
    {"__poll", child_poll, 1, MOBIUS_VAL_UNKNOWN, "Internal child method"},
    {"__kill", child_kill, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal child method"},
    {"__communicate", child_communicate, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal child method"},
};

static MobiusPlugin process_plugin = {
    .metadata = {
        .name = "process",
        .version = "1.0.0",
        .description = "Run programs with control over their streams",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = process_functions,
    .function_count = sizeof(process_functions) / sizeof(process_functions[0]),
    .init_plugin = process_init,
    .cleanup_plugin = nullptr,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &process_plugin;
}
