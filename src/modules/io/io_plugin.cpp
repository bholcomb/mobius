// io: file and standard-stream objects with incremental, binary-safe
// reads and writes.
//
// A stream wraps a C FILE*. Files are always opened in binary mode, so bytes
// pass through unchanged on every platform; strings in Mobius are bytes, so
// there is no encoding layer. Each stream has a mutex, so one stream can be
// shared between fibers: every call is atomic, and the order of calls from
// different fibers is whatever the scheduler produces. Calls block their
// worker thread (fiber-aware waits are later work).
//
// The method table for streams is assembled by io.mob (native methods plus
// script conveniences such as lines()) and installed with
// __set_stream_methods.

#include <mobius/mobius_plugin.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
  #define io_fseek _fseeki64
  #define io_ftell _ftelli64
  #define io_lock_file _lock_file
  #define io_unlock_file _unlock_file
  #define io_getc_unlocked _fgetc_nolock
#else
  #define io_fseek fseeko
  #define io_ftell ftello
  #define io_lock_file flockfile
  #define io_unlock_file funlockfile
  #define io_getc_unlocked getc_unlocked
#endif

namespace {

static const char* STREAM_TYPE = "io_stream";
static const size_t COPY_CHUNK = 64 * 1024;

enum class LastOp { none, read, write };

struct IoStream {
    std::mutex mu;
    FILE* fp = nullptr;
    std::string name;            // path, or "<stdin>" etc.
    bool readable = false;
    bool writable = false;
    bool is_std = false;         // closing never closes the OS stream
    bool closed = false;
    LastOp last = LastOp::none;
};

static void stream_destructor(void* ptr) {
    IoStream* s = static_cast<IoStream*>(ptr);
    if (!s) return;
    if (!s->closed && s->fp) {
        if (s->is_std) fflush(s->fp);
        else fclose(s->fp);
    }
    delete s;
}

static IoStream* get_stream(MobiusState* state, int idx) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, STREAM_TYPE) != 0) return nullptr;
    return static_cast<IoStream*>(ptr);
}

static int os_error(MobiusState* state, const char* op, const std::string& name, int err) {
    std::string msg = std::string(op) + ": " + name + ": " + strerror(err);
    return mobius_error(state, msg.c_str());
}

static int op_error(MobiusState* state, const char* op, const std::string& name, const char* what) {
    std::string msg = std::string(op) + ": " + name + ": " + what;
    return mobius_error(state, msg.c_str());
}

// Get `self` as an open stream for method `op`, or report why not.
static IoStream* self_stream(MobiusState* state, const char* op, int* rc) {
    IoStream* s = get_stream(state, 0);
    if (!s) { *rc = mobius_error(state, (std::string(op) + ": self is not a stream").c_str()); return nullptr; }
    return s;
}

// C requires a flush or seek between a write and a following read (and a
// seek between a read and a following write) on the same FILE. Insert it so
// scripts never have to.
static void switch_to(IoStream* s, LastOp next) {
    if (s->last == LastOp::write && next == LastOp::read) fflush(s->fp);
    else if (s->last == LastOp::read && next == LastOp::write) io_fseek(s->fp, 0, SEEK_CUR);
    s->last = next;
}

