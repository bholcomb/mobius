#pragma once

namespace mobius_socket_internal {

enum class SocketKind {
    tcp_socket,
    tcp_listener,
    udp_socket,
};

struct SocketObject {
#ifdef _WIN32
    SOCKET handle = INVALID_SOCKET;
#else
    int handle = -1;
#endif
    SocketKind kind = SocketKind::tcp_socket;
    bool closed = false;
};

} // namespace mobius_socket_internal
