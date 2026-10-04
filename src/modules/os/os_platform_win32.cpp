// Windows implementation of os_platform.h. Paths and strings are UTF-8,
// converted for the wide-character APIs.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "os_platform.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>

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

bool is_file(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// The extensions tried for a program name (PATHEXT).
std::vector<std::wstring> program_extensions() {
    const wchar_t* pathext = _wgetenv(L"PATHEXT");
    std::wstring list = pathext && *pathext ? pathext : L".COM;.EXE;.BAT;.CMD";
    std::vector<std::wstring> exts;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(L';', start);
        std::wstring ext = list.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!ext.empty()) exts.push_back(ext);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return exts;
}

// `base` itself, or with one of PATHEXT's extensions.
bool find_program(const std::wstring& base, std::string* path) {
    if (is_file(base)) { *path = narrow(base); return true; }
    for (const std::wstring& ext : program_extensions()) {
        if (is_file(base + ext)) { *path = narrow(base + ext); return true; }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------

bool os_platform_getenv(const std::string& name, std::string* value) {
    const wchar_t* v = _wgetenv(widen(name).c_str());
    if (!v) return false;
    *value = narrow(v);
    return true;
}

bool os_platform_setenv(const std::string& name, const std::string& value) {
    return _wputenv_s(widen(name).c_str(), widen(value).c_str()) == 0;
}

bool os_platform_unsetenv(const std::string& name) {
    return _wputenv_s(widen(name).c_str(), L"") == 0;   // an empty value removes it
}

void os_platform_environment(std::vector<std::pair<std::string, std::string>>* vars) {
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return;
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        std::wstring entry(p);
        size_t eq = entry.find(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;   // also "=C:=C:\dir"
        vars->push_back({narrow(entry.substr(0, eq)), narrow(entry.substr(eq + 1))});
    }
    FreeEnvironmentStringsW(block);
}

// ---------------------------------------------------------------------------
// Working directory, sleeping, commands
// ---------------------------------------------------------------------------

bool os_platform_getcwd(std::string* path) {
    DWORD n = GetCurrentDirectoryW(0, nullptr);
    if (n == 0) return false;
    std::wstring buf(n, L'\0');
    n = GetCurrentDirectoryW(n, &buf[0]);
    if (n == 0 || n >= buf.size()) return false;
    buf.resize(n);
    *path = narrow(buf);
    return true;
}

bool os_platform_chdir(const std::string& path) {
    return SetCurrentDirectoryW(widen(path).c_str()) != 0;
}

void os_platform_sleep(double seconds) {
    Sleep((DWORD)(seconds * 1000.0));
}

int64_t os_platform_system(const std::string& command) {
    return (int64_t)_wsystem(widen(command).c_str());
}

bool os_platform_exec(const std::string& command, std::string* output) {
    FILE* fp = _wpopen(widen(command).c_str(), L"rb");
    if (!fp) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) output->append(buf, n);
    _pclose(fp);
    return true;
}

// ---------------------------------------------------------------------------
// Process and system
// ---------------------------------------------------------------------------

int64_t os_platform_getpid() { return (int64_t)GetCurrentProcessId(); }

bool os_platform_getppid(int64_t* pid) {
    (void)pid;
    return false;   // not tracked by Windows
}

bool os_platform_hostname(std::string* name) {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buf, &size)) return false;
    *name = narrow(std::wstring(buf, size));
    return true;
}

bool os_platform_executable(std::string* path) {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
    if (n == 0 || n >= buf.size()) return false;
    buf.resize(n);
    *path = narrow(buf);
    return true;
}

const char* os_platform_name() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "windows-aarch64";
#else
    return "windows-x86_64";
#endif
}

int64_t os_platform_cpu_count() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwNumberOfProcessors;
}