// Bytes from a string or buffer argument.
static bool arg_bytes(MobiusState* state, int idx, const char** data, size_t* len) {
    if (mobius_stack_isBuffer(state, idx)) {
        *data = static_cast<const char*>(mobius_stack_getBufferData(state, idx, len));
        return true;
    }
    if (mobius_stack_isString(state, idx)) {
        *data = mobius_stack_getStringData(state, idx, len);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// io.open(path [, mode])
// ---------------------------------------------------------------------------

static int io_open(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "io.open expects (path [, mode])");
    if (!mobius_stack_isString(state, 0)) return mobius_error(state, "io.open: path must be a string");
    size_t path_len = 0;
    const char* path_data = mobius_stack_getStringData(state, 0, &path_len);
    std::string path(path_data, path_len);
    std::string mode = "r";
    if (arg_count == 2) {
        if (!mobius_stack_isString(state, 1)) return mobius_error(state, "io.open: mode must be a string");
        mode = mobius_stack_getString(state, 1);
    }

    // Mobius modes, opened in binary. "x" creates a new file and fails if
    // it already exists.
    const char* cmode = nullptr;
    bool readable = false, writable = false;
    if (mode == "r")       { cmode = "rb";  readable = true; }
    else if (mode == "w")  { cmode = "wb";  writable = true; }
    else if (mode == "a")  { cmode = "ab";  writable = true; }
    else if (mode == "r+") { cmode = "r+b"; readable = writable = true; }
    else if (mode == "w+") { cmode = "w+b"; readable = writable = true; }
    else if (mode == "a+") { cmode = "a+b"; readable = writable = true; }
    else if (mode == "x")  { cmode = "wbx"; writable = true; }
    else if (mode == "x+") { cmode = "w+bx"; readable = writable = true; }
    else {
        return mobius_error(state, ("io.open: invalid mode '" + mode +
                                    "' (use r, w, a, r+, w+, a+, x or x+)").c_str());
    }
    if (path.find('\0') != std::string::npos) {
        return op_error(state, "io.open", "path", "contains a NUL byte");
    }

    errno = 0;
    FILE* fp = fopen(path.c_str(), cmode);
    if (!fp) return os_error(state, "io.open", path, errno);

    IoStream* s = new IoStream();
    s->fp = fp;
    s->name = path;
    s->readable = readable;
    s->writable = writable;
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushUserdata(state, s, stream_destructor, STREAM_TYPE, sizeof(IoStream));
    return 1;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

static bool check_readable(MobiusState* state, IoStream* s, const char* op, int* rc) {
    if (s->closed) { *rc = op_error(state, op, s->name, "stream is closed"); return false; }
    if (!s->readable) { *rc = op_error(state, op, s->name, "stream is not open for reading"); return false; }
    return true;
}

static bool check_writable(MobiusState* state, IoStream* s, const char* op, int* rc) {
    if (s->closed) { *rc = op_error(state, op, s->name, "stream is closed"); return false; }
    if (!s->writable) { *rc = op_error(state, op, s->name, "stream is not open for writing"); return false; }
    return true;
}

// stream:read(n): up to n bytes as a buffer, or nil at end of file.
static int stream_read(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:read";
    if (arg_count != 2) return mobius_error(state, "stream:read expects (n)");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    if (!mobius_stack_isInteger(state, 1)) return mobius_error(state, "stream:read: n must be an integer");
    int64_t n = mobius_stack_getInt64(state, 1);
    if (n <= 0) return mobius_error(state, "stream:read: n must be positive");

    std::lock_guard<std::mutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    switch_to(s, LastOp::read);
    std::vector<char> data((size_t)n);
    size_t got = fread(data.data(), 1, (size_t)n, s->fp);
    if (got == 0 && ferror(s->fp)) {
        int err = errno;
        clearerr(s->fp);
        return os_error(state, op, s->name, err);
    }
    mobius_stack_pop(state, arg_count);
    if (got == 0) mobius_stack_pushNil(state);
    else mobius_stack_pushBufferCopy(state, data.data(), got);
    return 1;
}

// stream:read_into(buffer): fill up to the buffer's size; returns the count
// read, 0 at end of file.
static int stream_read_into(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:read_into";
    if (arg_count != 2) return mobius_error(state, "stream:read_into expects (buffer)");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    if (!mobius_stack_isBuffer(state, 1)) return mobius_error(state, "stream:read_into: argument must be a buffer");
    if (mobius_stack_bufferIsReadonly(state, 1)) return mobius_error(state, "stream:read_into: buffer is read-only");
    size_t cap = 0;
    void* dst = mobius_stack_getBufferData(state, 1, &cap);

    std::lock_guard<std::mutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    switch_to(s, LastOp::read);
    size_t got = cap ? fread(dst, 1, cap, s->fp) : 0;
    if (got == 0 && cap && ferror(s->fp)) {
        int err = errno;
        clearerr(s->fp);
        return os_error(state, op, s->name, err);
    }
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, (int64_t)got);
    return 1;
}

// Everything left in the stream.
static bool read_rest(IoStream* s, std::string& out, int* err) {
    switch_to(s, LastOp::read);
    char chunk[COPY_CHUNK];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), s->fp)) > 0) out.append(chunk, n);
    if (ferror(s->fp)) {
        *err = errno;
        clearerr(s->fp);
        return false;
    }
    return true;
}

