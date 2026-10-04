// os: the operating system interface: environment, working directory,
// files and directories, paths, processes and time. The OS calls are in
// os_platform.h.

#include <mobius/mobius_plugin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "os_platform.h"
#include "modules/internal/module_platform.h"

static void push_string(MobiusState* state, const std::string& s) {
    mobius_stack_pushString(state, s.c_str());
}

static void push_string_array(MobiusState* state, const std::vector<std::string>& items) {
    mobius_stack_pushNewArray(state, items.size());
    int arr_idx = mobius_stack_size(state) - 1;
    for (const std::string& item : items) {
        push_string(state, item);
        mobius_stack_arrayPush(state, arr_idx);
    }
}

// ============================================================================
// ENVIRONMENT VARIABLES
// ============================================================================

static int os_getenv(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "getenv() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "getenv() expects a string argument");
    std::string name = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string value;
    if (os_platform_getenv(name, &value)) push_string(state, value);
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_setenv(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 2)
        return mobius_error(state, "setenv() expects 2 arguments");
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, "setenv() expects string arguments");
    std::string value = mobius_stack_asString(state, -1);
    std::string name  = mobius_stack_asString(state, -2);
    bool ok = os_platform_setenv(name, value);
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, ok);
    return 1;
}

static int os_unsetenv(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "unsetenv() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "unsetenv() expects a string argument");
    std::string name = mobius_stack_asString(state, -1);
    bool ok = os_platform_unsetenv(name);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, ok);
    return 1;
}

static int os_env(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    std::vector<std::pair<std::string, std::string>> vars;
    os_platform_environment(&vars);
    mobius_stack_pushNewTable(state, 32);
    int tbl = mobius_stack_size(state) - 1;
    for (const auto& kv : vars) {
        push_string(state, kv.second);
        mobius_stack_setTableField(state, tbl, kv.first.c_str());
    }
    return 1;
}

// ============================================================================
// WORKING DIRECTORY
// ============================================================================

static int os_getcwd(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    std::string path;
    if (os_platform_getcwd(&path)) push_string(state, path);
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_chdir(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "chdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "chdir() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    bool ok = os_platform_chdir(path);
    mobius_stack_pop(state, 1);
    if (!ok)
        return mobius_error(state, "chdir() failed");
    mobius_stack_pushBool(state, true);
    return 1;
}

// ============================================================================
// SLEEP
// ============================================================================

static int os_sleep(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "sleep() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "sleep() expects a numeric argument");
    double seconds = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    if (seconds > 0.0) os_platform_sleep(seconds);
    mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// PROCESS / COMMAND EXECUTION
// ============================================================================

static int os_system(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "system() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "system() expects a string argument");
    std::string cmd = mobius_stack_asString(state, -1);
    int64_t code = os_platform_system(cmd);
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, code);
    return 1;
}

static int os_exec(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "exec() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "exec() expects a string argument");
    std::string cmd = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string output;
    if (os_platform_exec(cmd, &output)) mobius_stack_pushString(state, output.c_str());
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_getpid(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    mobius_stack_pushInt64(state, os_platform_getpid());
    return 1;
}

static int os_getppid(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    int64_t pid = 0;
    if (os_platform_getppid(&pid)) mobius_stack_pushInt64(state, pid);
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_hostname_fn(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    std::string hostname;
    if (os_platform_hostname(&hostname)) push_string(state, hostname);
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_executable(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    std::string path;
    if (os_platform_executable(&path)) push_string(state, path);
    else mobius_stack_pushNil(state);
    return 1;
}

static int os_which(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "which() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "which() expects a string argument");
    std::string name = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string resolved;
    if (os_platform_which(name, &resolved)) push_string(state, resolved);
    else mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

static int os_listdir(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "listdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "listdir() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::vector<std::string> names;
    os_platform_listdir(path, &names);   // an unreadable directory lists as empty
    push_string_array(state, names);
    return 1;
}

// Calls a path operation taking one string argument and pushes its result.
static int path_op(MobiusState* state, int arg_count, const char* name, bool (*op)(const std::string&)) {
    if (arg_count != 1)
        return mobius_error(state, (std::string(name) + "() expects 1 argument").c_str());
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, (std::string(name) + "() expects a string argument").c_str());
    std::string path = mobius_stack_asString(state, -1);
    bool ok = op(path);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, ok);
    return 1;
}

// The same for two string arguments (first, second).
static int path_op2(MobiusState* state, int arg_count, const char* name,
                    bool (*op)(const std::string&, const std::string&)) {
    if (arg_count != 2)
        return mobius_error(state, (std::string(name) + "() expects 2 arguments").c_str());
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2))
        return mobius_error(state, (std::string(name) + "() expects string arguments").c_str());
    std::string second = mobius_stack_asString(state, -1);
    std::string first = mobius_stack_asString(state, -2);
    bool ok = op(first, second);
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, ok);
    return 1;
}