bool os_platform_uname(OsUname* info) {
    info->sysname = "Windows";
    os_platform_hostname(&info->nodename);
    // GetVersionEx reports the version the program is manifested for;
    // RtlGetVersion reports the real one.
    typedef LONG (WINAPI* RtlGetVersionFn)(OSVERSIONINFOW*);
    OSVERSIONINFOW ovi = {};
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn rtl_get_version = ntdll ? (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    if (rtl_get_version) rtl_get_version(&ovi);
    char ver[64];
    snprintf(ver, sizeof(ver), "%lu.%lu.%lu", ovi.dwMajorVersion, ovi.dwMinorVersion, ovi.dwBuildNumber);
    info->release = ver;
    info->version = ver;
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: info->machine = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM:   info->machine = "arm"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: info->machine = "aarch64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: info->machine = "x86"; break;
        default:                           info->machine = "unknown"; break;
    }
    return true;
}

bool os_platform_which(const std::string& name, std::string* path) {
    if (name.empty()) return false;
    std::wstring wname = widen(name);
    if (wname.find_first_of(L"\\/:") != std::wstring::npos) return find_program(wname, path);
    const wchar_t* path_env = _wgetenv(L"PATH");
    if (!path_env || !*path_env) return false;
    std::wstring list(path_env);
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(L';', start);
        std::wstring dir = list.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (dir.empty()) dir = L".";
        if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
        if (find_program(dir + wname, path)) return true;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Directories and files
// ---------------------------------------------------------------------------

bool os_platform_listdir(const std::string& path, std::vector<std::string>* names) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((widen(path) + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        names->push_back(narrow(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

bool os_platform_mkdir(const std::string& path) { return CreateDirectoryW(widen(path).c_str(), nullptr) != 0; }

bool os_platform_rmdir(const std::string& path) { return RemoveDirectoryW(widen(path).c_str()) != 0; }

bool os_platform_remove(const std::string& path) { return _wremove(widen(path).c_str()) == 0; }

bool os_platform_rename(const std::string& from, const std::string& to) {
    // Replaces an existing file, as rename does on POSIX.
    return MoveFileExW(widen(from).c_str(), widen(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}

bool os_platform_copy_file(const std::string& from, const std::string& to) {
    return CopyFileW(widen(from).c_str(), widen(to).c_str(), FALSE) != 0;
}

bool os_platform_touch(const std::string& path) {
    HANDLE h = CreateFileW(widen(path).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    SetFileTime(h, nullptr, &ft, &ft);
    CloseHandle(h);
    return true;
}

bool os_platform_chmod(const std::string& path, int64_t mode) {
    // Windows has only the read-only attribute: write permission for the
    // owner clears it.
    return _wchmod(widen(path).c_str(), (mode & 0200) ? (_S_IREAD | _S_IWRITE) : _S_IREAD) == 0;
}

bool os_platform_link(const std::string& target, const std::string& link_path) {
    return CreateHardLinkW(widen(link_path).c_str(), widen(target).c_str(), nullptr) != 0;
}

bool os_platform_symlink(const std::string& target, const std::string& link_path) {
    std::wstring wtarget = widen(target);
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;   // with Developer Mode
    DWORD attrs = GetFileAttributesW(wtarget.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (CreateSymbolicLinkW(widen(link_path).c_str(), wtarget.c_str(), flags)) return true;
    // Older Windows rejects the unprivileged flag.
    return CreateSymbolicLinkW(widen(link_path).c_str(), wtarget.c_str(),
                               flags & ~SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
}

bool os_platform_realpath(const std::string& path, std::string* resolved) {
    // Like realpath: the path must exist, and links are resolved.
    HANDLE h = CreateFileW(widen(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::wstring buf(32768, L'\0');
    DWORD n = GetFinalPathNameByHandleW(h, &buf[0], (DWORD)buf.size(), FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (n == 0 || n >= buf.size()) return false;
    buf.resize(n);
    // Drop the "\\?\" prefix (and "\\?\UNC\" -> "\\").
    if (buf.compare(0, 8, L"\\\\?\\UNC\\") == 0) buf = L"\\\\" + buf.substr(8);
    else if (buf.compare(0, 4, L"\\\\?\\") == 0) buf = buf.substr(4);
    *resolved = narrow(buf);
    return true;
}

bool os_platform_stat(const std::string& path, OsStat* out) {
    struct _stat64 st;
    if (_wstat64(widen(path).c_str(), &st) != 0) return false;
    out->size = (int64_t)st.st_size;
    out->mtime = (int64_t)st.st_mtime;
    out->atime = (int64_t)st.st_atime;
    out->ctime = (int64_t)st.st_ctime;
    out->mode = (int64_t)st.st_mode;
    out->is_dir = (st.st_mode & _S_IFMT) == _S_IFDIR;
    out->is_file = (st.st_mode & _S_IFMT) == _S_IFREG;
    out->is_link = false;   // stat follows links, as on POSIX
    out->readonly = (st.st_mode & _S_IWRITE) == 0;
    return true;
}

// ---------------------------------------------------------------------------
// Temporary files, glob, walk
// ---------------------------------------------------------------------------

std::string os_platform_tmpdir() {
    wchar_t buf[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    if (n == 0 || n > MAX_PATH) return ".";
    if (n > 1 && (buf[n - 1] == L'\\' || buf[n - 1] == L'/')) n--;
    return narrow(std::wstring(buf, n));
}

bool os_platform_tmpfile(std::string* path) {
    wchar_t dir[MAX_PATH + 1];
    if (GetTempPathW(MAX_PATH + 1, dir) == 0) return false;
    wchar_t name[MAX_PATH + 1];
    if (!GetTempFileNameW(dir, L"mob", 0, name)) return false;   // creates it
    *path = narrow(name);
    return true;
}

void os_platform_glob(const std::string& pattern, std::vector<std::string>* paths) {
    // Wildcards in the last component only.
    std::wstring wpattern = widen(pattern);
    size_t sep = wpattern.find_last_of(L"\\/");
    std::wstring dir = sep == std::wstring::npos ? L"" : wpattern.substr(0, sep + 1);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        paths->push_back(narrow(dir + fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void walk(const std::wstring& base, std::vector<std::string>* paths) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = base + L"\\" + fd.cFileName;
        paths->push_back(narrow(full));
        // Not into directory links (they may loop).
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            walk(full, paths);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void os_platform_walkdir(const std::string& path, std::vector<std::string>* paths) {
    walk(widen(path), paths);
}

// ---------------------------------------------------------------------------
// Path syntax
// ---------------------------------------------------------------------------

char os_platform_separator() { return '\\'; }

bool os_platform_is_separator(char c) { return c == '\\' || c == '/'; }

std::string os_platform_basename(const std::string& path) {
    // As POSIX basename: trailing separators are ignored; a root stays.
    size_t end = path.size();
    while (end > 1 && os_platform_is_separator(path[end - 1])) end--;
    if (end == 1 && os_platform_is_separator(path[0])) return path.substr(0, 1);
    size_t start = end;
    while (start > 0 && !os_platform_is_separator(path[start - 1]) && path[start - 1] != ':') start--;
    if (start == end) return path.empty() ? "." : path;
    return path.substr(start, end - start);
}

std::string os_platform_dirname(const std::string& path) {
    // As POSIX dirname, keeping a drive ("C:\dir" -> "C:\", "C:x" -> "C:").
    size_t root = (path.size() >= 2 && path[1] == ':') ? 2 : 0;
    size_t end = path.size();
    while (end > root + 1 && os_platform_is_separator(path[end - 1])) end--;
    size_t sep = end;
    while (sep > root && !os_platform_is_separator(path[sep - 1])) sep--;
    if (sep == root) return root ? path.substr(0, root) : ".";
    while (sep > root + 1 && os_platform_is_separator(path[sep - 2])) sep--;
    return path.substr(0, sep == root + 1 ? sep : sep - 1);
}
