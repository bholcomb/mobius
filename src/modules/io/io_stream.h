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
//   - waitable: pipes, terminals, FIFOs and standard input. Operations use
//     the descriptor directly, and when it isn't ready they wait in the
//     fiber-aware I/O reactor (mobius_io_wait), so a fiber waiting on a
//     pipe parks instead of blocking its worker thread.

#include <atomic>
#include <cstdio>
#include <string>

#include <unistd.h>

#include "fiber/fiber_mutex.h"

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
    int fd = -1;
    bool nonblocking = false;    // fd is O_NONBLOCK (descriptors we own)
    std::string rbuf;            // bytes read but not yet returned
    size_t rpos = 0;
    bool eof = false;
};

static void stream_destructor(void* ptr) {
    IoStream* s = static_cast<IoStream*>(ptr);
    if (!s) return;
    if (!s->closed) {
        if (s->waitable) {
            if (!s->is_std && s->fd >= 0) close(s->fd);
        } else if (s->fp) {
            if (s->is_std) fflush(s->fp);
            else fclose(s->fp);
        }
    }
    delete s;
}

#endif // MOBIUS_MODULES_IO_STREAM_H
