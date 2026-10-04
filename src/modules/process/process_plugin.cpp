// process: run programs directly (no shell unless asked), with control over
// their standard streams, environment and working directory.
//
// Starting, waiting for and signaling children is in process_platform.h;
// pipes to a child are ordinary `io` streams (see io_stream.h) and are read
// and written through io_platform.h. run() and communicate() feed stdin
// and drain stdout and stderr in one loop, so a child that fills one pipe
// while we are writing the other can't deadlock.
//
// Every wait (pipe readiness, the child's exit) parks a fiber: its worker
// thread runs other fibers. fiber.cancel interrupts the wait with a
// CancellationError; a child started by run() is then killed (nothing else
// could reach it), one from start() is left running.

#include <mobius/mobius_plugin.h>

#include "../io/io_stream.h"
#include "process_platform.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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
    ProcChild* proc = nullptr;
};

static void child_destructor(void* ptr) {
    ChildHandle* c = static_cast<ChildHandle*>(ptr);
    if (!c) return;
    proc_release(c->proc);
    delete c;
}

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static bool try_reap(ChildHandle* c) { return proc_try_reap(c->proc); }

static int64_t exit_code_of(const ChildHandle* c) { return proc_exit_code(c->proc); }

enum class WaitResult { exited, timeout, cancelled };

// Wait until exit or until deadline_ms (<0: no deadline). With
// `cancellable` false the wait ignores fiber.cancel (used to finish
// killing a child).
static WaitResult wait_until(MobiusState* state, ChildHandle* c, int64_t deadline_ms,
                             bool cancellable = true) {
    while (!try_reap(c)) {
        int64_t left = -1;
        if (deadline_ms >= 0) {
            left = deadline_ms - now_ms();
            if (left <= 0) return WaitResult::timeout;
        }
        int rc = proc_wait_exit(state, c->proc, left);
        if (rc == MOBIUS_IO_CANCELLED && cancellable) return WaitResult::cancelled;
    }
    return WaitResult::exited;
}

// Kill a child nobody can reach any more and reap it.
static void kill_and_reap(MobiusState* state, ChildHandle* c) {
    if (!try_reap(c)) proc_signal(c->proc, ProcSignal::kill);
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
            spec->kind = StdioKind::handle;
            spec->handle = s->handle;
        } else {
            fflush(s->fp);   // our buffered output goes first
            spec->kind = StdioKind::handle;
            spec->handle = io_platform_file_handle(s->fp);
        }
    } else {
        ok = false;
        *err = std::string(key) + ": expected a string, {file: path} or an io stream";
    }
    mobius_stack_pop(state, 1);
    return ok;
}

// A pipe to the child as a waitable io stream (we own it).
static void push_pipe_stream(MobiusState* state, intptr_t handle, bool for_writing, const char* name) {
    IoStream* s = new IoStream();
    io_platform_attach_pipe(s, handle);
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
    intptr_t parent[3] = {-1, -1, -1};   // our ends of the pipes
};

