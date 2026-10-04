// Linux and macOS implementation of io_platform.h: file descriptors,
// waited on in the fiber-aware I/O reactor.

#include "io_stream.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

FILE* io_platform_fopen(const std::string& path, const char* mode, int* err) {
    errno = 0;
    FILE* fp = fopen(path.c_str(), mode);
    if (!fp) *err = errno ? errno : EIO;
    return fp;
}

void io_platform_attach_file(IoStream* s, FILE* fp) {
    struct stat st;
    if (fstat(fileno(fp), &st) == 0 && !S_ISREG(st.st_mode) && !S_ISBLK(st.st_mode)) {
        // A FIFO or device: use the descriptor, non-blocking (we own it).
        s->waitable = true;
        s->handle = fcntl(fileno(fp), F_DUPFD_CLOEXEC, 0);
        fclose(fp);
        io_platform_set_nonblocking(s->handle);
        s->nonblocking = true;
    } else {
        s->fp = fp;
    }
}

void io_platform_attach_std(IoStream* s, FILE* fp) {
    struct stat st;
    if (s->readable && fstat(fileno(fp), &st) == 0 && !S_ISREG(st.st_mode)) {
        // Standard input from a pipe or terminal: wait for it in the
        // reactor. The descriptor stays blocking (it is shared with the
        // parent process, e.g. a shell's terminal) and is read only once
        // it reports readable.
        s->waitable = true;
        s->handle = fileno(fp);
    } else {
        s->fp = fp;
    }
}

void io_platform_attach_pipe(IoStream* s, intptr_t handle) {
    io_platform_set_nonblocking(handle);
    s->waitable = true;
    s->handle = handle;
    s->nonblocking = true;
}

long long io_platform_read(intptr_t handle, void* buf, size_t len, int* err) {
    while (true) {
        ssize_t n = read((int)handle, buf, len);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return IO_PLATFORM_WOULD_BLOCK;
        *err = errno;
        return IO_PLATFORM_ERROR;
    }
}

long long io_platform_write(intptr_t handle, const void* buf, size_t len, int* err) {
    while (true) {
        ssize_t n = write((int)handle, buf, len);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return IO_PLATFORM_WOULD_BLOCK;
        *err = errno;
        return IO_PLATFORM_ERROR;
    }
}

int io_platform_close(intptr_t handle) {
    return close((int)handle) == 0 ? 0 : errno;
}

void io_platform_set_nonblocking(intptr_t handle) {
    int fd = (int)handle;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

int io_platform_wait(MobiusState* state, const MobiusIoWait* waits, int count, int64_t timeout_ms) {
    return mobius_io_wait(state, waits, count, timeout_ms);
}

bool io_platform_read_line(FILE* fp, std::string& line, bool* newline) {
    *newline = false;
    bool got_any = false;
    flockfile(fp);
    int c;
    while ((c = getc_unlocked(fp)) != EOF) {
        got_any = true;
        if (c == '\n') { *newline = true; break; }
        line.push_back((char)c);
    }
    funlockfile(fp);
    return got_any;
}

int io_platform_seek(FILE* fp, int64_t offset, int whence) {
    return fseeko(fp, (off_t)offset, whence);
}

int64_t io_platform_tell(FILE* fp) {
    return (int64_t)ftello(fp);
}

intptr_t io_platform_file_handle(FILE* fp) {
    return fileno(fp);
}

void io_platform_init_std() {}
