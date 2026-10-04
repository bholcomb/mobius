// Windows implementation of process_platform.h, on CreateProcessW.
//
// Arguments are quoted so the child's C runtime (CommandLineToArgvW rules)
// splits them back exactly. The child inherits only its three standard
// handles (PROC_THREAD_ATTRIBUTE_HANDLE_LIST). Windows has no signals:
// every ProcSignal terminates the child (exit code 1). Exits are found by
// polling, with parked sleeps in between.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "process_platform.h"

#include <algorithm>
#include <cwctype>

struct ProcChild {
    HANDLE process = nullptr;
    DWORD pid = 0;
    bool exited = false;
    DWORD exit_code = 0;
};

namespace {

// How long a wait for an exit sleeps before checking again.
const int64_t EXIT_POLL_MS = 5;

// What a terminated child exits with.
const UINT TERMINATED_EXIT_CODE = 1;

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

std::string error_text(DWORD code) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::string text = msg ? narrow(msg) : "error " + std::to_string(code);
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) text.pop_back();
    return text;
}

std::wstring upper(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towupper(c);
    return s;
}

// A variable from the child's environment; names are case-insensitive.
std::string env_get(const std::map<std::string, std::string>& env, const std::string& name) {
    std::wstring want = upper(widen(name));
    for (const auto& kv : env)
        if (upper(widen(kv.first)) == want) return kv.second;
    return "";
}

