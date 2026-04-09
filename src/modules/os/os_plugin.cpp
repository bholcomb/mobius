#include <mobius/mobius_plugin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <direct.h>
  #include <io.h>
  #include <process.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <shlwapi.h>

  #ifndef PATH_MAX
    #define PATH_MAX MAX_PATH
  #endif

  #ifndef S_ISDIR
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #endif
  #ifndef S_ISREG
    #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
  #endif
  #define S_ISLNK(m) (false)

  #define stat_t struct _stat64
  #define os_stat_call(p, s) _stat64(p, s)
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/utsname.h>
  #include <dirent.h>
  #include <glob.h>
  #include <libgen.h>
  #include <fcntl.h>
  #include <climits>
  #include <sys/wait.h>

  #define stat_t struct stat
  #define os_stat_call(p, s) ::stat(p, s)

  extern char** environ;
#endif

#ifdef __APPLE__
  #include <mach-o/dyld.h>
#endif

// ============================================================================
// ENVIRONMENT VARIABLES
// ============================================================================

static int os_getenv(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "getenv() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "getenv() expects a string argument");
    const char* name = mobius_stack_asString(state, -1);
    const char* val = ::getenv(name);
    mobius_stack_pop(state, 1);
    if (val) mobius_stack_pushString(state, val);
    else     mobius_stack_pushNil(state);
    return 1;
}

static int os_setenv(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "setenv() expects 2 arguments");
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, "setenv() expects string arguments");
    const char* value = mobius_stack_asString(state, -1);
    const char* name  = mobius_stack_asString(state, -2);
#ifdef _WIN32
    int rc = _putenv_s(name, value);
#else
    int rc = ::setenv(name, value, 1);
#endif
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

