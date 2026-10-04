// io: file and standard-stream objects with incremental, binary-safe
// reads and writes.
//
// Regular files and standard output/error go through a C FILE* (the
// standard streams share C's buffers with print). Pipes, terminals, FIFOs
// and standard input are "waitable": they use the descriptor directly, and
// when it isn't ready the calling fiber parks in the I/O reactor
// (mobius_io_wait) instead of blocking its worker thread. Files are always
// opened in binary mode; Mobius strings are bytes, so there is no encoding
// layer.
//
// Each stream has a fiber mutex held for a whole operation, so one stream
// can be shared between fibers: every call is atomic. close() first wakes a
// fiber waiting on the stream (which then reports "stream is closed"), so
// closing never waits on a read that may never finish.
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

#include "io_stream.h"

namespace {

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

// Get `self` as a stream for method `op`, or report why not.
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
    else if (s->last == LastOp::read && next == LastOp::write) io_platform_seek(s->fp, 0, SEEK_CUR);
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

// Errors from the stream core, reported by the method that hit them.
enum class IoErr { none, os, closed, cancelled };

struct IoResult {
    IoErr err = IoErr::none;
    int os_errno = 0;
};

static int report(MobiusState* state, const char* op, IoStream* s, const IoResult& r) {
    if (r.err == IoErr::closed) return op_error(state, op, s->name, "stream is closed");
    if (r.err == IoErr::cancelled) return mobius_error(state, "CancellationError: fiber was cancelled");
    return os_error(state, op, s->name, r.os_errno ? r.os_errno : EIO);
}

// A blocking pipe takes at least this many bytes without blocking once it
// reports writable (POSIX's smallest PIPE_BUF).
static const size_t ATOMIC_PIPE_WRITE = 512;

// Wait until a waitable stream's handle is ready (parks the fiber).
static bool wait_ready(MobiusState* state, IoStream* s, int events, IoResult* r) {
    while (true) {
        if (s->closing.load(std::memory_order_acquire)) { r->err = IoErr::closed; return false; }
        MobiusIoWait w = {s->handle, events};
        int rc = io_platform_wait(state, &w, 1, -1);
        if (rc >= 0) return true;
        if (rc == MOBIUS_IO_CLOSED) { r->err = IoErr::closed; return false; }
        if (rc == MOBIUS_IO_CANCELLED) { r->err = IoErr::cancelled; return false; }
        if (rc == MOBIUS_IO_TIMEOUT) continue;
        r->err = IoErr::os;
        r->os_errno = EIO;
        return false;
    }
}

// Read more into a waitable stream's buffer: at least one byte, or EOF.
static bool waitable_fill(MobiusState* state, IoStream* s, IoResult* r) {
    if (s->rpos > 0 && s->rpos == s->rbuf.size()) { s->rbuf.clear(); s->rpos = 0; }
    char chunk[COPY_CHUNK];
    while (!s->eof) {
        // A handle we don't own (standard input) stays blocking: wait for it
        // to be readable first, then read what is there.
        if (!s->nonblocking && !wait_ready(state, s, MOBIUS_IO_READ, r)) return false;
        int e = 0;
        long long n = io_platform_read(s->handle, chunk, sizeof(chunk), &e);
        if (n > 0) { s->rbuf.append(chunk, (size_t)n); return true; }
        if (n == 0) { s->eof = true; return true; }
        if (n == IO_PLATFORM_WOULD_BLOCK) {
            if (!wait_ready(state, s, MOBIUS_IO_READ, r)) return false;
            continue;
        }
        r->err = IoErr::os;
        r->os_errno = e;
        return false;
    }
    return true;
}

static size_t buffered(const IoStream* s) { return s->rbuf.size() - s->rpos; }

// Up to `max` bytes into `out` (0 at end of stream).
static bool read_some(MobiusState* state, IoStream* s, char* out, size_t max, size_t* got, IoResult* r) {
    *got = 0;
    if (!s->waitable) {
        switch_to(s, LastOp::read);
        *got = fread(out, 1, max, s->fp);
        if (*got == 0 && ferror(s->fp)) {
            r->err = IoErr::os;
            r->os_errno = errno;
            clearerr(s->fp);
            return false;
        }
        return true;
    }
    if (buffered(s) == 0 && !waitable_fill(state, s, r)) return false;
    size_t n = buffered(s) < max ? buffered(s) : max;
    memcpy(out, s->rbuf.data() + s->rpos, n);
    s->rpos += n;
    *got = n;
    return true;
}

static bool read_rest(MobiusState* state, IoStream* s, std::string& out, IoResult* r) {
    if (!s->waitable) {
        switch_to(s, LastOp::read);
        char chunk[COPY_CHUNK];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), s->fp)) > 0) out.append(chunk, n);
        if (ferror(s->fp)) {
            r->err = IoErr::os;
            r->os_errno = errno;
            clearerr(s->fp);
            return false;
        }
        return true;
    }
    while (true) {
        out.append(s->rbuf, s->rpos, std::string::npos);
        s->rbuf.clear();
        s->rpos = 0;
        if (s->eof) return true;
        if (!waitable_fill(state, s, r)) return false;
    }
}