static int os_mkdir(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op(state, arg_count, "mkdir", os_platform_mkdir);
}

static int os_rmdir(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op(state, arg_count, "rmdir", os_platform_rmdir);
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

static int os_remove(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op(state, arg_count, "remove", os_platform_remove);
}

static int os_rename(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op2(state, arg_count, "rename", os_platform_rename);
}

static int os_cp(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op2(state, arg_count, "cp", os_platform_copy_file);
}

static int os_touch(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op(state, arg_count, "touch", os_platform_touch);
}

// ============================================================================
// FILE METADATA
// ============================================================================

static int os_stat(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "stat() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "stat() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    OsStat st;
    if (!os_platform_stat(path, &st)) {
        mobius_stack_pushNil(state);
        return 1;
    }

    mobius_stack_pushNewTable(state, 12);
    int tbl = mobius_stack_size(state) - 1;
    push_string(state, path);
    mobius_stack_setTableField(state, tbl, "path");
    mobius_stack_pushInt64(state, st.size);
    mobius_stack_setTableField(state, tbl, "size");
    mobius_stack_pushInt64(state, st.mtime);
    mobius_stack_setTableField(state, tbl, "mtime");
    mobius_stack_pushInt64(state, st.atime);
    mobius_stack_setTableField(state, tbl, "atime");
    mobius_stack_pushInt64(state, st.ctime);
    mobius_stack_setTableField(state, tbl, "ctime");
    mobius_stack_pushInt64(state, st.mode);
    mobius_stack_setTableField(state, tbl, "mode");
    mobius_stack_pushBool(state, st.is_dir);
    mobius_stack_setTableField(state, tbl, "is_dir");
    mobius_stack_pushBool(state, st.is_file);
    mobius_stack_setTableField(state, tbl, "is_file");
    mobius_stack_pushBool(state, st.is_link);
    mobius_stack_setTableField(state, tbl, "is_link");

    const bool is_other = !st.is_dir && !st.is_file && !st.is_link;
    mobius_stack_pushString(state, st.is_dir ? "dir" : st.is_file ? "file" : st.is_link ? "link" : "other");
    mobius_stack_setTableField(state, tbl, "type");
    mobius_stack_pushBool(state, is_other);
    mobius_stack_setTableField(state, tbl, "is_other");
    mobius_stack_pushBool(state, st.readonly);
    mobius_stack_setTableField(state, tbl, "readonly");
    return 1;
}

static int os_chmod(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 2)
        return mobius_error(state, "chmod() expects 2 arguments");
    if (!mobius_stack_isInteger(state, -1))
        return mobius_error(state, "chmod() mode must be an integer");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "chmod() path must be a string");
    int64_t mode = mobius_stack_asInt64(state, -1);
    std::string path = mobius_stack_asString(state, -2);
    bool ok = os_platform_chmod(path, mode);
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, ok);
    return 1;
}