static int os_unsetenv(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "unsetenv() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "unsetenv() expects a string argument");
    const char* name = mobius_stack_asString(state, -1);
#ifdef _WIN32
    int rc = _putenv_s(name, "");
#else
    int rc = ::unsetenv(name);
#endif
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

// ============================================================================
// WORKING DIRECTORY
// ============================================================================

static int os_getcwd(MobiusState* state, int arg_count) {
    (void)arg_count;
    char buf[PATH_MAX];
#ifdef _WIN32
    if (_getcwd(buf, sizeof(buf))) {
#else
    if (::getcwd(buf, sizeof(buf))) {
#endif
        mobius_stack_pushString(state, buf);
    } else {
        mobius_stack_pushNil(state);
    }
    return 1;
}

static int os_chdir(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "chdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "chdir() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
#ifdef _WIN32
    int rc = _chdir(path);
#else
    int rc = ::chdir(path);
#endif
    mobius_stack_pop(state, 1);
    if (rc != 0)
        return mobius_error(state, "chdir() failed");
    mobius_stack_pushBool(state, true);
    return 1;
}

// ============================================================================
// SLEEP
// ============================================================================

static int os_sleep(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "sleep() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "sleep() expects a numeric argument");
    double seconds = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    if (seconds > 0.0) {
#ifdef _WIN32
        Sleep((DWORD)(seconds * 1000.0));
#else
        struct timespec ts;
        ts.tv_sec  = (time_t)seconds;
        ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, nullptr);
#endif
    }
    mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// PROCESS / COMMAND EXECUTION
// ============================================================================

static bool os_file_exists(const char* path) {
    stat_t st;
    return os_stat_call(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool os_is_executable_path(const char* path) {
#ifdef _WIN32
    return os_file_exists(path);
#else
    return os_file_exists(path) && ::access(path, X_OK) == 0;
#endif
}

static bool os_path_has_separator(const char* path) {
    return strchr(path, '/') != nullptr || strchr(path, '\\') != nullptr;
}

static std::string os_join_path_component(const std::string& base, const std::string& part) {
    if (base.empty()) return part;
    if (part.empty()) return base;
#ifdef _WIN32
    char sep = '\\';
    bool left_has_sep = base.back() == '/' || base.back() == '\\';
    bool right_has_sep = part.front() == '/' || part.front() == '\\';
#else
    char sep = '/';
    bool left_has_sep = base.back() == '/';
    bool right_has_sep = part.front() == '/';
#endif
    if (left_has_sep && right_has_sep) return base + part.substr(1);
    if (!left_has_sep && !right_has_sep) return base + sep + part;
    return base + part;
}

static bool os_current_executable(std::string& out) {
#ifdef _WIN32
    char buf[PATH_MAX];
    DWORD len = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    out.assign(buf, len);
    return true;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return false;
    out.assign(buf.data());
    return true;
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) return false;
    buf[len] = '\0';
    out.assign(buf);
    return true;
#endif
}

static bool os_hostname(std::string& out) {
#ifdef _WIN32
    char hostname[256] = {0};
    DWORD size = sizeof(hostname);
    if (!GetComputerNameA(hostname, &size)) return false;
    out.assign(hostname, size);
    return true;
#else
    char hostname[256] = {0};
    if (::gethostname(hostname, sizeof(hostname)) != 0) return false;
    hostname[sizeof(hostname) - 1] = '\0';
    out.assign(hostname);
    return true;
#endif
}

static bool os_which_impl(const char* name, std::string& out) {
    if (!name || !*name) return false;

#ifdef _WIN32
    const char path_sep = ';';
    const char* default_exts = ".COM;.EXE;.BAT;.CMD";
#else
    const char path_sep = ':';
#endif

    if (os_path_has_separator(name)) {
#ifdef _WIN32
        if (os_file_exists(name)) {
            out.assign(name);
            return true;
        }
        const char* pathext = ::getenv("PATHEXT");
        std::string exts = pathext && *pathext ? pathext : default_exts;
        size_t start = 0;
        while (start <= exts.size()) {
            size_t end = exts.find(path_sep, start);
            std::string ext = exts.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!ext.empty()) {
                std::string candidate = std::string(name) + ext;
                if (os_file_exists(candidate.c_str())) {
                    out = candidate;
                    return true;
                }
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return false;
#else
        if (os_is_executable_path(name)) {
            out.assign(name);
            return true;
        }
        return false;
#endif
    }

    const char* path_env = ::getenv("PATH");
    if (!path_env || !*path_env) return false;
    std::string path_list(path_env);
    size_t start = 0;
    while (start <= path_list.size()) {
        size_t end = path_list.find(path_sep, start);
        std::string dir = path_list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (dir.empty()) dir = ".";
        std::string candidate = os_join_path_component(dir, name);
#ifdef _WIN32
        if (os_file_exists(candidate.c_str())) {
            out = candidate;
            return true;
        }
        const char* pathext = ::getenv("PATHEXT");
        std::string exts = pathext && *pathext ? pathext : default_exts;
        size_t ext_start = 0;
        while (ext_start <= exts.size()) {
            size_t ext_end = exts.find(path_sep, ext_start);
            std::string ext = exts.substr(ext_start, ext_end == std::string::npos ? std::string::npos : ext_end - ext_start);
            if (!ext.empty()) {
                std::string with_ext = candidate + ext;
                if (os_file_exists(with_ext.c_str())) {
                    out = with_ext;
                    return true;
                }
            }
            if (ext_end == std::string::npos) break;
            ext_start = ext_end + 1;
        }
#else
        if (os_is_executable_path(candidate.c_str())) {
            out = candidate;
            return true;
        }
#endif
        if (end == std::string::npos) break;
        start = end + 1;
    }

    return false;
}

static int os_system(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "system() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "system() expects a string argument");
    const char* cmd = mobius_stack_asString(state, -1);
    int rc = ::system(cmd);
    mobius_stack_pop(state, 1);
#ifdef _WIN32
    mobius_stack_pushInt64(state, rc);
#else
    int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    mobius_stack_pushInt64(state, exit_code);
#endif
    return 1;
}

static int os_exec(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "exec() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "exec() expects a string argument");
    const char* cmd = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

#ifdef _WIN32
    FILE* fp = _popen(cmd, "r");
#else
    FILE* fp = popen(cmd, "r");
#endif
    if (!fp) {
        mobius_stack_pushNil(state);
        return 1;
    }

    char buf[4096];
    std::string output;
    while (fgets(buf, sizeof(buf), fp)) {
        output += buf;
    }
#ifdef _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif

    mobius_stack_pushString(state, output.c_str());
    return 1;
}

static int os_getpid(MobiusState* state, int arg_count) {
    (void)arg_count;
#ifdef _WIN32
    mobius_stack_pushInt64(state, (int64_t)_getpid());
#else
    mobius_stack_pushInt64(state, (int64_t)::getpid());
#endif
    return 1;
}

static int os_getppid(MobiusState* state, int arg_count) {
    (void)arg_count;
#ifdef _WIN32
    mobius_stack_pushNil(state);
#else
    mobius_stack_pushInt64(state, (int64_t)::getppid());
#endif
    return 1;
}

static int os_hostname_fn(MobiusState* state, int arg_count) {
    (void)arg_count;
    std::string hostname;
    if (os_hostname(hostname)) mobius_stack_pushString(state, hostname.c_str());
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_executable(MobiusState* state, int arg_count) {
    (void)arg_count;
    std::string path;
    if (os_current_executable(path)) mobius_stack_pushString(state, path.c_str());
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_env(MobiusState* state, int arg_count) {
    (void)arg_count;
    mobius_stack_pushNewTable(state, 32);
    int tbl = mobius_stack_size(state) - 1;

#ifdef _WIN32
    LPCH env_block = GetEnvironmentStringsA();
    if (!env_block) return 1;
    for (LPCH p = env_block; *p; ) {
        std::string entry(p);
        p += entry.size() + 1;
        size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string key = entry.substr(0, eq);
        std::string value = entry.substr(eq + 1);
        mobius_stack_pushString(state, value.c_str());
        mobius_stack_setTableField(state, tbl, key.c_str());
    }
    FreeEnvironmentStringsA(env_block);
#else
    for (char** p = environ; p && *p; ++p) {
        std::string entry(*p);
        size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string key = entry.substr(0, eq);
        std::string value = entry.substr(eq + 1);
        mobius_stack_pushString(state, value.c_str());
        mobius_stack_setTableField(state, tbl, key.c_str());
    }
#endif
    return 1;
}

static int os_which(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "which() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "which() expects a string argument");
    const char* name = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    std::string resolved;
    if (os_which_impl(name, resolved)) mobius_stack_pushString(state, resolved.c_str());
    else mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

static int os_listdir(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "listdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "listdir() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_pushNewArray(state, 16);
    int arr_idx = mobius_stack_size(state) - 1;

#ifdef _WIN32
    char search_path[PATH_MAX];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return 1;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        mobius_stack_pushString(state, fd.cFileName);
        mobius_stack_arrayPush(state, arr_idx);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* dir = opendir(path);
    if (!dir) {
        return 1;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        mobius_stack_pushString(state, entry->d_name);
        mobius_stack_arrayPush(state, arr_idx);
    }
    closedir(dir);
#endif
    return 1;
}

static int os_mkdir(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "mkdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "mkdir() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
#ifdef _WIN32
    int rc = _mkdir(path);
#else
    int rc = ::mkdir(path, 0755);
#endif
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

static int os_rmdir(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "rmdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "rmdir() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
#ifdef _WIN32
    int rc = _rmdir(path);
#else
    int rc = ::rmdir(path);
#endif
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

static int os_remove(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "remove() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "remove() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    int rc = ::remove(path);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

static int os_rename(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "rename() expects 2 arguments");
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, "rename() expects string arguments");
    const char* new_path = mobius_stack_asString(state, -1);
    const char* old_path = mobius_stack_asString(state, -2);
    int rc = ::rename(old_path, new_path);
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

static int os_cp(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "cp() expects 2 arguments");
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, "cp() expects string arguments");
    const char* dst = mobius_stack_asString(state, -1);
    const char* src = mobius_stack_asString(state, -2);
    mobius_stack_pop(state, 2);

    FILE* in = fopen(src, "rb");
    if (!in) {
        mobius_stack_pushBool(state, false);
        return 1;
    }
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        mobius_stack_pushBool(state, false);
        return 1;
    }
    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    mobius_stack_pushBool(state, ok);
    return 1;
}

static int os_touch(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "touch() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "touch() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

#ifdef _WIN32
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        SetFileTime(h, NULL, &ft, &ft);
        CloseHandle(h);
        mobius_stack_pushBool(state, true);
    } else {
        mobius_stack_pushBool(state, false);
    }
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_NOCTTY, 0644);
    if (fd >= 0) {
        struct timespec times[2] = {{0, UTIME_NOW}, {0, UTIME_NOW}};
        futimens(fd, times);
        close(fd);
        mobius_stack_pushBool(state, true);
    } else {
        mobius_stack_pushBool(state, false);
    }
#endif
    return 1;
}

// ============================================================================
// FILE METADATA
// ============================================================================

static int os_stat(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "stat() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "stat() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    stat_t st;
    if (os_stat_call(path, &st) != 0) {
        mobius_stack_pushNil(state);
        return 1;
    }

    mobius_stack_pushNewTable(state, 12);
    int tbl = mobius_stack_size(state) - 1;

    mobius_stack_pushString(state, path);
    mobius_stack_setTableField(state, tbl, "path");

    mobius_stack_pushInt64(state, (int64_t)st.st_size);
    mobius_stack_setTableField(state, tbl, "size");

    mobius_stack_pushInt64(state, (int64_t)st.st_mtime);
    mobius_stack_setTableField(state, tbl, "mtime");

    mobius_stack_pushInt64(state, (int64_t)st.st_atime);
    mobius_stack_setTableField(state, tbl, "atime");

    mobius_stack_pushInt64(state, (int64_t)st.st_ctime);
    mobius_stack_setTableField(state, tbl, "ctime");

    mobius_stack_pushInt64(state, (int64_t)st.st_mode);
    mobius_stack_setTableField(state, tbl, "mode");

    mobius_stack_pushBool(state, S_ISDIR(st.st_mode));
    mobius_stack_setTableField(state, tbl, "is_dir");

    mobius_stack_pushBool(state, S_ISREG(st.st_mode));
    mobius_stack_setTableField(state, tbl, "is_file");

    mobius_stack_pushBool(state, S_ISLNK(st.st_mode));
    mobius_stack_setTableField(state, tbl, "is_link");

    const bool is_dir = S_ISDIR(st.st_mode);
    const bool is_file = S_ISREG(st.st_mode);
    const bool is_link = S_ISLNK(st.st_mode);
    const bool is_other = !is_dir && !is_file && !is_link;
    const bool readonly = (st.st_mode & 0222) == 0;

    if (is_dir) {
        mobius_stack_pushString(state, "dir");
    } else if (is_file) {
        mobius_stack_pushString(state, "file");
    } else if (is_link) {
        mobius_stack_pushString(state, "link");
    } else {
        mobius_stack_pushString(state, "other");
    }
    mobius_stack_setTableField(state, tbl, "type");

    mobius_stack_pushBool(state, is_other);
    mobius_stack_setTableField(state, tbl, "is_other");

    mobius_stack_pushBool(state, readonly);
    mobius_stack_setTableField(state, tbl, "readonly");

    return 1;
}

static int os_chmod(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "chmod() expects 2 arguments");
    if (!mobius_stack_isInteger(state, -1))
        return mobius_error(state, "chmod() mode must be an integer");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "chmod() path must be a string");
    int64_t mode = mobius_stack_asInt64(state, -1);
    const char* path = mobius_stack_asString(state, -2);
#ifdef _WIN32
    int rc = _chmod(path, (int)mode);
#else
    int rc = ::chmod(path, (mode_t)mode);
#endif
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, rc == 0);
    return 1;
}

static int os_filesize(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "filesize() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "filesize() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    stat_t st;
    if (os_stat_call(path, &st) == 0) {
        mobius_stack_pushInt64(state, (int64_t)st.st_size);
    } else {
        mobius_stack_pushNil(state);
    }
    return 1;
}

// ============================================================================
// LINKS AND PATHS
// ============================================================================

static int os_link(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "link() expects 2 arguments");
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, "link() expects string arguments");
    const char* linkpath = mobius_stack_asString(state, -1);
    const char* target   = mobius_stack_asString(state, -2);
    mobius_stack_pop(state, 2);
