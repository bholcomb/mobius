#ifndef MOBIUS_MODULES_IO_PLATFORM_H
#define MOBIUS_MODULES_IO_PLATFORM_H

// What io streams need from the operating system, implemented once per
// platform (io_platform_posix.cpp for Linux and macOS,
// io_platform_win32.cpp). The process module compiles it too: pipes to a
// child are io streams.
//
// A handle is a file descriptor on Linux and macOS and a HANDLE on
// Windows; -1 means none. Errors are errno values. Paths are UTF-8.

#include <mobius/mobius_plugin.h>

#include <cstdint>
#include <cstdio>
#include <string>

struct IoStream;

// io_platform_read / io_platform_write results besides a byte count.
const long long IO_PLATFORM_ERROR = -1;        // *err holds the errno value
const long long IO_PLATFORM_WOULD_BLOCK = -2;  // wait (io_platform_wait), then retry

// fopen with a C mode string ("rb", "w+bx", ...). Null on failure, with
// *err set.
FILE* io_platform_fopen(const std::string& path, const char* mode, int* err);

// Set up a stream around a file io.open just opened: a stdio stream (s->fp),
// or, for a pipe, FIFO or device that should be waited on rather than
// blocked on, a waitable stream (s->handle) that takes over the file.
void io_platform_attach_file(IoStream* s, FILE* fp);

// The same for a standard stream (s->readable is already set). The OS
// stream is shared with the parent process: it is never closed, and a
// waitable one stays blocking (s->nonblocking false: read only after a wait).
void io_platform_attach_std(IoStream* s, FILE* fp);

// A waitable stream around our end of a pipe to a child (process module).
// The stream owns the handle, which becomes non-blocking.
void io_platform_attach_pipe(IoStream* s, intptr_t handle);

// Non-blocking reads and writes on a handle: a byte count (0 from a read:
// end of stream), IO_PLATFORM_WOULD_BLOCK or IO_PLATFORM_ERROR.
long long io_platform_read(intptr_t handle, void* buf, size_t len, int* err);
long long io_platform_write(intptr_t handle, const void* buf, size_t len, int* err);

// Close a handle: 0, or an errno value.
int io_platform_close(intptr_t handle);

// Make a handle non-blocking.
void io_platform_set_nonblocking(intptr_t handle);

// Wait until one of `waits` may be ready, like mobius_io_wait (parks the
// fiber): the index of a ready entry (or 0 when the platform can only
// suggest trying again), MOBIUS_IO_TIMEOUT, MOBIUS_IO_CANCELLED or
// MOBIUS_IO_ERROR.
int io_platform_wait(MobiusState* state, const MobiusIoWait* waits, int count, int64_t timeout_ms);

// The next line from a stdio stream, without its "\n" (appended to `line`).
// Returns whether anything was read; *newline whether the line ended with
// "\n". Check ferror(fp) when nothing was read.
bool io_platform_read_line(FILE* fp, std::string& line, bool* newline);

// Positions in stdio streams, 64-bit. As fseek/ftell (errno set on failure).
int io_platform_seek(FILE* fp, int64_t offset, int whence);
int64_t io_platform_tell(FILE* fp);

// The handle under a stdio stream (for a child's standard stream).
intptr_t io_platform_file_handle(FILE* fp);

// Prepare the standard streams (Windows: binary mode, no CRLF translation).
void io_platform_init_std();

#endif // MOBIUS_MODULES_IO_PLATFORM_H
