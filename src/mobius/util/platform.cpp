#include "util/platform.h"

#include <chrono>
#include <cstring>
#include <system_error>
#include <filesystem>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <limits.h>
#  include <pthread.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#endif

uint64_t platform_monotonic_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

size_t platform_page_size() {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

#if defined(_WIN32)
static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::string last_error_text() {
    DWORD code = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::string text = msg ? narrow(msg) : "error " + std::to_string(code);
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}
#endif

std::string platform_executable_path() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
    if (n == 0 || n >= buf.size()) return "";
    buf.resize(n);
    return narrow(buf);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(&buf[0], &size) != 0) return "";
    buf.resize(strlen(buf.c_str()));
    char resolved[PATH_MAX];
    return realpath(buf.c_str(), resolved) ? std::string(resolved) : buf;
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    return std::string(buf, (size_t)n);
#endif
}

void* platform_library_open(const std::string& path, bool global, std::string* error) {
#if defined(_WIN32)
    (void)global;
    // Let the DLL's own directory satisfy its dependencies (a package's
    // runtime libraries sit beside it).
    HMODULE h = LoadLibraryExW(widen(path).c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h && error) *error = last_error_text();
    return (void*)h;
#else
    void* h = dlopen(path.c_str(), RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
    if (!h && error) {
        const char* e = dlerror();
        *error = e ? e : "unknown error";
    }
    return h;
#endif
}

void* platform_library_symbol(void* library, const char* name) {
#if defined(_WIN32)
    return library ? (void*)GetProcAddress((HMODULE)library, name) : nullptr;
#else
    return library ? dlsym(library, name) : nullptr;
#endif
}

void platform_library_close(void* library) {
    if (!library) return;
#if defined(_WIN32)
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}

// A UTF-8 path (Windows file APIs would otherwise read it in the ANSI code page).
static std::filesystem::path fs_path(const std::string& s) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
#else
    return std::filesystem::u8path(s);
#endif
}

bool platform_is_regular_file(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(fs_path(path), ec);
}

bool platform_is_directory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(fs_path(path), ec);
}

bool platform_stack_low(char** low) {
#if defined(_WIN32)
    // Works on fibers too: the thread information block tracks the
    // running fiber's stack.
    ULONG_PTR lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    *low = (char*)lo;
    return lo != 0;
#else
    (void)low;
    return false;   // POSIX fibers know their own stacks (MobiusFiber)
#endif
}
