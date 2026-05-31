#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <string>
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <limits.h>
#else
#  include <unistd.h>
#  include <limits.h>
#endif

// Directory containing the running executable, or empty on failure.
static std::string executable_dir() {
    std::string path;
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return std::string();
    path.assign(buf, len);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string tmp(size + 1, '\0');
    if (_NSGetExecutablePath(&tmp[0], &size) != 0) return std::string();
    path.assign(tmp.c_str());
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) return std::string();
    buf[len] = '\0';
    path.assign(buf);
#endif
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return std::string();
    return path.substr(0, slash);
}

// Register module search directories: bundled modules live next to the
// executable (<exedir>/modules), and MOBIUS_MODULE_PATH (a PATH-style list)
// can append more.
static void register_module_directories(MobiusState* state) {
    std::string exedir = executable_dir();
    if (!exedir.empty()) {
        std::string mods = exedir + "/modules";
        mobius_add_plugin_directory(state, mods.c_str());
    }

    const char* env_path = getenv("MOBIUS_MODULE_PATH");
    if (env_path && *env_path) {
#if defined(_WIN32)
        const char list_sep = ';';
#else
        const char list_sep = ':';
#endif
        std::string s(env_path);
        size_t start = 0;
        while (start <= s.size()) {
            size_t end = s.find(list_sep, start);
            if (end == std::string::npos) end = s.size();
            std::string dir = s.substr(start, end - start);
            if (!dir.empty()) mobius_add_plugin_directory(state, dir.c_str());
            start = end + 1;
        }
    }
}

int execute_file(MobiusState* state, const char* filename) {
    if (!state || !filename) {
        fprintf(stderr, "Invalid arguments to execute_file\n");
        return 1;
    }
    
    int result = mobius_exec_file(state, filename);
    return (result != MOBIUS_OK) ? 1 : 0;
}

static void register_cli_argv(MobiusState* state, int argc, char* argv[], int arg_start) {
    if (!state) return;
    if (arg_start < 0) arg_start = 0;
    if (arg_start > argc) arg_start = argc;

    mobius_stack_pushNewArray(state, (size_t)(argc - arg_start));
    int arr_idx = mobius_stack_size(state) - 1;
    for (int i = arg_start; i < argc; i++) {
        mobius_stack_pushString(state, argv[i]);
        mobius_stack_arrayPush(state, arr_idx);
    }
    mobius_stack_setGlobal(state, "argv");
    mobius_set_global_readonly(state, "argv", true);
}

int main(int argc, char *argv[]) {
    const char* script_file = NULL;
    bool debug_mode = false;
    int script_arg_start = argc;
    
    for (int i = 1; i < argc; i++) {
        if (!script_file && (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0)) {
            debug_mode = true;
        } else if (!script_file && (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file] [script_args...]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --debug, -d        Enable debug mode\n");
            printf("  --help, -h         Show this help message\n");
            printf("\nIf a script file is provided, remaining positional arguments are exposed\n");
            printf("to the script via the global argv array.\n");
            printf("\nIf no script file is provided, starts interactive REPL.\n");
            return 0;
        } else if (!script_file) {
            script_file = argv[i];
            script_arg_start = i + 1;
        } else {
            break;
        }
    }
    
    MobiusConfig config = mobius_default_config();
    if (debug_mode) {
        config.debug_mode = true;
    }
    
    MobiusState* state = mobius_new_state(&config);
    if (!state) {
        fprintf(stderr, "Failed to create Mobius state\n");
        return 1;
    }
    
    register_module_directories(state);

    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        fprintf(stderr, "Failed to initialize standard library\n");
        mobius_free_state(state);
        return 1;
    }

    register_cli_argv(state, argc, argv, script_file ? script_arg_start : argc);
    
    int result = 0;
    
    if (script_file) {
        result = execute_file(state, script_file);
    } else {
        printf("Mobius Scripting Language Interpreter v0.1.0\n");
        mobius_start_repl(state);
    }
    
    mobius_free_state(state);
    
    return result;
}