bool is_file(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// The program's file: as given when it names a directory, otherwise looked
// up in the child's PATH. Without an extension, PATHEXT's are tried.
bool resolve_program(const std::string& prog, const std::map<std::string, std::string>& env, std::wstring* out) {
    std::wstring name = widen(prog);
    std::vector<std::wstring> exts = {L""};
    if (name.find(L'.', name.find_last_of(L"\\/") == std::wstring::npos ? 0 : name.find_last_of(L"\\/")) == std::wstring::npos) {
        std::string pathext = env_get(env, "PATHEXT");
        std::wstring list = widen(pathext.empty() ? ".COM;.EXE;.BAT;.CMD" : pathext);
        exts.clear();
        size_t start = 0;
        while (start <= list.size()) {
            size_t semi = list.find(L';', start);
            std::wstring ext = list.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
            if (!ext.empty()) exts.push_back(ext);
            if (semi == std::wstring::npos) break;
            start = semi + 1;
        }
    }
    auto try_base = [&](const std::wstring& base) {
        for (const std::wstring& ext : exts) {
            if (is_file(base + ext)) { *out = base + ext; return true; }
        }
        return false;
    };
    if (name.find_first_of(L"\\/:") != std::wstring::npos) return try_base(name);
    std::wstring path = widen(env_get(env, "PATH"));
    size_t start = 0;
    while (start <= path.size()) {
        size_t semi = path.find(L';', start);
        std::wstring dir = path.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
        if (!dir.empty()) {
            if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
            if (try_base(dir + name)) return true;
        }
        if (semi == std::wstring::npos) break;
        start = semi + 1;
    }
    return false;
}

// Quote one argument for CommandLineToArgvW / the C runtime.
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (size_t i = 0; ; i++) {
        size_t backslashes = 0;
        while (i < a.size() && a[i] == L'\\') { i++; backslashes++; }
        if (i == a.size()) {
            out.append(backslashes * 2, L'\\');   // before the closing quote
            break;
        }
        if (a[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(a[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

// The environment block: "name=value\0" sorted by name (case-insensitive),
// then "\0".
std::vector<wchar_t> environment_block(const std::map<std::string, std::string>& env) {
    std::vector<std::pair<std::wstring, std::wstring>> vars;
    for (const auto& kv : env) vars.push_back({widen(kv.first), widen(kv.second)});
    std::sort(vars.begin(), vars.end(), [](const auto& x, const auto& y) { return upper(x.first) < upper(y.first); });
    std::vector<wchar_t> block;
    for (const auto& v : vars) {
        block.insert(block.end(), v.first.begin(), v.first.end());
        block.push_back(L'=');
        block.insert(block.end(), v.second.begin(), v.second.end());
        block.push_back(L'\0');
    }
    if (block.empty()) block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

// An inheritable duplicate of one of our handles.
HANDLE inheritable_copy(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return nullptr;
    HANDLE copy = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &copy, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return nullptr;
    return copy;
}

} // namespace

ProcChild* proc_spawn(const SpawnRequest& req, intptr_t parent[3], std::string* err) {
    for (int i = 0; i < 3; i++) parent[i] = -1;

    // The program and the command line.
    std::wstring program;
    std::wstring cmdline;
    if (req.shell) {
        std::string comspec = env_get(req.env, "ComSpec");
        if (!comspec.empty()) {
            program = widen(comspec);
        } else {
            wchar_t sysdir[MAX_PATH];
            UINT n = GetSystemDirectoryW(sysdir, MAX_PATH);
            program = std::wstring(sysdir, n) + L"\\cmd.exe";
        }
        // /s: cmd strips the outer quotes and runs the rest as written.
        cmdline = quote_arg(program) + L" /d /s /c \"" + widen(req.argv[0]) + L"\"";
    } else {
        if (!resolve_program(req.argv[0], req.env, &program)) {
            *err = "cannot run '" + req.argv[0] + "': not found in PATH";
            return nullptr;
        }
        for (size_t i = 0; i < req.argv.size(); i++) {
            if (i) cmdline += L' ';
            cmdline += quote_arg(widen(req.argv[i]));
        }
    }

    // The child's standard handles: inheritable, closed here after the spawn.
    SECURITY_ATTRIBUTES inherit = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE child_h[3] = {nullptr, nullptr, nullptr};
    std::vector<HANDLE> to_close;
    auto fail = [&](const std::string& msg) -> ProcChild* {
        for (HANDLE h : to_close) CloseHandle(h);
        for (int i = 0; i < 3; i++) if (parent[i] != -1) { CloseHandle((HANDLE)parent[i]); parent[i] = -1; }
        *err = msg;
        return nullptr;
    };
    const char* names[3] = {"stdin", "stdout", "stderr"};
    const DWORD std_ids[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int i = 0; i < 3; i++) {
        const StdioSpec& spec = req.stdio[i];
        HANDLE h = nullptr;
        switch (spec.kind) {
            case StdioKind::inherit:
                h = inheritable_copy(GetStdHandle(std_ids[i]));   // null with no console
                break;
            case StdioKind::merge:
                break;   // below
            case StdioKind::pipe: {
                HANDLE r = nullptr, w = nullptr;
                if (!CreatePipe(&r, &w, &inherit, 64 * 1024))
                    return fail(std::string("pipe: ") + error_text(GetLastError()));
                // stdin: the child reads r, we write w; outputs the reverse.
                h = (i == 0) ? r : w;
                HANDLE ours = (i == 0) ? w : r;
                SetHandleInformation(ours, HANDLE_FLAG_INHERIT, 0);
                parent[i] = (intptr_t)ours;
                break;
            }
            case StdioKind::null_dev:
                h = CreateFileW(L"NUL", i == 0 ? GENERIC_READ : GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
                if (h == INVALID_HANDLE_VALUE) return fail(std::string("NUL: ") + error_text(GetLastError()));
                break;
            case StdioKind::file: {
                DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
                if (i == 0) {
                    h = CreateFileW(widen(spec.path).c_str(), GENERIC_READ, share, &inherit,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                } else if (spec.append) {
                    h = CreateFileW(widen(spec.path).c_str(), FILE_APPEND_DATA | SYNCHRONIZE, share, &inherit,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                } else {
                    h = CreateFileW(widen(spec.path).c_str(), GENERIC_WRITE, share, &inherit,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                }
                if (h == INVALID_HANDLE_VALUE)
                    return fail(std::string(names[i]) + " file " + spec.path + ": " + error_text(GetLastError()));
                break;
            }
            case StdioKind::handle:
                h = inheritable_copy((HANDLE)spec.handle);
                if (!h) return fail(std::string(names[i]) + ": " + error_text(GetLastError()));
                break;
        }
        if (h) to_close.push_back(h);
        child_h[i] = h;
    }
    if (req.stdio[2].kind == StdioKind::merge) child_h[2] = child_h[1];

    // Inherit exactly the standard handles (each once).
    std::vector<HANDLE> inherited;
    for (HANDLE h : child_h)
        if (h && std::find(inherited.begin(), inherited.end(), h) == inherited.end()) inherited.push_back(h);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<char> attr_buf(attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)attr_buf.data();
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size))
        return fail("cannot run '" + req.argv[0] + "': " + error_text(GetLastError()));
    if (!inherited.empty())
        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr);

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = child_h[0];
    si.StartupInfo.hStdOutput = child_h[1];
    si.StartupInfo.hStdError = child_h[2];
    si.lpAttributeList = attrs;

    std::vector<wchar_t> env_block = environment_block(req.env);
    std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back(L'\0');
    std::wstring cwd = widen(req.cwd);

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(program.c_str(), cmd.data(), nullptr, nullptr, !inherited.empty(),
                             CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                             env_block.data(), req.has_cwd ? cwd.c_str() : nullptr,
                             &si.StartupInfo, &pi);
    DWORD create_error = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attrs);
    if (!ok) {
        std::string where = req.has_cwd && (create_error == ERROR_DIRECTORY || create_error == ERROR_PATH_NOT_FOUND)
                                ? " (or cwd '" + req.cwd + "' does not exist)" : "";
        return fail("cannot run '" + req.argv[0] + "': " + error_text(create_error) + where);
    }
    for (HANDLE h : to_close) CloseHandle(h);
    CloseHandle(pi.hThread);

    ProcChild* c = new ProcChild();
    c->process = pi.hProcess;
    c->pid = pi.dwProcessId;
    return c;
}

void proc_environment(std::map<std::string, std::string>* env) {
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return;
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        std::wstring entry(p);
        // Skip the per-drive current directories ("=C:=C:\dir").
        size_t eq = entry.find(L'=', 1);
        if (entry[0] == L'=' || eq == std::wstring::npos) continue;
        (*env)[narrow(entry.substr(0, eq))] = narrow(entry.substr(eq + 1));
    }
    FreeEnvironmentStringsW(block);
}

int64_t proc_pid(const ProcChild* child) { return (int64_t)child->pid; }

bool proc_try_reap(ProcChild* c) {
    if (c->exited) return true;
    if (WaitForSingleObject(c->process, 0) != WAIT_OBJECT_0) return false;
    GetExitCodeProcess(c->process, &c->exit_code);
    c->exited = true;
    return true;
}

int64_t proc_exit_code(const ProcChild* c) { return (int64_t)c->exit_code; }

int proc_wait_exit(MobiusState* state, ProcChild* c, int64_t timeout_ms) {
    (void)c;
    int64_t step = (timeout_ms >= 0 && timeout_ms < EXIT_POLL_MS) ? timeout_ms : EXIT_POLL_MS;
    return mobius_io_wait(state, nullptr, 0, step);
}

bool proc_signal(ProcChild* c, ProcSignal sig) {
    (void)sig;
    return TerminateProcess(c->process, TERMINATED_EXIT_CODE) != 0;
}

void proc_release(ProcChild* c) {
    if (!c) return;
    if (c->process) CloseHandle(c->process);
    delete c;
}

void proc_init() {}