#ifdef _WIN32
    BOOL ok = CreateHardLinkA(linkpath, target, NULL);
    mobius_stack_pushBool(state, ok != 0);
#else
    int rc = ::link(target, linkpath);
    mobius_stack_pushBool(state, rc == 0);
#endif
    return 1;
}

static int os_symlink(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "symlink() expects 2 arguments");
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, "symlink() expects string arguments");
    const char* linkpath = mobius_stack_asString(state, -1);
    const char* target   = mobius_stack_asString(state, -2);
    mobius_stack_pop(state, 2);
#ifdef _WIN32
    DWORD flags = 0;
    DWORD attrs = GetFileAttributesA(target);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    BOOL ok = CreateSymbolicLinkA(linkpath, target, flags);
    mobius_stack_pushBool(state, ok != 0);
#else
    int rc = ::symlink(target, linkpath);
    mobius_stack_pushBool(state, rc == 0);
#endif
    return 1;
}

static int os_realpath(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "realpath() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "realpath() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
#ifdef _WIN32
    char resolved[PATH_MAX];
    DWORD n = GetFullPathNameA(path, sizeof(resolved), resolved, NULL);
    if (n > 0 && n < sizeof(resolved)) {
        mobius_stack_pushString(state, resolved);
    } else {
        mobius_stack_pushNil(state);
    }
#else
    char resolved[PATH_MAX];
    if (::realpath(path, resolved)) {
        mobius_stack_pushString(state, resolved);
    } else {
        mobius_stack_pushNil(state);
    }
#endif
    return 1;
}

