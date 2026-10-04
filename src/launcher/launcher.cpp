// mobius-launcher: the front of a standalone bundle (tools/bundle.mob).
//
// A bundle is this program with an archive appended: the Mobius core
// library, the modules the application uses (as installed, native
// libraries included), its scripts and data files, followed by an index and
// a fixed-size trailer. This program links nothing from Mobius. It
// extracts the archive once to a per-bundle cache directory (native
// libraries must be files to be loaded), loads the core library from there,
// and runs the bundle's entry script with the command-line arguments.
//
// Archive layout (all integers little-endian):
//   [this executable][file data ...][index][trailer]
//   index entry: u32 path_length, path bytes, u64 offset, u64 size, u32 mode
//   trailer (64 bytes): "MOBIUS-BUNDLE-01" (16), u64 index_offset,
//                       u64 index_size, u64 file_count, char hash[24]
// Offsets are from the start of the file. The hash names the cache
// directory, so a changed bundle never reuses an old extraction.

#if !defined(__linux__)
// Standalone bundles are Linux-only for now: elsewhere this program only
// says so (it is still built, as a target of every platform).
#include <cstdio>
int main() {
    fprintf(stderr, "mobius bundle: standalone bundles are supported on Linux only for now\n");
    return 1;
}
#else

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char kMagic[16] = {'M','O','B','I','U','S','-','B','U','N','D','L','E','-','0','1'};
const size_t kTrailerSize = 64;

struct Entry {
    std::string path;
    uint64_t offset = 0, size = 0;
    uint32_t mode = 0644;
};

[[noreturn]] void die(const std::string& message) {
    fprintf(stderr, "mobius bundle: %s\n", message.c_str());
    exit(1);
}

uint64_t read_u64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

uint32_t read_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

std::string self_path() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) die("cannot locate the executable");
    buf[n] = '\0';
    return buf;
}

