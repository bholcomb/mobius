// Linux and macOS implementation of os_platform.h. What differs between
// them (the executable's path, the environment, the platform name) is in
// os_platform_linux.cpp and os_platform_macos.cpp.

#include "os_platform.h"
#include "os_platform_posix.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------

bool os_platform_getenv(const std::string& name, std::string* value) {
    const char* v = ::getenv(name.c_str());
    if (!v) return false;
    *value = v;
    return true;
}

bool os_platform_setenv(const std::string& name, const std::string& value) {
    return ::setenv(name.c_str(), value.c_str(), 1) == 0;
}

bool os_platform_unsetenv(const std::string& name) {
    return ::unsetenv(name.c_str()) == 0;
}

void os_platform_environment(std::vector<std::pair<std::string, std::string>>* vars) {
    for (char** p = posix_environ(); p && *p; ++p) {
        std::string entry(*p);
        size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        vars->push_back({entry.substr(0, eq), entry.substr(eq + 1)});
    }
}

// ---------------------------------------------------------------------------
// Working directory, sleeping, commands
// ---------------------------------------------------------------------------

bool os_platform_getcwd(std::string* path) {
    char buf[PATH_MAX];
    if (!::getcwd(buf, sizeof(buf))) return false;
    *path = buf;
    return true;
}

bool os_platform_chdir(const std::string& path) {
    return ::chdir(path.c_str()) == 0;
}

void os_platform_sleep(double seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

int64_t os_platform_system(const std::string& command) {
    int rc = ::system(command.c_str());
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool os_platform_exec(const std::string& command, std::string* output) {
    FILE* fp = popen(command.c_str(), "r");
    if (!fp) return false;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) *output += buf;
    pclose(fp);
    return true;
}

// ---------------------------------------------------------------------------
// Process and system
// ---------------------------------------------------------------------------

int64_t os_platform_getpid() { return (int64_t)::getpid(); }

bool os_platform_getppid(int64_t* pid) {
    *pid = (int64_t)::getppid();
    return true;
}

bool os_platform_hostname(std::string* name) {
    char hostname[256] = {0};
    if (::gethostname(hostname, sizeof(hostname)) != 0) return false;
    hostname[sizeof(hostname) - 1] = '\0';
    *name = hostname;
    return true;
}

int64_t os_platform_cpu_count() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int64_t)n : 1;
}

bool os_platform_uname(OsUname* info) {
    struct utsname un;
    if (::uname(&un) != 0) return false;
    info->sysname = un.sysname;
    info->nodename = un.nodename;
    info->release = un.release;
    info->version = un.version;
    info->machine = un.machine;
    return true;
}

static bool is_executable_file(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

bool os_platform_which(const std::string& name, std::string* path) {
    if (name.empty()) return false;
    if (name.find('/') != std::string::npos) {
        if (!is_executable_file(name)) return false;
        *path = name;
        return true;
    }
    const char* path_env = ::getenv("PATH");
    if (!path_env || !*path_env) return false;
    std::string list(path_env);
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        std::string dir = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (dir.empty()) dir = ".";
        std::string candidate = dir.back() == '/' ? dir + name : dir + "/" + name;
        if (is_executable_file(candidate)) {
            *path = candidate;
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Directories and files
// ---------------------------------------------------------------------------

bool os_platform_listdir(const std::string& path, std::vector<std::string>* names) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        names->push_back(entry->d_name);
    }
    closedir(dir);
    return true;
}

bool os_platform_mkdir(const std::string& path) { return ::mkdir(path.c_str(), 0755) == 0; }

bool os_platform_rmdir(const std::string& path) { return ::rmdir(path.c_str()) == 0; }

bool os_platform_remove(const std::string& path) { return ::remove(path.c_str()) == 0; }

bool os_platform_rename(const std::string& from, const std::string& to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
}

bool os_platform_copy_file(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

bool os_platform_touch(const std::string& path) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_NOCTTY | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    struct timespec times[2] = {{0, UTIME_NOW}, {0, UTIME_NOW}};
    futimens(fd, times);
    close(fd);
    return true;
}

bool os_platform_chmod(const std::string& path, int64_t mode) {
    return ::chmod(path.c_str(), (mode_t)mode) == 0;
}

bool os_platform_link(const std::string& target, const std::string& link_path) {
    return ::link(target.c_str(), link_path.c_str()) == 0;
}

bool os_platform_symlink(const std::string& target, const std::string& link_path) {
    return ::symlink(target.c_str(), link_path.c_str()) == 0;
}

bool os_platform_realpath(const std::string& path, std::string* resolved) {
    char buf[PATH_MAX];
    if (!::realpath(path.c_str(), buf)) return false;
    *resolved = buf;
    return true;
}

bool os_platform_stat(const std::string& path, OsStat* out) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    out->size = (int64_t)st.st_size;
    out->mtime = (int64_t)st.st_mtime;
    out->atime = (int64_t)st.st_atime;
    out->ctime = (int64_t)st.st_ctime;
    out->mode = (int64_t)st.st_mode;
    out->is_dir = S_ISDIR(st.st_mode);
    out->is_file = S_ISREG(st.st_mode);
    out->is_link = S_ISLNK(st.st_mode);
    out->readonly = (st.st_mode & 0222) == 0;
    return true;
}

// ---------------------------------------------------------------------------
// Temporary files, glob, walk
// ---------------------------------------------------------------------------

std::string os_platform_tmpdir() {
    const char* tmp = ::getenv("TMPDIR");
    return tmp && *tmp ? tmp : "/tmp";
}

bool os_platform_tmpfile(std::string* path) {
    std::string tmpl = os_platform_tmpdir() + "/mobius_XXXXXX";
    int fd = mkstemp(&tmpl[0]);
    if (fd < 0) return false;
    close(fd);
    *path = tmpl;
    return true;
}

void os_platform_glob(const std::string& pattern, std::vector<std::string>* paths) {
    glob_t gl;
    if (::glob(pattern.c_str(), GLOB_NOSORT, nullptr, &gl) != 0) return;
    for (size_t i = 0; i < gl.gl_pathc; i++) paths->push_back(gl.gl_pathv[i]);
    globfree(&gl);
}

void os_platform_walkdir(const std::string& path, std::vector<std::string>* paths) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        std::string full = path + "/" + entry->d_name;
        paths->push_back(full);
        struct stat st;
        if (::stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) os_platform_walkdir(full, paths);
    }
    closedir(dir);
}

// ---------------------------------------------------------------------------
// Path syntax
// ---------------------------------------------------------------------------

char os_platform_separator() { return '/'; }

bool os_platform_is_separator(char c) { return c == '/'; }

std::string os_platform_basename(const std::string& path) {
    std::string buf = path;
    return ::basename(&buf[0]);
}

std::string os_platform_dirname(const std::string& path) {
    std::string buf = path;
    return ::dirname(&buf[0]);
}