// ============================================================================
// TEMP FILES AND DIRECTORIES
// ============================================================================

static int os_tmpdir(MobiusState* state, int arg_count) {
    (void)arg_count;
#ifdef _WIN32
    char buf[PATH_MAX];
    DWORD n = GetTempPathA(sizeof(buf), buf);
    if (n > 0 && n < sizeof(buf)) {
        if (n > 1 && (buf[n-1] == '\\' || buf[n-1] == '/'))
            buf[n-1] = '\0';
        mobius_stack_pushString(state, buf);
    } else {
        mobius_stack_pushString(state, ".");
    }
#else
    const char* tmp = ::getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    mobius_stack_pushString(state, tmp);
#endif
    return 1;
}

static int os_tmpfile(MobiusState* state, int arg_count) {
    (void)arg_count;
#ifdef _WIN32
    char tmp_dir[PATH_MAX];
    GetTempPathA(sizeof(tmp_dir), tmp_dir);
    char tmp_name[PATH_MAX];
    if (GetTempFileNameA(tmp_dir, "mob", 0, tmp_name)) {
        mobius_stack_pushString(state, tmp_name);
    } else {
        mobius_stack_pushNil(state);
    }
#else
    const char* tmp = ::getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof(tmpl), "%s/mobius_XXXXXX", tmp);
    int fd = mkstemp(tmpl);
    if (fd >= 0) {
        close(fd);
        mobius_stack_pushString(state, tmpl);
    } else {
        mobius_stack_pushNil(state);
    }
#endif
    return 1;
}

// ============================================================================
// GLOB AND DIRECTORY WALKING
// ============================================================================

static int os_glob(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "glob() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "glob() expects a string argument");
    const char* pattern = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_pushNewArray(state, 16);
    int arr_idx = mobius_stack_size(state) - 1;

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        std::string dir;
        const char* last_sep = strrchr(pattern, '\\');
        if (!last_sep) last_sep = strrchr(pattern, '/');
        if (last_sep) dir.assign(pattern, last_sep + 1);

        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            std::string fullpath = dir + fd.cFileName;
            mobius_stack_pushString(state, fullpath.c_str());
            mobius_stack_arrayPush(state, arr_idx);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    glob_t gl;
    int rc = ::glob(pattern, GLOB_NOSORT, nullptr, &gl);
    if (rc == 0) {
        for (size_t i = 0; i < gl.gl_pathc; i++) {
            mobius_stack_pushString(state, gl.gl_pathv[i]);
            mobius_stack_arrayPush(state, arr_idx);
        }
        globfree(&gl);
    }
#endif
    return 1;
}