bool read_at(FILE* f, uint64_t offset, void* out, size_t size) {
    return fseeko(f, (off_t)offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}

bool mkdirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            std::string part = path.substr(0, i);
            if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string cache_root() {
    if (const char* c = getenv("MOBIUS_BUNDLE_CACHE")) if (*c) return c;
    if (const char* x = getenv("XDG_CACHE_HOME")) if (*x) return std::string(x) + "/mobius/bundles";
    if (const char* h = getenv("HOME")) if (*h) return std::string(h) + "/.cache/mobius/bundles";
    return "/tmp/mobius-bundles-" + std::to_string(getuid());
}

// Paths come from the bundle's own index; refuse anything that would land
// outside the cache directory.
bool safe_path(const std::string& p) {
    if (p.empty() || p[0] == '/') return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t slash = p.find('/', start);
        std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

// Extract every entry into a fresh directory, then rename it into place:
// a concurrent first run of the same bundle either wins the rename or finds
// the finished directory.
void extract(FILE* f, const std::vector<Entry>& entries, const std::string& dir) {
    std::string tmp = dir + ".tmp-" + std::to_string(getpid());
    if (!mkdirs(tmp)) die("cannot create " + tmp + ": " + strerror(errno));
    std::vector<char> chunk(1 << 16);
    for (const Entry& e : entries) {
        std::string out_path = tmp + "/" + e.path;
        size_t slash = out_path.rfind('/');
        if (!mkdirs(out_path.substr(0, slash))) die("cannot create directories for " + e.path);
        FILE* out = fopen(out_path.c_str(), "wb");
        if (!out) die("cannot write " + out_path + ": " + strerror(errno));
        if (fseeko(f, (off_t)e.offset, SEEK_SET) != 0) die("corrupt bundle (bad offset)");
        uint64_t left = e.size;
        while (left > 0) {
            size_t n = left < chunk.size() ? (size_t)left : chunk.size();
            if (fread(chunk.data(), 1, n, f) != n) die("corrupt bundle (short data)");
            if (fwrite(chunk.data(), 1, n, out) != n) die("cannot write " + out_path);
            left -= n;
        }
        fclose(out);
        chmod(out_path.c_str(), (mode_t)(e.mode & 0777));
    }
    FILE* done = fopen((tmp + "/.complete").c_str(), "wb");
    if (done) fclose(done);
    if (rename(tmp.c_str(), dir.c_str()) != 0) {
        // Another run finished first: use its copy, and remove our files
        // (best effort; empty directories may remain).
        if (!exists(dir + "/.complete")) die("cannot install " + dir + ": " + strerror(errno));
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) unlink((tmp + "/" + it->path).c_str());
        unlink((tmp + "/.complete").c_str());
    }
}

// The C API functions the launcher uses, looked up in the loaded core.
struct Api {
    void (*default_config_into)(void* out);
    void* (*new_state)(void* config);
    int (*init_stdlib)(void* state);
    void (*free_state)(void* state);
    void (*add_plugin_directory)(void* state, const char* path);
    void (*set_exit_handler)(void* state, void (*handler)(void*, int, void*), void* userdata);
    void (*push_new_array)(void* state, size_t capacity);
    void (*push_string)(void* state, const char* s);
    void (*array_push)(void* state, int idx);
    int (*stack_size)(void* state);
    void (*set_global)(void* state, const char* name);
    void (*set_global_readonly)(void* state, const char* name, bool readonly);
    int (*exec_file)(void* state, const char* path);
};

template <typename T>
void bind(void* lib, T& fn, const char* name) {
    fn = reinterpret_cast<T>(dlsym(lib, name));
    if (!fn) die(std::string("the bundled core library lacks ") + name);
}

void on_exit_call(void*, int code, void*) {
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string exe = self_path();
    FILE* f = fopen(exe.c_str(), "rb");
    if (!f) die("cannot read " + exe);

    // Trailer
    if (fseeko(f, 0, SEEK_END) != 0) die("cannot read " + exe);
    off_t file_size = ftello(f);
    unsigned char trailer[kTrailerSize];
    if (file_size < (off_t)kTrailerSize || !read_at(f, (uint64_t)file_size - kTrailerSize, trailer, kTrailerSize) ||
        memcmp(trailer, kMagic, sizeof(kMagic)) != 0) {
        die("this program has no bundle attached (make one with tools/bundle.mob)");
    }
    uint64_t index_offset = read_u64(trailer + 16);
    uint64_t index_size = read_u64(trailer + 24);
    uint64_t file_count = read_u64(trailer + 32);
    std::string hash((const char*)trailer + 40, 24);
    hash = hash.c_str();   // NUL-padded

    // Index
    std::vector<unsigned char> index(index_size);
    if (index_size && !read_at(f, index_offset, index.data(), index.size())) die("corrupt bundle (index)");
    std::vector<Entry> entries;
    size_t pos = 0;
    for (uint64_t i = 0; i < file_count; i++) {
        if (pos + 4 > index.size()) die("corrupt bundle (index)");
        uint32_t len = read_u32(&index[pos]); pos += 4;
        if (pos + len + 20 > index.size()) die("corrupt bundle (index)");
        Entry e;
        e.path.assign((const char*)&index[pos], len); pos += len;
        e.offset = read_u64(&index[pos]); pos += 8;
        e.size = read_u64(&index[pos]); pos += 8;
        e.mode = read_u32(&index[pos]); pos += 4;
        if (!safe_path(e.path)) die("corrupt bundle (unsafe path " + e.path + ")");
        entries.push_back(e);
    }

    // Cache
    std::string dir = cache_root() + "/" + hash;
    if (!exists(dir + "/.complete")) {
        if (!mkdirs(cache_root())) die("cannot create " + cache_root() + ": " + strerror(errno));
        extract(f, entries, dir);
    }
    fclose(f);

    std::string entry_script;
    {
        FILE* ef = fopen((dir + "/__entry__").c_str(), "rb");
        if (!ef) die("bundle has no entry script");
        char buf[PATH_MAX] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, ef);
        fclose(ef);
        entry_script.assign(buf, n);
    }

    // Core
    void* lib = dlopen((dir + "/libmobius-core.so").c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!lib) die(std::string("cannot load the core library: ") + dlerror());
    Api api;
    bind(lib, api.default_config_into, "mobius_default_config_into");
    bind(lib, api.new_state, "mobius_new_state");
    bind(lib, api.init_stdlib, "mobius_init_stdlib");
    bind(lib, api.free_state, "mobius_free_state");
    bind(lib, api.add_plugin_directory, "mobius_add_plugin_directory");
    bind(lib, api.set_exit_handler, "mobius_set_exit_handler");
    bind(lib, api.push_new_array, "mobius_stack_pushNewArray");
    bind(lib, api.push_string, "mobius_stack_pushString");
    bind(lib, api.array_push, "mobius_stack_arrayPush");
    bind(lib, api.stack_size, "mobius_stack_size");
    bind(lib, api.set_global, "mobius_stack_setGlobal");
    bind(lib, api.set_global_readonly, "mobius_set_global_readonly");
    bind(lib, api.exec_file, "mobius_exec_file");

    // MobiusConfig is filled by the library; this buffer is larger than it.
    alignas(16) unsigned char config[512];
    api.default_config_into(config);
    void* state = api.new_state(config);
    if (!state) die("cannot create the interpreter");
    api.add_plugin_directory(state, (dir + "/modules").c_str());
    if (api.init_stdlib(state) != 0) die("cannot initialize the standard library");
    api.set_exit_handler(state, on_exit_call, nullptr);

    // argv: the arguments after the program name, as for a script run with
    // `mobius script.mob args...`. bundle_dir: where the bundle's files are.
    api.push_new_array(state, (size_t)(argc > 1 ? argc - 1 : 0));
    int arr = api.stack_size(state) - 1;
    for (int i = 1; i < argc; i++) {
        api.push_string(state, argv[i]);
        api.array_push(state, arr);
    }
    api.set_global(state, "argv");
    api.set_global_readonly(state, "argv", true);
    api.push_string(state, dir.c_str());
    api.set_global(state, "bundle_dir");
    api.set_global_readonly(state, "bundle_dir", true);

    int rc = api.exec_file(state, (dir + "/" + entry_script).c_str());
    fflush(stdout);
    api.free_state(state);
    return rc == 0 ? 0 : 1;
}

#endif // __linux__