static int os_filesize(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "filesize() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "filesize() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    OsStat st;
    if (os_platform_stat(path, &st)) mobius_stack_pushInt64(state, st.size);
    else mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// LINKS AND PATHS
// ============================================================================

// link(target, linkpath) and symlink(target, linkpath)
static int os_link(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op2(state, arg_count, "link", os_platform_link);
}

static int os_symlink(MobiusState* state, int arg_count, void* /*userdata*/) {
    return path_op2(state, arg_count, "symlink", os_platform_symlink);
}

static int os_realpath(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "realpath() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "realpath() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string resolved;
    if (os_platform_realpath(path, &resolved)) push_string(state, resolved);
    else mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// TEMP FILES AND DIRECTORIES
// ============================================================================

static int os_tmpdir(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    push_string(state, os_platform_tmpdir());
    return 1;
}

static int os_tmpfile(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    std::string path;
    if (os_platform_tmpfile(&path)) push_string(state, path);
    else mobius_stack_pushNil(state);
    return 1;
}

// ============================================================================
// GLOB AND DIRECTORY WALKING
// ============================================================================

static int os_glob(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "glob() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "glob() expects a string argument");
    std::string pattern = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::vector<std::string> paths;
    os_platform_glob(pattern, &paths);
    push_string_array(state, paths);
    return 1;
}

static int os_walkdir(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "walkdir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "walkdir() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::vector<std::string> paths;
    os_platform_walkdir(path, &paths);
    push_string_array(state, paths);
    return 1;
}

// ============================================================================
// SYSTEM INFO
// ============================================================================

static int os_uname(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    mobius_stack_pushNewTable(state, 8);
    int tbl = mobius_stack_size(state) - 1;
    OsUname info;
    if (os_platform_uname(&info)) {
        push_string(state, info.sysname);
        mobius_stack_setTableField(state, tbl, "sysname");
        push_string(state, info.nodename);
        mobius_stack_setTableField(state, tbl, "nodename");
        push_string(state, info.release);
        mobius_stack_setTableField(state, tbl, "release");
        push_string(state, info.version);
        mobius_stack_setTableField(state, tbl, "version");
        push_string(state, info.machine);
        mobius_stack_setTableField(state, tbl, "machine");
    }
    return 1;
}

static int os_cpu_count(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    mobius_stack_pushInt64(state, os_platform_cpu_count());
    return 1;
}

// ============================================================================
// DATETIME
// ============================================================================

static void push_tm_table(MobiusState* state, const struct tm* tm) {
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
}

static int os_gmtime(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "gmtime() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "gmtime() expects a numeric argument");
    int64_t t = mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);
    struct tm tm_buf;
    if (!module_platform_gmtime(t, &tm_buf)) { mobius_stack_pushNil(state); return 1; }
    push_tm_table(state, &tm_buf);
    return 1;
}

static int os_localtime(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "localtime() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "localtime() expects a numeric argument");
    int64_t t = mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);
    struct tm tm_buf;
    if (!module_platform_localtime(t, &tm_buf)) { mobius_stack_pushNil(state); return 1; }
    push_tm_table(state, &tm_buf);
    return 1;
}

static int os_strftime(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 2)
        return mobius_error(state, "strftime() expects 2 arguments");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "strftime() timestamp must be a number");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "strftime() format must be a string");
    int64_t t = mobius_stack_asInt64(state, -1);
    std::string fmt = mobius_stack_asString(state, -2);
    mobius_stack_pop(state, 2);
    struct tm tm_buf;
    if (!module_platform_localtime(t, &tm_buf)) { mobius_stack_pushNil(state); return 1; }
    char buf[512];
    size_t n = ::strftime(buf, sizeof(buf), fmt.c_str(), &tm_buf);
    mobius_stack_pushString(state, n > 0 ? buf : "");
    return 1;
}