// stream:read_all(): the rest of the stream as a buffer (empty at EOF).
static int stream_read_all(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:read_all";
    if (arg_count != 1) return mobius_error(state, "stream:read_all expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    std::string data;
    int err = 0;
    if (!read_rest(s, data, &err)) return os_error(state, op, s->name, err);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBufferCopy(state, data.data(), data.size());
    return 1;
}

// stream:read_text(): the rest of the stream as a string (empty at EOF).
static int stream_read_text(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:read_text";
    if (arg_count != 1) return mobius_error(state, "stream:read_text expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    std::string data;
    int err = 0;
    if (!read_rest(s, data, &err)) return os_error(state, op, s->name, err);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushStringLength(state, data.data(), data.size());
    return 1;
}

// stream:read_line(): the next line without "\n" (or "\r\n"), or nil at end
// of file. A last line without a newline is still returned; an empty line
// is "". Byte-exact: NUL bytes stay in the line.
static int stream_read_line(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:read_line";
    if (arg_count != 1) return mobius_error(state, "stream:read_line expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    switch_to(s, LastOp::read);

    std::string line;
    bool got_any = false, newline = false;
    io_lock_file(s->fp);
    int c;
    while ((c = io_getc_unlocked(s->fp)) != EOF) {
        got_any = true;
        if (c == '\n') { newline = true; break; }
        line.push_back((char)c);
    }
    io_unlock_file(s->fp);
    if (!got_any && ferror(s->fp)) {
        int err = errno;
        clearerr(s->fp);
        return os_error(state, op, s->name, err);
    }
    mobius_stack_pop(state, arg_count);
    if (!got_any) {
        mobius_stack_pushNil(state);
        return 1;
    }
    if (newline && !line.empty() && line.back() == '\r') line.pop_back();
    mobius_stack_pushStringLength(state, line.data(), line.size());
    return 1;
}

// ---------------------------------------------------------------------------
// Writing and control
// ---------------------------------------------------------------------------

// stream:write(data): write a string or buffer completely; returns the
// number of bytes written.
static int stream_write(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:write";
    if (arg_count != 2) return mobius_error(state, "stream:write expects (data)");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    const char* data = nullptr;
    size_t len = 0;
    if (!arg_bytes(state, 1, &data, &len)) return mobius_error(state, "stream:write: data must be a string or buffer");

    std::lock_guard<std::mutex> lock(s->mu);
    if (!check_writable(state, s, op, &rc)) return rc;
    switch_to(s, LastOp::write);
    size_t put = len ? fwrite(data, 1, len, s->fp) : 0;
    if (put != len) {
        int err = errno;
        clearerr(s->fp);
        return os_error(state, op, s->name, err ? err : EIO);
    }
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, (int64_t)len);
    return 1;
}

static int stream_flush(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:flush";
    if (arg_count != 1) return mobius_error(state, "stream:flush expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    if (s->closed) return op_error(state, op, s->name, "stream is closed");
    if (fflush(s->fp) != 0) return os_error(state, op, s->name, errno);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNil(state);
    return 1;
}

// stream:close(): flush and release the handle. Closing again is harmless.
// A standard stream is flushed and marked closed, but the OS stream stays
// open (print and child processes keep working).
static int stream_close(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:close";
    if (arg_count != 1) return mobius_error(state, "stream:close expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    int err = 0;
    if (!s->closed) {
        if (s->is_std) {
            if (fflush(s->fp) != 0) err = errno;
        } else {
            if (fclose(s->fp) != 0) err = errno;
            s->fp = nullptr;
        }
        s->closed = true;
    }
    if (err) return os_error(state, op, s->name, err);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNil(state);
    return 1;
}

static int stream_is_closed(MobiusState* state, int arg_count) {
    int rc = 0;
    if (arg_count != 1) return mobius_error(state, "stream:is_closed expects no arguments");
    IoStream* s = self_stream(state, "stream:is_closed", &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    bool closed = s->closed;
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, closed);
    return 1;
}

// stream:seek(offset [, origin]): origin "set" (default), "cur" or "end".
// Returns the new position. Errors on streams that can't seek (pipes,
// terminals).
static int stream_seek(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:seek";
    if (arg_count < 2 || arg_count > 3) return mobius_error(state, "stream:seek expects (offset [, origin])");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    if (!mobius_stack_isInteger(state, 1)) return mobius_error(state, "stream:seek: offset must be an integer");
    int64_t offset = mobius_stack_getInt64(state, 1);
    int whence = SEEK_SET;
    if (arg_count == 3) {
        if (!mobius_stack_isString(state, 2)) return mobius_error(state, "stream:seek: origin must be a string");
        std::string origin = mobius_stack_getString(state, 2);
        if (origin == "set") whence = SEEK_SET;
        else if (origin == "cur") whence = SEEK_CUR;
        else if (origin == "end") whence = SEEK_END;
        else return mobius_error(state, ("stream:seek: invalid origin '" + origin + "' (use set, cur or end)").c_str());
    }
    std::lock_guard<std::mutex> lock(s->mu);
    if (s->closed) return op_error(state, op, s->name, "stream is closed");
    if (io_fseek(s->fp, offset, whence) != 0) return os_error(state, op, s->name, errno);
    s->last = LastOp::none;   // a seek satisfies both direction switches
    int64_t pos = (int64_t)io_ftell(s->fp);
    if (pos < 0) return os_error(state, op, s->name, errno);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, pos);
    return 1;
}

static int stream_tell(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "stream:tell";
    if (arg_count != 1) return mobius_error(state, "stream:tell expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<std::mutex> lock(s->mu);
    if (s->closed) return op_error(state, op, s->name, "stream is closed");
    int64_t pos = (int64_t)io_ftell(s->fp);
    if (pos < 0) return os_error(state, op, s->name, errno);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, pos);
    return 1;
}

static int stream_name(MobiusState* state, int arg_count) {
    int rc = 0;
    if (arg_count != 1) return mobius_error(state, "stream:name expects no arguments");
    IoStream* s = self_stream(state, "stream:name", &rc);
    if (!s) return rc;
    std::string name = s->name;
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushStringLength(state, name.data(), name.size());
    return 1;
}

// ---------------------------------------------------------------------------
// io.copy(source, destination): stream everything from source to
// destination in fixed-size chunks; returns the number of bytes copied.
// ---------------------------------------------------------------------------

static int io_copy(MobiusState* state, int arg_count) {
    int rc = 0;
    const char* op = "io.copy";
    if (arg_count != 2) return mobius_error(state, "io.copy expects (source, destination)");
    IoStream* src = get_stream(state, 0);
    IoStream* dst = get_stream(state, 1);
    if (!src || !dst) return mobius_error(state, "io.copy: source and destination must be streams");
    if (src == dst) return mobius_error(state, "io.copy: source and destination are the same stream");

    // Lock both without risking deadlock against a copy in the other
    // direction.
    std::unique_lock<std::mutex> l1(src->mu, std::defer_lock), l2(dst->mu, std::defer_lock);
    std::lock(l1, l2);
    if (!check_readable(state, src, op, &rc)) return rc;
    if (!check_writable(state, dst, op, &rc)) return rc;
    switch_to(src, LastOp::read);
    switch_to(dst, LastOp::write);

    std::vector<char> chunk(COPY_CHUNK);
    int64_t total = 0;
    size_t n;
    while ((n = fread(chunk.data(), 1, chunk.size(), src->fp)) > 0) {
        if (fwrite(chunk.data(), 1, n, dst->fp) != n) {
            int err = errno;
            clearerr(dst->fp);
            return os_error(state, op, dst->name, err ? err : EIO);
        }
        total += (int64_t)n;
    }
    if (ferror(src->fp)) {
        int err = errno;
        clearerr(src->fp);
        return os_error(state, op, src->name, err);
    }
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, total);
    return 1;
}

// __set_stream_methods(table): install the stream method table (built by
// io.mob) as the metatable for every stream.
static int io_set_stream_methods(MobiusState* state, int arg_count) {
    if (arg_count != 1 || !mobius_stack_isTable(state, 0)) {
        return mobius_error(state, "__set_stream_methods expects a table");
    }
    mobius_set_userdata_type_metatable(state, STREAM_TYPE);   // consumes the table
    mobius_stack_pushNil(state);
    return 1;
}

static void push_std_stream(MobiusState* state, int module_idx, FILE* fp,
                            const char* key, const char* name, bool readable) {
    IoStream* s = new IoStream();
    s->fp = fp;
    s->name = name;
    s->readable = readable;
    s->writable = !readable;
    s->is_std = true;
    mobius_stack_pushUserdata(state, s, stream_destructor, STREAM_TYPE, sizeof(IoStream));
    mobius_stack_setTableField(state, module_idx, key);
}

static int io_post_init(MobiusState* state) {
#ifdef _WIN32
    // Byte-exact standard streams: no CRLF translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const int module_idx = 0;
    push_std_stream(state, module_idx, stdin, "stdin", "<stdin>", true);
    push_std_stream(state, module_idx, stdout, "stdout", "<stdout>", false);
    push_std_stream(state, module_idx, stderr, "stderr", "<stderr>", false);
    return 0;
}

} // namespace

static MobiusPluginFunction io_functions[] = {
    {"open", io_open, SIZE_MAX, MOBIUS_VAL_USERDATA, "Open a file stream: io.open(path [, mode])"},
    {"copy", io_copy, 2, MOBIUS_VAL_INT64, "Copy everything from one stream to another"},
    {"__set_stream_methods", io_set_stream_methods, 1, MOBIUS_VAL_NIL, "Internal: install stream methods"},
    {"__read", stream_read, 2, MOBIUS_VAL_UNKNOWN, "Internal stream method"},
    {"__read_into", stream_read_into, 2, MOBIUS_VAL_INT64, "Internal stream method"},
    {"__read_line", stream_read_line, 1, MOBIUS_VAL_UNKNOWN, "Internal stream method"},
    {"__read_all", stream_read_all, 1, MOBIUS_VAL_BUFFER, "Internal stream method"},
    {"__read_text", stream_read_text, 1, MOBIUS_VAL_STRING, "Internal stream method"},
    {"__write", stream_write, 2, MOBIUS_VAL_INT64, "Internal stream method"},
    {"__flush", stream_flush, 1, MOBIUS_VAL_NIL, "Internal stream method"},
    {"__close", stream_close, 1, MOBIUS_VAL_NIL, "Internal stream method"},
    {"__is_closed", stream_is_closed, 1, MOBIUS_VAL_BOOL, "Internal stream method"},
    {"__seek", stream_seek, SIZE_MAX, MOBIUS_VAL_INT64, "Internal stream method"},
    {"__tell", stream_tell, 1, MOBIUS_VAL_INT64, "Internal stream method"},
    {"__name", stream_name, 1, MOBIUS_VAL_STRING, "Internal stream method"},
};

static MobiusPlugin io_plugin = {
    .metadata = {
        .name = "io",
        .version = "1.0.0",
        .description = "File and standard-stream objects",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = io_functions,
    .function_count = sizeof(io_functions) / sizeof(io_functions[0]),
    .init_plugin = nullptr,
    .cleanup_plugin = nullptr,
    .post_init = io_post_init,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &io_plugin;
}
