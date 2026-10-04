#ifndef MOBIUS_MODULES_IO_STREAM_H
#define MOBIUS_MODULES_IO_STREAM_H

// The stream object behind `io` streams, shared by the plugins that create
// them (io for files and the standard streams, process for child pipes).
// Every plugin that includes this header gets identical layout and
// destructor behavior, so a stream made by one works with the methods of
// the other; the methods themselves live in io and are installed for the
// "io_stream" userdata type by io.mob.
//
// Two kinds of stream:
//   - stdio: regular files, and standard output/error (which share C's
//     buffers with print). Operations go through a FILE*.
//   - waitable: pipes, and where the platform supports it FIFOs, terminals
//     and standard input. Operations use the OS handle directly, and when
//     it isn't ready they wait (io_platform_wait), so a fiber waiting on a
//     pipe parks instead of blocking its worker thread.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "fiber/fiber_mutex.h"
#include "io_platform.h"

static const char* STREAM_TYPE = "io_stream";
static const size_t COPY_CHUNK = 64 * 1024;

enum class LastOp { none, read, write };

struct IoStream {
    // Held for a whole operation, including waits. A fiber mutex: a fiber
    // waiting for it yields rather than blocking its thread.
    FiberMutex mu;
    std::string name;            // path, or "<stdin>" etc.
    bool readable = false;
    bool writable = false;
    bool is_std = false;         // closing never closes the OS stream
    bool closed = false;
    std::atomic<bool> closing{false};   // set by close() before it takes mu

    // stdio streams
    FILE* fp = nullptr;
    LastOp last = LastOp::none;

    // waitable streams
    bool waitable = false;
    intptr_t handle = -1;
    bool nonblocking = false;    // false: wait before each read or write
    std::string rbuf;            // bytes read but not yet returned
    size_t rpos = 0;
    bool eof = false;
};

static void stream_destructor(void* ptr) {
    IoStream* s = static_cast<IoStream*>(ptr);
    if (!s) return;
    if (!s->closed) {
        if (s->waitable) {
            if (!s->is_std && s->handle != -1) io_platform_close(s->handle);
        } else if (s->fp) {
            if (s->is_std) fflush(s->fp);
            else fclose(s->fp);
        }
    }
    delete s;
}

#endif // MOBIUS_MODULES_IO_STREAM_H