#ifdef _WIN32
static void walkdir_recurse(MobiusState* state, const std::string& base, int arr_idx) {
    std::string search = base + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        std::string fullpath = base + "\\" + fd.cFileName;
        mobius_stack_pushString(state, fullpath.c_str());
        mobius_stack_arrayPush(state, arr_idx);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walkdir_recurse(state, fullpath, arr_idx);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}
#else
static void walkdir_recurse(MobiusState* state, const char* base, int arr_idx) {
    DIR* dir = opendir(base);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base, entry->d_name);
        mobius_stack_pushString(state, fullpath);
        mobius_stack_arrayPush(state, arr_idx);

        struct stat st;
        if (::stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            walkdir_recurse(state, fullpath, arr_idx);
        }
    }
    closedir(dir);
}
#endif

static int os_walkdir(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "walkdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "walkdir() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_pushNewArray(state, 64);
    int arr_idx = mobius_stack_size(state) - 1;
#ifdef _WIN32
    walkdir_recurse(state, std::string(path), arr_idx);
#else
    walkdir_recurse(state, path, arr_idx);
#endif
    return 1;
}

// ============================================================================
// SYSTEM INFO
// ============================================================================

static int os_uname(MobiusState* state, int arg_count) {
    (void)arg_count;
    mobius_stack_pushNewTable(state, 8);
    int tbl = mobius_stack_size(state) - 1;

#ifdef _WIN32
    mobius_stack_pushString(state, "Windows");
    mobius_stack_setTableField(state, tbl, "sysname");

    char hostname[256] = {0};
    DWORD size = sizeof(hostname);
    GetComputerNameA(hostname, &size);
    mobius_stack_pushString(state, hostname);
    mobius_stack_setTableField(state, tbl, "nodename");

    OSVERSIONINFOA ovi;
    memset(&ovi, 0, sizeof(ovi));
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    #pragma warning(suppress: 4996)
    GetVersionExA(&ovi);
    char ver_buf[128];
    snprintf(ver_buf, sizeof(ver_buf), "%lu.%lu.%lu",
             ovi.dwMajorVersion, ovi.dwMinorVersion, ovi.dwBuildNumber);
    mobius_stack_pushString(state, ver_buf);
    mobius_stack_setTableField(state, tbl, "release");
    mobius_stack_pushString(state, ver_buf);
    mobius_stack_setTableField(state, tbl, "version");

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char* arch = "unknown";
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM:   arch = "arm"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "aarch64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
    }
    mobius_stack_pushString(state, arch);
    mobius_stack_setTableField(state, tbl, "machine");
#else
    struct utsname un;
    if (::uname(&un) == 0) {
        mobius_stack_pushString(state, un.sysname);
        mobius_stack_setTableField(state, tbl, "sysname");
        mobius_stack_pushString(state, un.nodename);
        mobius_stack_setTableField(state, tbl, "nodename");
        mobius_stack_pushString(state, un.release);
        mobius_stack_setTableField(state, tbl, "release");
        mobius_stack_pushString(state, un.version);
        mobius_stack_setTableField(state, tbl, "version");
        mobius_stack_pushString(state, un.machine);
        mobius_stack_setTableField(state, tbl, "machine");
    }
#endif
    return 1;
}

static int os_cpu_count(MobiusState* state, int arg_count) {
    (void)arg_count;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    mobius_stack_pushInt64(state, (int64_t)si.dwNumberOfProcessors);
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    mobius_stack_pushInt64(state, n > 0 ? (int64_t)n : 1);
#endif
    return 1;
}

// ============================================================================
// DATETIME
// ============================================================================

static int os_gmtime(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "gmtime() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "gmtime() expects a numeric argument");
    time_t t = (time_t)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);

    struct tm tm_buf;
#ifdef _WIN32
    if (gmtime_s(&tm_buf, &t) != 0) { mobius_stack_pushNil(state); return 1; }
    struct tm* tm = &tm_buf;
#else
    struct tm* tm = ::gmtime_r(&t, &tm_buf);
    if (!tm) { mobius_stack_pushNil(state); return 1; }
#endif

    mobius_stack_pushNewTable(state, 8);
    int tbl = mobius_stack_size(state) - 1;
    mobius_stack_pushInt64(state, tm->tm_year + 1900);
    mobius_stack_setTableField(state, tbl, "year");
    mobius_stack_pushInt64(state, tm->tm_mon + 1);
    mobius_stack_setTableField(state, tbl, "month");
    mobius_stack_pushInt64(state, tm->tm_mday);
    mobius_stack_setTableField(state, tbl, "day");
    mobius_stack_pushInt64(state, tm->tm_hour);
    mobius_stack_setTableField(state, tbl, "hour");
    mobius_stack_pushInt64(state, tm->tm_min);
    mobius_stack_setTableField(state, tbl, "min");
    mobius_stack_pushInt64(state, tm->tm_sec);
    mobius_stack_setTableField(state, tbl, "sec");
    mobius_stack_pushInt64(state, tm->tm_wday);
    mobius_stack_setTableField(state, tbl, "wday");
    mobius_stack_pushInt64(state, tm->tm_yday);
    mobius_stack_setTableField(state, tbl, "yday");
    return 1;
}