// Spawn from args (stack index 0) and options (opts_idx or -1) with the
// given stdio defaults. On failure returns false and fills *err.
static bool do_spawn(MobiusState* state, int opts_idx,
                     StdioKind dflt_in, StdioKind dflt_out, StdioKind dflt_err,
                     SpawnResult* result, std::string* err) {
    SpawnRequest req;
    // Program and arguments.
    std::vector<std::string>& argv = req.argv;
    req.shell = option_bool(state, opts_idx, "shell", false);
    if (req.shell) {
        std::string cmd;
        if (!get_string_at(state, 0, &cmd)) { *err = "with shell: true, the command must be a string"; return false; }
        argv = {cmd};
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
    std::map<std::string, std::string>& env = req.env;
    if (!option_bool(state, opts_idx, "env_clear", false)) proc_environment(&env);
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

    push_option(state, opts_idx, "cwd");
    req.has_cwd = get_string_at(state, top(state), &req.cwd);
    bool cwd_bad = !req.has_cwd && !mobius_stack_isNil(state, top(state));
    mobius_stack_pop(state, 1);
    if (cwd_bad) { *err = "cwd must be a string"; return false; }

    if (!parse_stdio(state, opts_idx, "stdin", 0, dflt_in, &req.stdio[0], err)) return false;
    if (!parse_stdio(state, opts_idx, "stdout", 1, dflt_out, &req.stdio[1], err)) return false;
    if (!parse_stdio(state, opts_idx, "stderr", 2, dflt_err, &req.stdio[2], err)) return false;

    ProcChild* proc = proc_spawn(req, result->parent, err);
    if (!proc) return false;
    result->child = new ChildHandle();
    result->child->proc = proc;
    return true;
}

// Push {handle, pid, stdin, stdout, stderr} for a spawned child.
static void push_spawn_table(MobiusState* state, SpawnResult& r) {
    mobius_stack_pushNewTable(state, 8);
    int tbl = top(state);
    int64_t pid = proc_pid(r.child->proc);
    mobius_stack_pushUserdata(state, r.child, child_destructor, CHILD_TYPE, sizeof(ChildHandle));
    set_field(state, tbl, "__handle");
    mobius_stack_pushInt64(state, pid);
    set_field(state, tbl, "pid");
    const char* keys[3] = {"stdin", "stdout", "stderr"};
    const char* names[3] = {"<child stdin>", "<child stdout>", "<child stderr>"};
    for (int i = 0; i < 3; i++) {
        if (r.parent[i] != -1) push_pipe_stream(state, r.parent[i], i == 0, names[i]);
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
static int set_child_methods(MobiusState* state, int arg_count, void* /*userdata*/) {
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

// Take the handle out of a pipe stream: it is consumed by communicate.
// A fiber blocked reading the stream is woken (it sees "stream is closed").
// Bytes the stream had already read but not returned go to `pending`.
static intptr_t take_stream_handle(IoStream* s, std::string* pending) {
    if (!s) return -1;
    if (s->waitable && s->handle != -1) {
        s->closing.store(true, std::memory_order_release);
        mobius_io_wake_fd(s->handle);
    }
    std::lock_guard<FiberMutex> lock(s->mu);
    if (s->closed || !s->waitable || s->handle == -1) return -1;
    intptr_t h = s->handle;
    if (pending) pending->append(s->rbuf, s->rpos, std::string::npos);
    s->rbuf.clear();
    s->rpos = 0;
    s->handle = -1;
    s->closed = true;
    return h;
}

static void close_handle(intptr_t* h) {
    if (*h != -1) { io_platform_close(*h); *h = -1; }
}

static void communicate(MobiusState* state, ChildHandle* c, intptr_t in_h, intptr_t out_h, intptr_t err_h,
                        const char* input, size_t input_len, int64_t timeout_ms,
                        CommunicateResult* res) {
    int64_t deadline = timeout_ms > 0 ? now_ms() + timeout_ms : -1;
    int64_t kill_at = -1;   // after the terminate signal, when to kill
    size_t written = 0;
    for (intptr_t h : {in_h, out_h, err_h})
        if (h != -1) io_platform_set_nonblocking(h);
    if (in_h != -1 && input_len == 0) close_handle(&in_h);
    char buf[65536];
    // Every handle is non-blocking, so each pass tries all of them and then
    // waits (parked, in a fiber) until one is ready.
    bool try_io = true;
    while (in_h != -1 || out_h != -1 || err_h != -1) {
        if (try_io) {
            if (in_h != -1) {
                int e = 0;
                long long w = io_platform_write(in_h, input + written, input_len - written, &e);
                if (w > 0) written += (size_t)w;
                if (w == IO_PLATFORM_ERROR) written = input_len;   // EPIPE: child stopped reading
                if (written >= input_len) close_handle(&in_h);
            }
            intptr_t* hs[2] = {&out_h, &err_h};
            std::string* outs[2] = {&res->out, &res->err};
            for (int i = 0; i < 2; i++) {
                while (*hs[i] != -1) {
                    int e = 0;
                    long long n = io_platform_read(*hs[i], buf, sizeof(buf), &e);
                    if (n > 0) { outs[i]->append(buf, (size_t)n); continue; }
                    if (n != IO_PLATFORM_WOULD_BLOCK) close_handle(hs[i]);   // end, or an error
                    break;
                }
            }
            if (in_h == -1 && out_h == -1 && err_h == -1) break;
        }

        MobiusIoWait waits[3];
        int nw = 0;
        if (in_h != -1)  waits[nw++] = {in_h, MOBIUS_IO_WRITE};
        if (out_h != -1) waits[nw++] = {out_h, MOBIUS_IO_READ};
        if (err_h != -1) waits[nw++] = {err_h, MOBIUS_IO_READ};
        int64_t wait_ms = -1;
        int64_t now = now_ms();
        if (kill_at >= 0) wait_ms = std::max<int64_t>(0, kill_at - now);
        else if (deadline >= 0) wait_ms = std::max<int64_t>(0, deadline - now);
        int pr = io_platform_wait(state, waits, nw, wait_ms);
        if (pr == MOBIUS_IO_CANCELLED) { res->cancelled = true; break; }
        if (pr == MOBIUS_IO_ERROR) break;
        try_io = pr >= 0;

        now = now_ms();
        if (deadline >= 0 && now >= deadline && kill_at < 0 && !try_reap(c)) {
            proc_signal(c->proc, ProcSignal::term);
            res->timed_out = true;
            kill_at = now + TERM_GRACE_MS;
        }
        if (kill_at >= 0 && now >= kill_at) {
            proc_signal(c->proc, ProcSignal::kill);
            kill_at = now + 1000000;   // once
            // Stop waiting for output: grandchildren may still hold the pipes.
            close_handle(&in_h);
            close_handle(&out_h);
            close_handle(&err_h);
            break;
        }
    }
    close_handle(&in_h);
    close_handle(&out_h);
    close_handle(&err_h);
    if (res->cancelled) return;

    // Outputs are closed; now wait for the exit, still honoring the timeout.
    WaitResult wr;
    while ((wr = wait_until(state, c, kill_at >= 0 ? kill_at : deadline)) != WaitResult::exited) {
        if (wr == WaitResult::cancelled) { res->cancelled = true; return; }
        int64_t now = now_ms();
        if (kill_at < 0) {
            proc_signal(c->proc, ProcSignal::term);
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
    mobius_stack_pushInt64(state, exit_code_of(c));
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

static int process_run(MobiusState* state, int arg_count, void* /*userdata*/) {
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
    if (!do_spawn(state, opts_idx,
                  has_input ? StdioKind::pipe : StdioKind::inherit,
                  StdioKind::pipe, StdioKind::pipe, &sr, &err)) {
        return mobius_error(state, ("process.run: " + err).c_str());
    }
    if (has_input && sr.parent[0] == -1) {
        // stdin was redirected elsewhere explicitly; the input has nowhere to go.
        for (int i = 0; i < 3; i++) close_handle(&sr.parent[i]);
        kill_and_reap(state, sr.child);
        child_destructor(sr.child);
        return mobius_error(state, "process.run: input was given but stdin is not a pipe");
    }
    if (sr.parent[0] != -1 && !has_input) close_handle(&sr.parent[0]);   // stdin: "pipe" with no input means EOF
    bool captured_out = sr.parent[1] != -1, captured_err = sr.parent[2] != -1;

    CommunicateResult res;
    communicate(state, sr.child, sr.parent[0], sr.parent[1], sr.parent[2],
                input.data(), input.size(), timeout_ms, &res);
    ChildHandle* c = sr.child;
    if (res.cancelled) {
        kill_and_reap(state, c);   // nothing else holds this child
        child_destructor(c);
        return mobius_error(state, "CancellationError: fiber was cancelled");
    }
    int64_t code = exit_code_of(c);

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

static int process_start(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "process.start expects (args [, options])");
    int opts_idx = -1;
    if (arg_count == 2) {
        if (!mobius_stack_isTable(state, 1) && !mobius_stack_isNil(state, 1))
            return mobius_error(state, "process.start: options must be a table");
        if (mobius_stack_isTable(state, 1)) opts_idx = 1;
    }
    std::string err;
    SpawnResult sr;
    if (!do_spawn(state, opts_idx, StdioKind::inherit, StdioKind::inherit,
                  StdioKind::inherit, &sr, &err)) {
        return mobius_error(state, ("process.start: " + err).c_str());
    }
    mobius_stack_pop(state, arg_count);
    push_spawn_table(state, sr);   // process.mob adds the methods
    return 1;
}

// child:wait([timeout_ms]): the exit code, or nil if the timeout passed
// first.
static int child_wait(MobiusState* state, int arg_count, void* /*userdata*/) {
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
    if (wr == WaitResult::exited) mobius_stack_pushInt64(state, exit_code_of(c));
    else mobius_stack_pushNil(state);
    return 1;
}

// child:poll(): the exit code if the child has finished, otherwise nil.
static int child_poll(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1) return mobius_error(state, "child:poll expects no arguments");
    ChildHandle* c = child_from_self(state, 0);
    if (!c) return mobius_error(state, "child:poll: self is not a child process");
    std::lock_guard<FiberMutex> lock(c->mu);
    bool done = try_reap(c);
    mobius_stack_pop(state, arg_count);
    if (done) mobius_stack_pushInt64(state, exit_code_of(c));
    else mobius_stack_pushNil(state);
    return 1;
}

// child:kill([signal]): "term" (default), "kill", "int" or "hup". Returns
// false if the child had already exited.
static int child_kill(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "child:kill expects ([signal])");
    ChildHandle* c = child_from_self(state, 0);
    if (!c) return mobius_error(state, "child:kill: self is not a child process");
    ProcSignal sig = ProcSignal::term;
    if (arg_count == 2 && !mobius_stack_isNil(state, 1)) {
        std::string name;
        if (!get_string_at(state, 1, &name)) return mobius_error(state, "child:kill: signal must be a string");
        if (name == "term") sig = ProcSignal::term;
        else if (name == "kill") sig = ProcSignal::kill;
        else if (name == "int") sig = ProcSignal::interrupt;
        else if (name == "hup") sig = ProcSignal::hangup;
        else return mobius_error(state, ("child:kill: unknown signal '" + name + "' (use term, kill, int or hup)").c_str());
    }
    std::lock_guard<FiberMutex> lock(c->mu);
    bool sent = !try_reap(c) && proc_signal(c->proc, sig);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, sent);
    return 1;
}

// child:communicate([input [, timeout_ms]]): write input to stdin (if
// piped) and close it, read stdout/stderr (if piped) to the end, wait for
// the exit. Returns {exit_code, stdout, stderr, timed_out}. The child's
// pipe streams are consumed (closed) by this call.
static int child_communicate(MobiusState* state, int arg_count, void* /*userdata*/) {
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
    intptr_t in_h = take_stream_handle(in, nullptr);
    intptr_t out_h = take_stream_handle(out, &res.out);
    intptr_t err_h = take_stream_handle(errs, &res.err);
    bool captured_out = out_h != -1, captured_err = err_h != -1;
    communicate(state, c, in_h, out_h, err_h, input.data(), input.size(), timeout_ms, &res);
    if (res.cancelled) return mobius_error(state, "CancellationError: fiber was cancelled");
    mobius_stack_pop(state, arg_count);
    push_result(state, c, res, captured_out, captured_err, false);
    return 1;
}

static int process_init(MobiusState* state) {
    (void)state;
    proc_init();
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
    {
        "process" /* name */,
        "1.0.0" /* version */,
        "Run programs with control over their streams" /* description */,
        "Mobius Team" /* author */,
        MOBIUS_PLUGIN_API_VERSION /* api_version */,
        "MIT" /* license */,
        nullptr /* depends_on */,
        0 /* depends_on_count */
    },
    process_functions /* functions */,
    sizeof(process_functions) / sizeof(process_functions[0]) /* function_count */,
    process_init /* init_plugin */,
    nullptr /* cleanup_plugin */,
    nullptr /* post_init */
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &process_plugin;
}