// The next line without its newline; *found false at end of stream.
static bool read_line(MobiusState* state, IoStream* s, std::string& line, bool* found, IoResult* r) {
    *found = false;
    bool newline = false;
    if (!s->waitable) {
        switch_to(s, LastOp::read);
        bool got_any = io_platform_read_line(s->fp, line, &newline);
        if (!got_any && ferror(s->fp)) {
            r->err = IoErr::os;
            r->os_errno = errno;
            clearerr(s->fp);
            return false;
        }
        *found = got_any;
    } else {
        while (true) {
            size_t nl = s->rbuf.find('\n', s->rpos);
            if (nl != std::string::npos) {
                line.append(s->rbuf, s->rpos, nl - s->rpos);
                s->rpos = nl + 1;
                newline = true;
                *found = true;
                break;
            }
            if (s->eof) {
                if (buffered(s) > 0) {
                    line.append(s->rbuf, s->rpos, std::string::npos);
                    s->rpos = s->rbuf.size();
                    *found = true;
                }
                break;
            }
            if (!waitable_fill(state, s, r)) return false;
        }
    }
    if (newline && !line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

// Write all of `data`.
static bool write_all(MobiusState* state, IoStream* s, const char* data, size_t len, IoResult* r) {
    if (!s->waitable) {
        switch_to(s, LastOp::write);
        size_t put = len ? fwrite(data, 1, len, s->fp) : 0;
        if (put != len) {
            r->err = IoErr::os;
            r->os_errno = errno ? errno : EIO;
            clearerr(s->fp);
            return false;
        }
        return true;
    }
    size_t done = 0;
    while (done < len) {
        // A blocking handle is written a little at a time after a readiness
        // wait (a writable pipe takes that much without blocking).
        size_t chunk = len - done;
        if (!s->nonblocking) {
            if (!wait_ready(state, s, MOBIUS_IO_WRITE, r)) return false;
            if (chunk > ATOMIC_PIPE_WRITE) chunk = ATOMIC_PIPE_WRITE;
        }
        int e = 0;
        long long n = io_platform_write(s->handle, data + done, chunk, &e);
        if (n > 0) { done += (size_t)n; continue; }
        if (n == IO_PLATFORM_WOULD_BLOCK || n == 0) {
            if (!wait_ready(state, s, MOBIUS_IO_WRITE, r)) return false;
            continue;
        }
        r->err = IoErr::os;
        r->os_errno = e ? e : EIO;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// io.open(path [, mode])
// ---------------------------------------------------------------------------

static int io_open(MobiusState* state, int arg_count, void* /*userdata*/) {
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

    // A FIFO blocks in open() until the other side opens it; that wait is
    // not fiber-aware.
    int open_err = 0;
    FILE* fp = io_platform_fopen(path, cmode, &open_err);
    if (!fp) return os_error(state, "io.open", path, open_err);

    IoStream* s = new IoStream();
    s->name = path;
    s->readable = readable;
    s->writable = writable;
    io_platform_attach_file(s, fp);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushUserdata(state, s, stream_destructor, STREAM_TYPE, sizeof(IoStream));
    return 1;
}

// ---------------------------------------------------------------------------
// Methods
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

// stream:read(n): up to n bytes as a buffer, or nil at end of stream.
static int stream_read(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:read";
    if (arg_count != 2) return mobius_error(state, "stream:read expects (n)");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    if (!mobius_stack_isInteger(state, 1)) return mobius_error(state, "stream:read: n must be an integer");
    int64_t n = mobius_stack_getInt64(state, 1);
    if (n <= 0) return mobius_error(state, "stream:read: n must be positive");

    std::lock_guard<FiberMutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    std::vector<char> data((size_t)n);
    size_t got = 0;
    IoResult r;
    if (!s->waitable) {
        if (!read_some(state, s, data.data(), (size_t)n, &got, &r)) return report(state, op, s, r);
    } else {
        // Like fread: fill up to n unless the stream ends first.
        while (got < (size_t)n) {
            size_t part = 0;
            if (!read_some(state, s, data.data() + got, (size_t)n - got, &part, &r)) return report(state, op, s, r);
            if (part == 0) break;
            got += part;
        }
    }
    mobius_stack_pop(state, arg_count);
    if (got == 0) mobius_stack_pushNil(state);
    else mobius_stack_pushBufferCopy(state, data.data(), got);
    return 1;
}

// stream:read_into(buffer): read up to the buffer's size; the count read,
// 0 at end of stream. Waitable streams return what is available.
static int stream_read_into(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:read_into";
    if (arg_count != 2) return mobius_error(state, "stream:read_into expects (buffer)");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    if (!mobius_stack_isBuffer(state, 1)) return mobius_error(state, "stream:read_into: argument must be a buffer");
    if (mobius_stack_bufferIsReadonly(state, 1)) return mobius_error(state, "stream:read_into: buffer is read-only");
    size_t cap = 0;
    void* dst = mobius_stack_getBufferData(state, 1, &cap);

    std::lock_guard<FiberMutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    size_t got = 0;
    IoResult r;
    if (cap && !read_some(state, s, static_cast<char*>(dst), cap, &got, &r)) return report(state, op, s, r);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, (int64_t)got);
    return 1;
}

// stream:read_all(): the rest of the stream as a buffer (empty at EOF).
static int stream_read_all(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:read_all";
    if (arg_count != 1) return mobius_error(state, "stream:read_all expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<FiberMutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    std::string data;
    IoResult r;
    if (!read_rest(state, s, data, &r)) return report(state, op, s, r);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBufferCopy(state, data.data(), data.size());
    return 1;
}

// stream:read_text(): the rest of the stream as a string (empty at EOF).
static int stream_read_text(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:read_text";
    if (arg_count != 1) return mobius_error(state, "stream:read_text expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<FiberMutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    std::string data;
    IoResult r;
    if (!read_rest(state, s, data, &r)) return report(state, op, s, r);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushStringLength(state, data.data(), data.size());
    return 1;
}

// stream:read_line(): the next line without "\n" (or "\r\n"), or nil at end
// of stream. A last line without a newline is still returned; an empty line
// is "". Byte-exact: NUL bytes stay in the line.
static int stream_read_line(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:read_line";
    if (arg_count != 1) return mobius_error(state, "stream:read_line expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<FiberMutex> lock(s->mu);
    if (!check_readable(state, s, op, &rc)) return rc;
    std::string line;
    bool found = false;
    IoResult r;
    if (!read_line(state, s, line, &found, &r)) return report(state, op, s, r);
    mobius_stack_pop(state, arg_count);
    if (!found) mobius_stack_pushNil(state);
    else mobius_stack_pushStringLength(state, line.data(), line.size());
    return 1;
}

// stream:write(data): write a string or buffer completely; returns the
// number of bytes written.
static int stream_write(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:write";
    if (arg_count != 2) return mobius_error(state, "stream:write expects (data)");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    const char* data = nullptr;
    size_t len = 0;
    if (!arg_bytes(state, 1, &data, &len)) return mobius_error(state, "stream:write: data must be a string or buffer");
    // The argument stays on the stack (and alive) while a write waits.
    std::lock_guard<FiberMutex> lock(s->mu);
    if (!check_writable(state, s, op, &rc)) return rc;
    IoResult r;
    if (!write_all(state, s, data, len, &r)) return report(state, op, s, r);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, (int64_t)len);
    return 1;
}

static int stream_flush(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:flush";
    if (arg_count != 1) return mobius_error(state, "stream:flush expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<FiberMutex> lock(s->mu);
    if (s->closed) return op_error(state, op, s->name, "stream is closed");
    if (!s->waitable && fflush(s->fp) != 0) return os_error(state, op, s->name, errno);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNil(state);
    return 1;
}

// stream:close(): flush and release the stream. Closing again is harmless.
// A fiber waiting on the stream is woken first (its call reports "stream is
// closed"). A standard stream is flushed and marked closed, but the OS
// stream stays open (print and child processes keep working).
static int stream_close(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:close";
    if (arg_count != 1) return mobius_error(state, "stream:close expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    s->closing.store(true, std::memory_order_release);
    if (s->waitable && s->handle != -1) mobius_io_wake_fd(s->handle);
    std::lock_guard<FiberMutex> lock(s->mu);
    int err = 0;
    if (!s->closed) {
        if (s->waitable) {
            if (!s->is_std) err = io_platform_close(s->handle);
            s->handle = -1;
        } else if (s->is_std) {
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

static int stream_is_closed(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    if (arg_count != 1) return mobius_error(state, "stream:is_closed expects no arguments");
    IoStream* s = self_stream(state, "stream:is_closed", &rc);
    if (!s) return rc;
    bool closed = s->closed || s->closing.load(std::memory_order_acquire);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, closed);
    return 1;
}

// stream:seek(offset [, origin]): origin "set" (default), "cur" or "end".
// Returns the new position. Errors on streams that can't seek (pipes,
// terminals).
static int stream_seek(MobiusState* state, int arg_count, void* /*userdata*/) {
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
    std::lock_guard<FiberMutex> lock(s->mu);
    if (s->closed) return op_error(state, op, s->name, "stream is closed");
    if (s->waitable) return os_error(state, op, s->name, ESPIPE);
    if (io_platform_seek(s->fp, offset, whence) != 0) return os_error(state, op, s->name, errno);
    s->last = LastOp::none;   // a seek satisfies both direction switches
    int64_t pos = (int64_t)io_platform_tell(s->fp);
    if (pos < 0) return os_error(state, op, s->name, errno);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, pos);
    return 1;
}

static int stream_tell(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "stream:tell";
    if (arg_count != 1) return mobius_error(state, "stream:tell expects no arguments");
    IoStream* s = self_stream(state, op, &rc);
    if (!s) return rc;
    std::lock_guard<FiberMutex> lock(s->mu);
    if (s->closed) return op_error(state, op, s->name, "stream is closed");
    if (s->waitable) return os_error(state, op, s->name, ESPIPE);
    int64_t pos = (int64_t)io_platform_tell(s->fp);
    if (pos < 0) return os_error(state, op, s->name, errno);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, pos);
    return 1;
}

static int stream_name(MobiusState* state, int arg_count, void* /*userdata*/) {
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

static int io_copy(MobiusState* state, int arg_count, void* /*userdata*/) {
    int rc = 0;
    const char* op = "io.copy";
    if (arg_count != 2) return mobius_error(state, "io.copy expects (source, destination)");
    IoStream* src = get_stream(state, 0);
    IoStream* dst = get_stream(state, 1);
    if (!src || !dst) return mobius_error(state, "io.copy: source and destination must be streams");
    if (src == dst) return mobius_error(state, "io.copy: source and destination are the same stream");

    // Lock both without risking deadlock against a copy in the other
    // direction.
    std::unique_lock<FiberMutex> l1(src->mu, std::defer_lock), l2(dst->mu, std::defer_lock);
    std::lock(l1, l2);
    if (!check_readable(state, src, op, &rc)) return rc;
    if (!check_writable(state, dst, op, &rc)) return rc;

    std::vector<char> chunk(COPY_CHUNK);
    int64_t total = 0;
    IoResult r;
    while (true) {
        size_t n = 0;
        if (!read_some(state, src, chunk.data(), chunk.size(), &n, &r)) return report(state, op, src, r);
        if (n == 0) break;
        if (!write_all(state, dst, chunk.data(), n, &r)) return report(state, op, dst, r);
        total += (int64_t)n;
    }
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushInt64(state, total);
    return 1;
}

// __set_stream_methods(table): install the stream method table (built by
// io.mob) as the metatable for every stream.
static int io_set_stream_methods(MobiusState* state, int arg_count, void* /*userdata*/) {
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
    s->name = name;
    s->readable = readable;
    s->writable = !readable;
    s->is_std = true;
    io_platform_attach_std(s, fp);
    mobius_stack_pushUserdata(state, s, stream_destructor, STREAM_TYPE, sizeof(IoStream));
    mobius_stack_setTableField(state, module_idx, key);
}

static int io_post_init(MobiusState* state) {
    io_platform_init_std();
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
    {
        "io" /* name */,
        "1.0.0" /* version */,
        "File and standard-stream objects" /* description */,
        "Mobius Team" /* author */,
        MOBIUS_PLUGIN_API_VERSION /* api_version */,
        "MIT" /* license */,
        nullptr /* depends_on */,
        0 /* depends_on_count */
    },
    io_functions /* functions */,
    sizeof(io_functions) / sizeof(io_functions[0]) /* function_count */,
    nullptr /* init_plugin */,
    nullptr /* cleanup_plugin */,
    io_post_init /* post_init */
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &io_plugin;
}