static int os_localtime(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "localtime() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "localtime() expects a numeric argument");
    time_t t = (time_t)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);

    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &t) != 0) { mobius_stack_pushNil(state); return 1; }
    struct tm* tm = &tm_buf;
#else
    struct tm* tm = ::localtime_r(&t, &tm_buf);
    if (!tm) { mobius_stack_pushNil(state); return 1; }
#endif

    mobius_stack_pushNewTable(state, 8);
    int tbl = mobius_stack_size(state) - 1;
    mobius_stack_pushInt64(state, tm->tm_year + 1900);
    mobius_stack_setTableField(state, tbl, "year");
    mobius_stack_pushInt64(state, tm->tm_mon + 1);
    mobius_stack_setTableField(state, tbl, "month");
    mobius_stack_pushInt64(state, tm->tm_mday);
    mobius_stack_setTableField(state, tbl, "day");
    mobius_stack_pushInt64(state, tm->tm_hour);
    mobius_stack_setTableField(state, tbl, "hour");
    mobius_stack_pushInt64(state, tm->tm_min);
    mobius_stack_setTableField(state, tbl, "min");
    mobius_stack_pushInt64(state, tm->tm_sec);
    mobius_stack_setTableField(state, tbl, "sec");
    mobius_stack_pushInt64(state, tm->tm_wday);
    mobius_stack_setTableField(state, tbl, "wday");
    mobius_stack_pushInt64(state, tm->tm_yday);
    mobius_stack_setTableField(state, tbl, "yday");
    return 1;
}

static int os_strftime(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "strftime() expects 2 arguments");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "strftime() timestamp must be a number");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "strftime() format must be a string");
    time_t t = (time_t)mobius_stack_asInt64(state, -1);
    const char* fmt = mobius_stack_asString(state, -2);
    mobius_stack_pop(state, 2);

    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &t) != 0) { mobius_stack_pushNil(state); return 1; }
    struct tm* tm = &tm_buf;
#else
    struct tm* tm = ::localtime_r(&t, &tm_buf);
    if (!tm) { mobius_stack_pushNil(state); return 1; }
#endif
    char buf[512];
    size_t n = ::strftime(buf, sizeof(buf), fmt, tm);
    if (n > 0) mobius_stack_pushString(state, buf);
    else       mobius_stack_pushString(state, "");
    return 1;
}

static int os_mktime(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "mktime() expects 1 argument (table)");
    if (!mobius_stack_isTable(state, -1))
        return mobius_error(state, "mktime() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));

    mobius_stack_getTableField(state, tbl, "year");
    tm_val.tm_year = (int)mobius_stack_asInt64(state, -1) - 1900;
    mobius_stack_pop(state, 1);

    mobius_stack_getTableField(state, tbl, "month");
    tm_val.tm_mon = (int)mobius_stack_asInt64(state, -1) - 1;
    mobius_stack_pop(state, 1);

    mobius_stack_getTableField(state, tbl, "day");
    tm_val.tm_mday = (int)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_getTableField(state, tbl, "hour");
    tm_val.tm_hour = (int)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_getTableField(state, tbl, "min");
    tm_val.tm_min = (int)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_getTableField(state, tbl, "sec");
    tm_val.tm_sec = (int)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);

    tm_val.tm_isdst = -1;
    mobius_stack_pop(state, 1);

    time_t result = ::mktime(&tm_val);
    mobius_stack_pushInt64(state, (int64_t)result);
    return 1;
}

// ============================================================================
// EXISTENCE CHECKS
// ============================================================================

static int os_exists(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "exists() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "exists() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    stat_t st;
    mobius_stack_pushBool(state, os_stat_call(path, &st) == 0);
    return 1;
}

static int os_is_file(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "is_file() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "is_file() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    stat_t st;
    mobius_stack_pushBool(state, os_stat_call(path, &st) == 0 && S_ISREG(st.st_mode));
    return 1;
}

static int os_is_dir(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "is_dir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "is_dir() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    stat_t st;
    mobius_stack_pushBool(state, os_stat_call(path, &st) == 0 && S_ISDIR(st.st_mode));
    return 1;
}

// ============================================================================
// PATH UTILITIES
// ============================================================================

static int os_basename(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "basename() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "basename() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

#ifdef _WIN32
    const char* last = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    if (last > path || (*path != '/' && *path != '\\'))
        mobius_stack_pushString(state, last);
    else
        mobius_stack_pushString(state, path);
#else
    char* buf = strdup(path);
    const char* base = ::basename(buf);
    mobius_stack_pushString(state, base);
    free(buf);
#endif
    return 1;
}