static int os_mktime(MobiusState* state, int arg_count, void* /*userdata*/) {
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

static int os_exists(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "exists() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "exists() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    OsStat st;
    mobius_stack_pushBool(state, os_platform_stat(path, &st));
    return 1;
}

static int os_is_file(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "is_file() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "is_file() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    OsStat st;
    mobius_stack_pushBool(state, os_platform_stat(path, &st) && st.is_file);
    return 1;
}

static int os_is_dir(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "is_dir() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "is_dir() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    OsStat st;
    mobius_stack_pushBool(state, os_platform_stat(path, &st) && st.is_dir);
    return 1;
}

// ============================================================================
// PATH UTILITIES
// ============================================================================

static int os_basename(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "basename() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "basename() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    push_string(state, os_platform_basename(path));
    return 1;
}

static int os_dirname(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "dirname() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "dirname() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    push_string(state, os_platform_dirname(path));
    return 1;
}

static int os_extname(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "extname() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "extname() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    size_t dot = path.rfind('.');
    size_t sep = std::string::npos;
    for (size_t i = path.size(); i > 0; i--) {
        if (os_platform_is_separator(path[i - 1])) { sep = i - 1; break; }
    }
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep))
        push_string(state, path.substr(dot));
    else
        mobius_stack_pushString(state, "");
    return 1;
}

static int os_join(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 1)
        return mobius_error(state, "join() expects at least 1 argument");
    for (int i = 1; i <= arg_count; i++) {
        if (!mobius_stack_isString(state, -i))
            return mobius_error(state, "join() expects string arguments");
    }

    std::string joined;
    for (int i = arg_count; i >= 1; i--) {
        const char* part = mobius_stack_asString(state, -i);
        if (!part || part[0] == '\0') continue;

        if (joined.empty()) {
            joined = part;
            continue;
        }

        bool left_has_sep = os_platform_is_separator(joined.back());
        bool right_has_sep = os_platform_is_separator(part[0]);
        if (!left_has_sep && !right_has_sep) {
            joined.push_back(os_platform_separator());
            joined += part;
        } else if (left_has_sep && right_has_sep) {
            joined += (part + 1);
        } else {
            joined += part;
        }
    }

    mobius_stack_pop(state, arg_count);
    push_string(state, joined);
    return 1;
}

// ============================================================================
// RECURSIVE MKDIR
// ============================================================================

static bool is_directory(const std::string& path) {
    OsStat st;
    return os_platform_stat(path, &st) && st.is_dir;
}

static bool mkdirp_impl(const std::string& path) {
    if (is_directory(path)) return true;
    std::string parent = os_platform_dirname(path);
    if (parent != path && parent != "." && !is_directory(parent)) mkdirp_impl(parent);
    return os_platform_mkdir(path) || is_directory(path);
}

static int os_mkdirp(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count != 1)
        return mobius_error(state, "mkdirp() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "mkdirp() expects a string argument");
    std::string path = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, mkdirp_impl(path));
    return 1;
}

// ============================================================================
// TIMESTAMP
// ============================================================================

static int os_time(MobiusState* state, int arg_count, void* /*userdata*/) {
    (void)arg_count;
    mobius_stack_pushInt64(state, (int64_t)::time(nullptr));
    return 1;
}

// ============================================================================
// POST-INIT: add constants to module table
// ============================================================================

static int os_post_init(MobiusState* state) {
    mobius_stack_pushString(state, os_platform_name());
    mobius_stack_setTableField(state, 0, "platform");
    char sep[2] = {os_platform_separator(), '\0'};
    mobius_stack_pushString(state, sep);
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
    {
        "os" /* name */,
        "1.0.0" /* version */,
        "Operating System Interface" /* description */,
        "Mobius Team" /* author */,
        MOBIUS_PLUGIN_API_VERSION /* api_version */,
        "MIT" /* license */,
        nullptr /* depends_on */,
        0 /* depends_on_count */
    },
    os_functions /* functions */,
    sizeof(os_functions) / sizeof(os_functions[0]) /* function_count */,
    init_os_plugin /* init_plugin */,
    cleanup_os_plugin /* cleanup_plugin */,
    os_post_init /* post_init */
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &os_plugin;
}
