#ifndef MOBIUS_MODULES_SOCKET_PLATFORM_H
#define MOBIUS_MODULES_SOCKET_PLATFORM_H

// Sockets on Linux and macOS: the BSD socket API plus the few calls that
// differ on Windows (socket/win32/socket_platform.h). The build puts the
// platform's directory on the include path.

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef int mobius_socket_handle;
static const mobius_socket_handle MOBIUS_INVALID_SOCKET_HANDLE = -1;

#ifdef MSG_NOSIGNAL
static const int SEND_FLAGS = MSG_NOSIGNAL;   // a closed peer is an error, not SIGPIPE
#else
static const int SEND_FLAGS = 0;              // macOS: SO_NOSIGPIPE, set per socket
#endif

static inline bool socket_platform_init() { return true; }
static inline void socket_platform_cleanup() {}

static inline int socket_last_error_code() { return errno; }

static inline bool socket_error_is_timeout(int code) {
    return code == EAGAIN || code == EWOULDBLOCK || code == ETIMEDOUT;
}

static inline std::string socket_error_text(int code) { return strerror(code); }

static inline void socket_close_handle(mobius_socket_handle handle) {
    if (handle != MOBIUS_INVALID_SOCKET_HANDLE) close(handle);
}

// Non-blocking, closed in child processes, and (macOS) no SIGPIPE.
static inline void socket_set_nonblocking(mobius_socket_handle handle) {
    int flags = fcntl(handle, F_GETFL);
    if (flags >= 0) fcntl(handle, F_SETFL, flags | O_NONBLOCK);
    fcntl(handle, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

static inline bool socket_would_block(int code) { return code == EAGAIN || code == EWOULDBLOCK; }

// A non-blocking connect() that is still in progress.
static inline bool socket_connect_pending(int code) { return code == EINPROGRESS; }

// A call interrupted by a signal: retry it.
static inline bool socket_interrupted(int code) { return code == EINTR; }

// accept() of a connection the peer already dropped: take the next one.
static inline bool socket_accept_aborted(int code) { return code == ECONNABORTED; }

// shutdown()'s `how` for the directions to close (read and/or write).
static inline int socket_shutdown_how(bool read, bool write) {
    return read && write ? SHUT_RDWR : read ? SHUT_RD : SHUT_WR;
}

static inline long long sock_send(mobius_socket_handle h, const void* data, size_t len, int flags) {
    return send(h, data, len, flags);
}

static inline long long sock_recv(mobius_socket_handle h, void* data, size_t len) {
    return recv(h, data, len, 0);
}

static inline long long sock_sendto(mobius_socket_handle h, const void* data, size_t len, int flags,
                                    const sockaddr* addr, size_t addr_len) {
    return sendto(h, data, len, flags, addr, (socklen_t)addr_len);
}

static inline long long sock_recvfrom(mobius_socket_handle h, void* data, size_t len, sockaddr* addr,
                                      socklen_t* addr_len) {
    return recvfrom(h, data, len, 0, addr, addr_len);
}

#endif // MOBIUS_MODULES_SOCKET_PLATFORM_H
