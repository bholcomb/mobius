// Windows implementation of io_platform.h.
//
// Files and the standard streams are stdio streams (blocking): the reactor
// waits on sockets only. Pipes to a child are waitable: their handles are
// put in non-blocking mode (PIPE_NOWAIT), and waiting for one is a short
// parked sleep before trying again.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "io_stream.h"

#include <cerrno>
#include <fcntl.h>
#include <io.h>

namespace {

// How long a wait on a pipe sleeps before the caller tries again.
const int64_t PIPE_POLL_MS = 5;

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

int errno_from_win32(DWORD code) {
    switch (code) {
        case ERROR_BROKEN_PIPE:
        case ERROR_NO_DATA:
        case ERROR_PIPE_NOT_CONNECTED: return EPIPE;
        case ERROR_ACCESS_DENIED:      return EACCES;
        case ERROR_INVALID_HANDLE:     return EBADF;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:        return ENOMEM;
        case ERROR_DISK_FULL:          return ENOSPC;
        default:                       return EIO;
    }
}

} // namespace

FILE* io_platform_fopen(const std::string& path, const char* mode, int* err) {
    errno = 0;
    FILE* fp = _wfopen(widen(path).c_str(), widen(mode).c_str());
    if (!fp) *err = errno ? errno : EIO;
    return fp;
}

void io_platform_attach_file(IoStream* s, FILE* fp) {
    s->fp = fp;
}

void io_platform_attach_std(IoStream* s, FILE* fp) {
    s->fp = fp;
}

void io_platform_attach_pipe(IoStream* s, intptr_t handle) {
    io_platform_set_nonblocking(handle);
    s->waitable = true;
    s->handle = handle;
    s->nonblocking = true;
}

long long io_platform_read(intptr_t handle, void* buf, size_t len, int* err) {
    DWORD want = len > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)len;
    DWORD got = 0;
    if (ReadFile((HANDLE)handle, buf, want, &got, nullptr)) {
        // A non-blocking pipe with nothing in it may also succeed empty.
        return got > 0 ? (long long)got : IO_PLATFORM_WOULD_BLOCK;
    }
    DWORD code = GetLastError();
    if (code == ERROR_NO_DATA) return IO_PLATFORM_WOULD_BLOCK;   // empty pipe
    if (code == ERROR_BROKEN_PIPE || code == ERROR_HANDLE_EOF) return 0;   // writer closed
    *err = errno_from_win32(code);
    return IO_PLATFORM_ERROR;
}

long long io_platform_write(intptr_t handle, const void* buf, size_t len, int* err) {
    DWORD want = len > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)len;
    DWORD put = 0;
    if (WriteFile((HANDLE)handle, buf, want, &put, nullptr)) {
        // A non-blocking pipe that is full accepts nothing.
        return put > 0 ? (long long)put : IO_PLATFORM_WOULD_BLOCK;
    }
    // ERROR_NO_DATA on a write: the reader closed the pipe.
    *err = errno_from_win32(GetLastError());
    return IO_PLATFORM_ERROR;
}

int io_platform_close(intptr_t handle) {
    return CloseHandle((HANDLE)handle) ? 0 : errno_from_win32(GetLastError());
}

void io_platform_set_nonblocking(intptr_t handle) {
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState((HANDLE)handle, &mode, nullptr, nullptr);
}

int io_platform_wait(MobiusState* state, const MobiusIoWait* waits, int count, int64_t timeout_ms) {
    (void)waits;
    (void)count;
    // Pipes can't be waited on: sleep (parked) briefly, then let the caller
    // try again.
    int64_t step = (timeout_ms >= 0 && timeout_ms < PIPE_POLL_MS) ? timeout_ms : PIPE_POLL_MS;
    int rc = mobius_io_wait(state, nullptr, 0, step);
    if (rc == MOBIUS_IO_CANCELLED || rc == MOBIUS_IO_ERROR) return rc;
    if (timeout_ms >= 0 && step >= timeout_ms) return MOBIUS_IO_TIMEOUT;
    return 0;
}

bool io_platform_read_line(FILE* fp, std::string& line, bool* newline) {
    *newline = false;
    bool got_any = false;
    _lock_file(fp);
    int c;
    while ((c = _fgetc_nolock(fp)) != EOF) {
        got_any = true;
        if (c == '\n') { *newline = true; break; }
        line.push_back((char)c);
    }
    _unlock_file(fp);
    return got_any;
}

int io_platform_seek(FILE* fp, int64_t offset, int whence) {
    return _fseeki64(fp, offset, whence);
}

int64_t io_platform_tell(FILE* fp) {
    return _ftelli64(fp);
}

intptr_t io_platform_file_handle(FILE* fp) {
    return _get_osfhandle(_fileno(fp));
}

void io_platform_init_std() {
    // Byte-exact standard streams: no CRLF translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}
