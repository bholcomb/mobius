#ifndef MOBIUS_MODULES_IO_STREAM_H
#define MOBIUS_MODULES_IO_STREAM_H

// The stream object behind `io` streams, shared by the plugins that create
// them (io for files and the standard streams, process for child pipes).
// Every plugin that includes this header gets identical layout and
// destructor behavior, so a stream made by one works with the methods of
// the other; the methods themselves live in io and are installed for the
// "io_stream" userdata type by io.mob.

#include <cstdio>
#include <mutex>
#include <string>

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

#endif // MOBIUS_MODULES_IO_STREAM_H
