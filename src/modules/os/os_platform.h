#ifndef MOBIUS_MODULES_OS_PLATFORM_H
#define MOBIUS_MODULES_OS_PLATFORM_H

// What the os module needs from the operating system. The build compiles
// the implementation for its target:
//
//   Linux    os_platform_posix.cpp + os_platform_linux.cpp
//   macOS    os_platform_posix.cpp + os_platform_macos.cpp
//   Windows  os_platform_win32.cpp
//
// Paths and strings are UTF-8. Functions returning bool report success.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Environment
bool os_platform_getenv(const std::string& name, std::string* value);
bool os_platform_setenv(const std::string& name, const std::string& value);
bool os_platform_unsetenv(const std::string& name);
void os_platform_environment(std::vector<std::pair<std::string, std::string>>* vars);

// Working directory
bool os_platform_getcwd(std::string* path);
bool os_platform_chdir(const std::string& path);

// Block the calling thread.
void os_platform_sleep(double seconds);

// Commands through the platform's shell. system: the exit code (-1 if it
// could not run). exec: the command's standard output.
int64_t os_platform_system(const std::string& command);
bool os_platform_exec(const std::string& command, std::string* output);

// Process and system
int64_t os_platform_getpid();
bool os_platform_getppid(int64_t* pid);
bool os_platform_hostname(std::string* name);
bool os_platform_executable(std::string* path);
const char* os_platform_name();   // "linux-x86_64", "windows-x86_64", ...
int64_t os_platform_cpu_count();

struct OsUname {
    std::string sysname, nodename, release, version, machine;
};
bool os_platform_uname(OsUname* info);

// An executable's path: a name with a separator as given, otherwise looked
// up in PATH (with PATHEXT's extensions on Windows).
bool os_platform_which(const std::string& name, std::string* path);

// Directories and files
bool os_platform_listdir(const std::string& path, std::vector<std::string>* names);
bool os_platform_mkdir(const std::string& path);
bool os_platform_rmdir(const std::string& path);
bool os_platform_remove(const std::string& path);
bool os_platform_rename(const std::string& from, const std::string& to);
bool os_platform_copy_file(const std::string& from, const std::string& to);
bool os_platform_touch(const std::string& path);
bool os_platform_chmod(const std::string& path, int64_t mode);
bool os_platform_link(const std::string& target, const std::string& link_path);
bool os_platform_symlink(const std::string& target, const std::string& link_path);
bool os_platform_realpath(const std::string& path, std::string* resolved);

struct OsStat {
    int64_t size = 0;
    int64_t mtime = 0, atime = 0, ctime = 0;
    int64_t mode = 0;
    bool is_dir = false, is_file = false, is_link = false;
    bool readonly = false;
};
bool os_platform_stat(const std::string& path, OsStat* st);

// Temporary files
std::string os_platform_tmpdir();
bool os_platform_tmpfile(std::string* path);   // creates it

// Paths matching a pattern; everything under a directory, recursively.
void os_platform_glob(const std::string& pattern, std::vector<std::string>* paths);
void os_platform_walkdir(const std::string& path, std::vector<std::string>* paths);

// Path syntax
char os_platform_separator();             // '/' or '\\'
bool os_platform_is_separator(char c);    // also '/' on Windows
std::string os_platform_basename(const std::string& path);
std::string os_platform_dirname(const std::string& path);

#endif // MOBIUS_MODULES_OS_PLATFORM_H
