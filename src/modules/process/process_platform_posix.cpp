// Linux and macOS implementation of process_platform.h, on posix_spawn.
// What differs between them is in process_platform_posix.h.

#include "process_platform.h"
#include "process_platform_posix.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

struct ProcChild {
    pid_t pid = -1;
    int exit_fd = -1;    // readable once the child exits; -1 if none
    bool reaped = false;
    int status = 0;
    int pause_ms = 1;    // polling interval without exit_fd
};

namespace {

void mark_reaped(ProcChild* c, int status) {
    c->reaped = true;
    c->status = status;
    if (c->exit_fd >= 0) { close(c->exit_fd); c->exit_fd = -1; }   // no longer needed
}

// A program name without a '/' is looked up in the PATH the child will see.
bool resolve_program(const std::string& prog, const std::string& path_env, std::string* out) {
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

} // namespace

ProcChild* proc_spawn(const SpawnRequest& req, intptr_t parent[3], std::string* err) {
    for (int i = 0; i < 3; i++) parent[i] = -1;
    std::vector<std::string> argv = req.argv;
    if (req.shell) argv = {"/bin/sh", "-c", req.argv[0]};

    std::string program;
    auto path_it = req.env.find("PATH");
    if (!resolve_program(argv[0], path_it == req.env.end() ? "" : path_it->second, &program)) {
        *err = "cannot run '" + argv[0] + "': not found in PATH";
        return nullptr;
    }

    // Set up the child's descriptors. Files are opened here (not in the
    // child) so a bad path gets its own error message.
    int child_fd[3] = {-1, -1, -1};     // descriptors to dup2 onto 0/1/2
    int to_close[6]; int n_close = 0;    // our temporaries, closed after spawn
    auto fail = [&](const std::string& msg) -> ProcChild* {
        for (int i = 0; i < n_close; i++) close(to_close[i]);
        for (int i = 0; i < 3; i++) if (parent[i] >= 0) { close((int)parent[i]); parent[i] = -1; }
        *err = msg;
        return nullptr;
    };
    const char* names[3] = {"stdin", "stdout", "stderr"};
    for (int i = 0; i < 3; i++) {
        const StdioSpec& spec = req.stdio[i];
        switch (spec.kind) {
            case StdioKind::inherit:
            case StdioKind::merge:
                break;
            case StdioKind::pipe: {
                int p[2];
                if (posix_cloexec_pipe(p) != 0) return fail(std::string("pipe: ") + strerror(errno));
                // stdin: child reads p[0], we write p[1]; outputs the reverse.
                child_fd[i] = (i == 0) ? p[0] : p[1];
                parent[i] = (i == 0) ? p[1] : p[0];
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
                                     : (O_WRONLY | O_CREAT | (spec.append ? O_APPEND : O_TRUNC));
                int fd = open(spec.path.c_str(), flags | O_CLOEXEC, 0644);
                if (fd < 0) return fail(std::string(names[i]) + " file " + spec.path + ": " + strerror(errno));
                child_fd[i] = fd;
                to_close[n_close++] = fd;
                break;
            }
            case StdioKind::handle:
                child_fd[i] = (int)spec.handle;
                break;
        }
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int i = 0; i < 3; i++) {
        if (child_fd[i] >= 0) posix_spawn_file_actions_adddup2(&actions, child_fd[i], i);
    }
    if (req.stdio[2].kind == StdioKind::merge) posix_spawn_file_actions_adddup2(&actions, 1, 2);
    if (req.has_cwd) posix_spawn_file_actions_addchdir_np(&actions, req.cwd.c_str());

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
    for (auto& kv : req.env) env_strings.push_back(kv.first + "=" + kv.second);
    std::vector<char*> cenv;
    for (std::string& e : env_strings) cenv.push_back(&e[0]);
    cenv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawn(&pid, program.c_str(), &actions, &attr, cargv.data(), cenv.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        std::string where = req.has_cwd && rc == ENOENT ? " (or cwd '" + req.cwd + "' does not exist)" : "";
        return fail("cannot run '" + argv[0] + "': " + strerror(rc) + where);
    }
    for (int i = 0; i < n_close; i++) close(to_close[i]);

    ProcChild* c = new ProcChild();
    c->pid = pid;
    c->exit_fd = posix_exit_fd(pid);
    return c;
}

void proc_environment(std::map<std::string, std::string>* env) {
    for (char** e = posix_environ(); e && *e; e++) {
        const char* eq = strchr(*e, '=');
        if (eq) (*env)[std::string(*e, eq - *e)] = eq + 1;
    }
}

int64_t proc_pid(const ProcChild* child) { return (int64_t)child->pid; }

bool proc_try_reap(ProcChild* c) {
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

int64_t proc_exit_code(const ProcChild* c) {
    if (WIFEXITED(c->status)) return WEXITSTATUS(c->status);
    if (WIFSIGNALED(c->status)) return -(int64_t)WTERMSIG(c->status);
    return -1;
}

int proc_wait_exit(MobiusState* state, ProcChild* c, int64_t timeout_ms) {
    if (c->exit_fd >= 0) {
        MobiusIoWait w = {c->exit_fd, MOBIUS_IO_READ};
        return mobius_io_wait(state, &w, 1, timeout_ms);
    }
    // No exit descriptor: sleep in short, growing steps.
    int64_t step = (timeout_ms >= 0 && timeout_ms < c->pause_ms) ? timeout_ms : c->pause_ms;
    if (c->pause_ms < 20) c->pause_ms *= 2;
    return mobius_io_wait(state, nullptr, 0, step);
}

bool proc_signal(ProcChild* c, ProcSignal sig) {
    int s = SIGTERM;
    switch (sig) {
        case ProcSignal::term:      s = SIGTERM; break;
        case ProcSignal::kill:      s = SIGKILL; break;
        case ProcSignal::interrupt: s = SIGINT; break;
        case ProcSignal::hangup:    s = SIGHUP; break;
    }
    return kill(c->pid, s) == 0;
}

void proc_release(ProcChild* c) {
    if (!c) return;
    // Reap if it has already finished; a child still running is left to
    // run (it is not killed because the script dropped its handle).
    if (!c->reaped && c->pid > 0) {
        int st = 0;
        if (waitpid(c->pid, &st, WNOHANG) == c->pid) c->reaped = true;
    }
    if (c->exit_fd >= 0) close(c->exit_fd);
    delete c;
}

void proc_init() {
    // Writing to a pipe whose reader has exited raises SIGPIPE, which would
    // kill the interpreter; ignore it so the write fails with EPIPE instead.
    // Children get the default disposition back (see proc_spawn).
    signal(SIGPIPE, SIG_IGN);
}
