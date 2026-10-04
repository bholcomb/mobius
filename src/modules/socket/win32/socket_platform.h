#ifndef MOBIUS_MODULES_SOCKET_PLATFORM_H
#define MOBIUS_MODULES_SOCKET_PLATFORM_H

// Sockets on Windows (Winsock): the BSD socket API plus the few calls that
// differ from Linux and macOS (socket/posix/socket_platform.h). The build
// puts the platform's directory on the include path.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>

#pragma comment(lib, "Ws2_32.lib")

typedef SOCKET mobius_socket_handle;
static const mobius_socket_handle MOBIUS_INVALID_SOCKET_HANDLE = INVALID_SOCKET;

static const int SEND_FLAGS = 0;   // no SIGPIPE on Windows

static bool g_winsock_initialized = false;

static inline bool socket_platform_init() {
    if (g_winsock_initialized) return true;
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
    g_winsock_initialized = true;
    return true;
}

static inline void socket_platform_cleanup() {
    if (g_winsock_initialized) {
        WSACleanup();
        g_winsock_initialized = false;
    }
}

static inline int socket_last_error_code() { return WSAGetLastError(); }

static inline bool socket_error_is_timeout(int code) { return code == WSAETIMEDOUT; }

static inline std::string socket_error_text(int code) {
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, (DWORD)code, 0, (LPSTR)&msg, 0, nullptr);
    std::string text = msg ? msg : "error " + std::to_string(code);
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) text.pop_back();
    return text;
}

static inline void socket_close_handle(mobius_socket_handle handle) {
    if (handle != MOBIUS_INVALID_SOCKET_HANDLE) closesocket(handle);
}

// Non-blocking (waits go through the WSAPoll reactor), and not inherited
// by child processes.
static inline void socket_set_nonblocking(mobius_socket_handle handle) {
    u_long on = 1;
    ioctlsocket(handle, FIONBIO, &on);
    SetHandleInformation((HANDLE)handle, HANDLE_FLAG_INHERIT, 0);
}

static inline bool socket_would_block(int code) { return code == WSAEWOULDBLOCK; }

// A non-blocking connect() that is still in progress.
static inline bool socket_connect_pending(int code) { return code == WSAEWOULDBLOCK; }

// A call interrupted (by WSACancelBlockingCall): retry it.
static inline bool socket_interrupted(int code) { return code == WSAEINTR; }

// accept() of a connection the peer already dropped: take the next one.
static inline bool socket_accept_aborted(int code) { return code == WSAECONNRESET; }

// shutdown()'s `how` for the directions to close (read and/or write).
static inline int socket_shutdown_how(bool read, bool write) {
    return read && write ? SD_BOTH : read ? SD_RECEIVE : SD_SEND;
}

static inline long long sock_send(mobius_socket_handle h, const void* data, size_t len, int flags) {
    return send(h, (const char*)data, (int)len, flags);
}

static inline long long sock_recv(mobius_socket_handle h, void* data, size_t len) {
    return recv(h, (char*)data, (int)len, 0);
}

static inline long long sock_sendto(mobius_socket_handle h, const void* data, size_t len, int flags,
                                    const sockaddr* addr, size_t addr_len) {
    return sendto(h, (const char*)data, (int)len, flags, addr, (int)addr_len);
}

static inline long long sock_recvfrom(mobius_socket_handle h, void* data, size_t len, sockaddr* addr,
                                      socklen_t* addr_len) {
    return recvfrom(h, (char*)data, (int)len, 0, addr, addr_len);
}

#endif // MOBIUS_MODULES_SOCKET_PLATFORM_H
