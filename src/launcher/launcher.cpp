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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "platform/mobius_platform.h"

namespace fs = std::filesystem;

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

// Paths are UTF-8.
fs::path to_path(const std::string& s) { return fs::u8path(s); }

bool read_at(std::ifstream& f, uint64_t offset, void* out, size_t size) {
    f.clear();
    f.seekg((std::streamoff)offset);
    f.read((char*)out, (std::streamsize)size);
    return (size_t)f.gcount() == size;
}

bool mkdirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(to_path(path), ec);
    return fs::is_directory(to_path(path), ec);
}

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(to_path(path), ec);
}

std::string cache_root() {
    if (const char* c = getenv("MOBIUS_BUNDLE_CACHE")) if (*c) return c;
    std::string user = platform_user_cache_dir();
    if (user.empty()) die("no per-user cache directory (set MOBIUS_BUNDLE_CACHE)");
    return user + "/mobius/bundles";
}

// Paths come from the bundle's own index; refuse anything that would land
// outside the cache directory.
bool safe_path(const std::string& p) {
    if (p.empty() || p[0] == '/' || p[0] == '\\' || p.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t slash = p.find_first_of("/\\", start);
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
void extract(std::ifstream& f, const std::vector<Entry>& entries, const std::string& dir) {
    std::string tmp = dir + ".tmp-" + std::to_string(platform_process_id());
    if (!mkdirs(tmp)) die("cannot create " + tmp);
    std::vector<char> chunk(1 << 16);
    for (const Entry& e : entries) {
        std::string out_path = tmp + "/" + e.path;
        if (!mkdirs(to_path(out_path).parent_path().u8string())) die("cannot create directories for " + e.path);
        std::ofstream out(to_path(out_path), std::ios::binary | std::ios::trunc);
        if (!out) die("cannot write " + out_path);
        f.clear();
        f.seekg((std::streamoff)e.offset);
        if (!f) die("corrupt bundle (bad offset)");
        uint64_t left = e.size;
        while (left > 0) {
            size_t n = left < chunk.size() ? (size_t)left : chunk.size();
            f.read(chunk.data(), (std::streamsize)n);
            if ((size_t)f.gcount() != n) die("corrupt bundle (short data)");
            out.write(chunk.data(), (std::streamsize)n);
            if (!out) die("cannot write " + out_path);
            left -= n;
        }
        out.close();
        std::error_code ec;
        fs::permissions(to_path(out_path), (fs::perms)(e.mode & 0777), ec);
    }
    { std::ofstream done(to_path(tmp + "/.complete"), std::ios::binary); }
    std::error_code ec;
    fs::rename(to_path(tmp), to_path(dir), ec);
    if (ec) {
        // Another run finished first: use its copy, and remove ours.
        if (!exists(dir + "/.complete")) die("cannot install " + dir + ": " + ec.message());
        std::error_code ignored;
        fs::remove_all(to_path(tmp), ignored);
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
    fn = reinterpret_cast<T>(platform_library_symbol(lib, name));
    if (!fn) die(std::string("the bundled core library lacks ") + name);
}

void on_exit_call(void*, int code, void*) {
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string exe = platform_executable_path();
    if (exe.empty()) die("cannot locate the executable");
    std::ifstream f(to_path(exe), std::ios::binary);
    if (!f) die("cannot read " + exe);

    // Trailer
    f.seekg(0, std::ios::end);
    uint64_t file_size = (uint64_t)f.tellg();
    unsigned char trailer[kTrailerSize];
    if (!f || file_size < kTrailerSize || !read_at(f, file_size - kTrailerSize, trailer, kTrailerSize) ||
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
        if (!mkdirs(cache_root())) die("cannot create " + cache_root());
        extract(f, entries, dir);
    }
    f.close();

    std::string entry_script;
    {
        std::ifstream ef(to_path(dir + "/__entry__"), std::ios::binary);
        if (!ef) die("bundle has no entry script");
        entry_script.assign(std::istreambuf_iterator<char>(ef), std::istreambuf_iterator<char>());
    }

    // Core
    std::string load_error;
    void* lib = platform_library_open(dir + "/" + platform_core_library_name(), true, &load_error);
    if (!lib) die("cannot load the core library: " + load_error);
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