static int os_dirname(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "dirname() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "dirname() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

#ifdef _WIN32
    char buf[PATH_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* last_sep = nullptr;
    for (char* p = buf; *p; ++p) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (last_sep) {
        if (last_sep == buf) last_sep[1] = '\0';
        else *last_sep = '\0';
    } else {
        buf[0] = '.'; buf[1] = '\0';
    }
    mobius_stack_pushString(state, buf);
#else
    char* buf = strdup(path);
    const char* dir = ::dirname(buf);
    mobius_stack_pushString(state, dir);
    free(buf);
#endif
    return 1;
}

static int os_extname(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "extname() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "extname() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    const char* dot = strrchr(path, '.');
    const char* sep = strrchr(path, '/');
#ifdef _WIN32
    const char* bsep = strrchr(path, '\\');
    if (bsep && (!sep || bsep > sep)) sep = bsep;
#endif
    if (dot && (!sep || dot > sep)) {
        mobius_stack_pushString(state, dot);
    } else {
        mobius_stack_pushString(state, "");
    }
    return 1;
}

static int os_join(MobiusState* state, int arg_count) {
    if (arg_count < 1)
        return mobius_error(state, "join() expects at least 1 argument");
    for (int i = 1; i <= arg_count; i++) {
        if (!mobius_stack_isString(state, -i))
            return mobius_error(state, "join() expects string arguments");
    }

#ifdef _WIN32
    char sep = '\\';
#else
    char sep = '/';
#endif

    std::string joined;
    for (int i = arg_count; i >= 1; i--) {
        const char* part = mobius_stack_asString(state, -i);
        if (!part || part[0] == '\0') continue;

        if (joined.empty()) {
            joined = part;
            continue;
        }

        bool left_has_sep = joined.back() == '/'
#ifdef _WIN32
                         || joined.back() == '\\'
#endif
            ;
        bool right_has_sep = part[0] == '/'
#ifdef _WIN32
                          || part[0] == '\\'
#endif
            ;

        if (!left_has_sep && !right_has_sep) {
            joined.push_back(sep);
            joined += part;
        } else if (left_has_sep && right_has_sep) {
            joined += (part + 1);
        } else {
            joined += part;
        }
    }

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushString(state, joined.c_str());
    return 1;
}

// ============================================================================
// RECURSIVE MKDIR
// ============================================================================

static bool mkdirp_impl(const char* path) {
    stat_t st;
    if (os_stat_call(path, &st) == 0 && S_ISDIR(st.st_mode)) return true;

#ifdef _WIN32
    char buf[PATH_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* last_sep = nullptr;
    for (char* p = buf; *p; ++p) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (last_sep && last_sep != buf) {
        *last_sep = '\0';
        mkdirp_impl(buf);
    }
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    char* buf = strdup(path);
    char* parent = ::dirname(buf);
    if (strcmp(parent, path) != 0 && strcmp(parent, ".") != 0 && strcmp(parent, "/") != 0) {
        mkdirp_impl(parent);
    }
    free(buf);
    return ::mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

static int os_mkdirp(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "mkdirp() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "mkdirp() expects a string argument");
    const char* path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, mkdirp_impl(path));
    return 1;
}

// ============================================================================
// TIMESTAMP
// ============================================================================

static int os_time(MobiusState* state, int arg_count) {
    (void)arg_count;
    mobius_stack_pushInt64(state, (int64_t)::time(nullptr));
    return 1;
}

// ============================================================================
// POST-INIT: add constants to module table
// ============================================================================

static int os_post_init(MobiusState* state) {
#ifdef __linux__
    const char* platform = "linux";
#elif defined(__APPLE__)
    const char* platform = "darwin";
#elif defined(_WIN32)
    const char* platform = "windows";
#else
    const char* platform = "unknown";
#endif

    mobius_stack_pushString(state, platform);
    mobius_stack_setTableField(state, 0, "platform");

#ifdef _WIN32
    mobius_stack_pushString(state, "\\");
#else
    mobius_stack_pushString(state, "/");
#endif
    mobius_stack_setTableField(state, 0, "separator");

    return 0;
}

// ============================================================================
// PLUGIN DEFINITION
// ============================================================================

static int init_os_plugin(MobiusState* state) {
    (void)state;
    return 0;
}

static void cleanup_os_plugin(void) {}

static MobiusPluginFunction os_functions[] = {
    // Environment
    {"getenv",      os_getenv,      1,  MOBIUS_VAL_STRING,  "Get environment variable"},
    {"setenv",      os_setenv,      2,  MOBIUS_VAL_NIL,     "Set environment variable"},
    {"unsetenv",    os_unsetenv,    1,  MOBIUS_VAL_NIL,     "Unset environment variable"},
    // Working directory
    {"getcwd",      os_getcwd,      0,  MOBIUS_VAL_STRING,  "Get current working directory"},
    {"chdir",       os_chdir,       1,  MOBIUS_VAL_BOOL,    "Change working directory"},
    // Sleep
    {"sleep",       os_sleep,       1,  MOBIUS_VAL_NIL,     "Sleep for fractional seconds"},
    // Process / commands
    {"system",      os_system,      1,  MOBIUS_VAL_INT64,   "Run shell command, return exit code"},
    {"exec",        os_exec,        1,  MOBIUS_VAL_STRING,  "Run command, capture stdout"},
    {"getpid",      os_getpid,      0,  MOBIUS_VAL_INT64,   "Get current process ID"},
    {"getppid",     os_getppid,     0,  MOBIUS_VAL_INT64,   "Get parent process ID when available"},
    {"hostname",    os_hostname_fn, 0,  MOBIUS_VAL_STRING,  "Get local host name"},
    {"executable",  os_executable,  0,  MOBIUS_VAL_STRING,  "Get current executable path"},
    {"env",         os_env,         0,  MOBIUS_VAL_TABLE,   "Get current environment as a table"},
    {"which",       os_which,       1,  MOBIUS_VAL_STRING,  "Resolve an executable on PATH"},
    // Directory ops
    {"listdir",     os_listdir,     1,  MOBIUS_VAL_ARRAY,   "List directory entries"},
    {"mkdir",       os_mkdir,       1,  MOBIUS_VAL_BOOL,    "Create directory"},
    {"rmdir",       os_rmdir,       1,  MOBIUS_VAL_BOOL,    "Remove empty directory"},
    // File ops
    {"remove",      os_remove,      1,  MOBIUS_VAL_BOOL,    "Remove file"},
    {"rename",      os_rename,      2,  MOBIUS_VAL_BOOL,    "Rename/move file"},
    {"cp",          os_cp,          2,  MOBIUS_VAL_BOOL,    "Copy file"},
    {"touch",       os_touch,       1,  MOBIUS_VAL_BOOL,    "Create file or update mtime"},
    // File metadata
    {"stat",        os_stat,        1,  MOBIUS_VAL_TABLE,   "Get file status (table)"},
    {"chmod",       os_chmod,       2,  MOBIUS_VAL_BOOL,    "Change file permissions"},
    {"filesize",    os_filesize,    1,  MOBIUS_VAL_INT64,   "Get file size in bytes"},
    // Links and paths
    {"link",        os_link,        2,  MOBIUS_VAL_BOOL,    "Create hard link"},
    {"symlink",     os_symlink,     2,  MOBIUS_VAL_BOOL,    "Create symbolic link"},
    {"realpath",    os_realpath,    1,  MOBIUS_VAL_STRING,  "Resolve symlinks to absolute path"},
    // Temp
    {"tmpdir",      os_tmpdir,      0,  MOBIUS_VAL_STRING,  "Get temp directory path"},
    {"tmpfile",     os_tmpfile,     0,  MOBIUS_VAL_STRING,  "Create temp file, return path"},
    // Glob / walk
    {"glob",        os_glob,        1,  MOBIUS_VAL_ARRAY,   "List files matching glob pattern"},
    {"walkdir",     os_walkdir,     1,  MOBIUS_VAL_ARRAY,   "Recursively list directory contents"},
    // System info
    {"uname",       os_uname,       0,  MOBIUS_VAL_TABLE,   "Get OS/kernel info table"},
    {"cpu_count",   os_cpu_count,   0,  MOBIUS_VAL_INT64,   "Get number of CPU cores"},
    // Existence checks
    {"exists",      os_exists,      1,  MOBIUS_VAL_BOOL,    "Check if path exists"},
    {"is_file",     os_is_file,     1,  MOBIUS_VAL_BOOL,    "Check if path is a regular file"},
    {"is_dir",      os_is_dir,      1,  MOBIUS_VAL_BOOL,    "Check if path is a directory"},
    // Path utilities
    {"basename",    os_basename,    1,  MOBIUS_VAL_STRING,  "Get filename from path"},
    {"dirname",     os_dirname,     1,  MOBIUS_VAL_STRING,  "Get directory from path"},
    {"extname",     os_extname,     1,  MOBIUS_VAL_STRING,  "Get file extension (incl. dot)"},
    {"join",        os_join,        2,  MOBIUS_VAL_STRING,  "Join two path components"},
    // Recursive mkdir
    {"mkdirp",      os_mkdirp,      1,  MOBIUS_VAL_BOOL,    "Create directory recursively"},
    // Timestamp
    {"time",        os_time,        0,  MOBIUS_VAL_INT64,   "Current Unix timestamp"},
    // Datetime
    {"gmtime",      os_gmtime,      1,  MOBIUS_VAL_TABLE,   "Convert timestamp to UTC time table"},
    {"localtime",   os_localtime,   1,  MOBIUS_VAL_TABLE,   "Convert timestamp to local time table"},
    {"strftime",    os_strftime,    2,  MOBIUS_VAL_STRING,  "Format timestamp with strftime"},
    {"mktime",      os_mktime,      1,  MOBIUS_VAL_INT64,   "Convert time table to timestamp"},
};

static MobiusPlugin os_plugin = {
    .metadata = {
        .name = "os",
        .version = "1.0.0",
        .description = "Operating System Interface",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = os_functions,
    .function_count = sizeof(os_functions) / sizeof(os_functions[0]),
    .init_plugin = init_os_plugin,
    .cleanup_plugin = cleanup_os_plugin,
    .post_init = os_post_init,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &os_plugin;
}
