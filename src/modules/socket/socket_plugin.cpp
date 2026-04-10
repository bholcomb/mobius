#include <mobius/mobius_plugin.h>

#include <cinttypes>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "modules/socket/socket_internal.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "Ws2_32.lib")
  #endif
  typedef SOCKET mobius_socket_handle;
  static const mobius_socket_handle MOBIUS_INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
  typedef int mobius_socket_handle;
  static const mobius_socket_handle MOBIUS_INVALID_SOCKET_HANDLE = -1;
#endif

namespace {

static const char* TCP_SOCKET_TYPE = "tcp_socket";
static const char* TCP_LISTENER_TYPE = "tcp_listener";
static const char* UDP_SOCKET_TYPE = "udp_socket";
using mobius_socket_internal::SocketKind;
using mobius_socket_internal::SocketObject;

#ifdef _WIN32
static bool g_winsock_initialized = false;
#endif

static bool socket_platform_init() {
#ifdef _WIN32
    if (g_winsock_initialized) return true;
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
    g_winsock_initialized = true;
#endif
    return true;
}

static void socket_platform_cleanup() {
#ifdef _WIN32
    if (g_winsock_initialized) {
        WSACleanup();
        g_winsock_initialized = false;
    }
#endif
}

static int socket_last_error_code() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool socket_error_is_timeout(int code) {
#ifdef _WIN32
    return code == WSAETIMEDOUT;
#else
    return code == EAGAIN || code == EWOULDBLOCK || code == ETIMEDOUT;
#endif
}

static void socket_close_handle(mobius_socket_handle handle) {
    if (handle == MOBIUS_INVALID_SOCKET_HANDLE) return;
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

static void socket_object_destructor(void* ptr) {
    SocketObject* obj = static_cast<SocketObject*>(ptr);
    if (!obj) return;
    if (!obj->closed) socket_close_handle(obj->handle);
    delete obj;
}

static std::string socket_error_message(const char* prefix, int code) {
    char buffer[256];
#ifdef _WIN32
    snprintf(buffer, sizeof(buffer), "%s failed (code %d)", prefix, code);
#else
    snprintf(buffer, sizeof(buffer), "%s failed: %s", prefix, strerror(code));
#endif
    return std::string(buffer);
}

static bool socket_object_is_listener(const SocketObject* obj) {
    return obj && obj->kind == SocketKind::tcp_listener;
}

static bool socket_object_is_udp(const SocketObject* obj) {
    return obj && obj->kind == SocketKind::udp_socket;
}

static SocketObject* get_socket_object(MobiusState* state, int idx, const char* expected_type) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, expected_type) != 0) return nullptr;
    return static_cast<SocketObject*>(ptr);
}

static bool value_to_bytes(MobiusState* state, int idx, std::vector<uint8_t>& out) {
    out.clear();
    if (mobius_stack_isBuffer(state, idx)) {
        size_t len = 0;
        void* data = mobius_stack_getBufferData(state, idx, &len);
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        if (len > 0) out.assign(ptr, ptr + len);
        return true;
    }
    if (mobius_stack_isString(state, idx)) {
        size_t len = 0;
        const char* data = mobius_stack_getStringData(state, idx, &len);
        if (len > 0) out.assign((const uint8_t*)data, (const uint8_t*)data + len);
        return true;
    }
    return false;
}

static void set_string_field(MobiusState* state, int tbl_idx, const char* key, const std::string& value) {
    mobius_stack_pushStringLength(state, value.data(), value.size());
    mobius_stack_setTableField(state, tbl_idx, key);
}

static void set_int_field(MobiusState* state, int tbl_idx, const char* key, int64_t value) {
    mobius_stack_pushInt64(state, value);
    mobius_stack_setTableField(state, tbl_idx, key);
}

static int push_addr_table(MobiusState* state, const sockaddr_storage& storage, socklen_t length) {
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    int rc = getnameinfo((const sockaddr*)&storage, length,
                         host, sizeof(host), service, sizeof(service),
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) return mobius_error(state, "socket address formatting failed");

    mobius_stack_pushNewTable(state, 3);
    int tbl = mobius_stack_size(state) - 1;
    set_string_field(state, tbl, "host", host);
    set_int_field(state, tbl, "port", strtoll(service, nullptr, 10));
    if (storage.ss_family == AF_INET) set_string_field(state, tbl, "family", "ipv4");
    else if (storage.ss_family == AF_INET6) set_string_field(state, tbl, "family", "ipv6");
    else set_string_field(state, tbl, "family", "unknown");
    return 1;
}

static int get_socket_name_table(MobiusState* state, SocketObject* obj, bool peer) {
    sockaddr_storage storage;
    socklen_t len = (socklen_t)sizeof(storage);
    memset(&storage, 0, sizeof(storage));
    int rc = peer
        ? getpeername(obj->handle, (sockaddr*)&storage, &len)
        : getsockname(obj->handle, (sockaddr*)&storage, &len);
    if (rc != 0) {
        return mobius_error(state, socket_error_message(peer ? "socket.peer_addr()" : "socket.local_addr()",
                                                        socket_last_error_code()).c_str());
    }
    return push_addr_table(state, storage, len);
}

static bool resolve_socket_addresses(const std::string& host, int64_t port,
                                     int socktype, int protocol, int flags,
                                     addrinfo** out_results, std::string& error,
                                     const char* context) {
    *out_results = nullptr;
    char port_buffer[32];
    snprintf(port_buffer, sizeof(port_buffer), "%" PRId64, port);

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;
    hints.ai_flags = flags;

    int rc = getaddrinfo(host.empty() ? nullptr : host.c_str(), port_buffer, &hints, out_results);
    if (rc != 0 || !*out_results) {
        error = std::string(context) + " address resolution failed";
        return false;
    }
    return true;
}

static int apply_socket_timeout(SocketObject* obj, int64_t timeout_ms) {
#ifdef _WIN32
    DWORD timeout = (timeout_ms < 0) ? 0 : (DWORD)timeout_ms;
    int rc1 = setsockopt(obj->handle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    int rc2 = setsockopt(obj->handle, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (int)((timeout_ms % 1000) * 1000);
    int rc1 = setsockopt(obj->handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int rc2 = setsockopt(obj->handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return (rc1 == 0 && rc2 == 0) ? 0 : -1;
}

static int push_socket_userdata(MobiusState* state, mobius_socket_handle handle,
                                SocketKind kind, const char* type_name) {
    SocketObject* obj = new (std::nothrow) SocketObject();
    if (!obj) {
        socket_close_handle(handle);
        return mobius_error(state, "failed to allocate socket object");
    }
    obj->handle = handle;
    obj->kind = kind;
    obj->closed = false;
    mobius_stack_pushUserdata(state, obj, socket_object_destructor, type_name, sizeof(SocketObject));
    return 1;
}

static int socket_connect(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket.connect() expects 2 arguments (host, port)");
    if (!mobius_stack_isString(state, -2)) return mobius_error(state, "socket.connect() host must be a string");

    size_t host_len = 0;
    const char* host_data = mobius_stack_getStringData(state, -2, &host_len);
    std::string host(host_data ? host_data : "", host_len);
    int64_t port = mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 2);
    if (port < 0 || port > 65535) return mobius_error(state, "socket.connect() port must be in [0, 65535]");
    if (!socket_platform_init()) return mobius_error(state, "socket.connect() socket platform init failed");

    char port_buffer[32];
    snprintf(port_buffer, sizeof(port_buffer), "%" PRId64, port);

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    int rc = getaddrinfo(host.c_str(), port_buffer, &hints, &results);
    if (rc != 0 || !results) return mobius_error(state, "socket.connect() address resolution failed");

    mobius_socket_handle handle = MOBIUS_INVALID_SOCKET_HANDLE;
    int last_error = 0;
    for (addrinfo* it = results; it; it = it->ai_next) {
        handle = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
            last_error = socket_last_error_code();
            continue;
        }
        if (::connect(handle, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) {
            break;
        }
        last_error = socket_last_error_code();
        socket_close_handle(handle);
        handle = MOBIUS_INVALID_SOCKET_HANDLE;
    }
    freeaddrinfo(results);

    if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
        return mobius_error(state, socket_error_message("socket.connect()", last_error).c_str());
    }

    int yes = 1;
    setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));
    return push_socket_userdata(state, handle, SocketKind::tcp_socket, TCP_SOCKET_TYPE);
}

static int socket_listen(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return mobius_error(state, "socket.listen() expects 2 or 3 arguments (host, port [, backlog])");
    }
    if (!mobius_stack_isString(state, -arg_count)) return mobius_error(state, "socket.listen() host must be a string");

    int64_t backlog = 128;
    if (arg_count == 3) backlog = mobius_stack_asInt64(state, -1);
    int64_t port = mobius_stack_asInt64(state, 1 - arg_count);
    size_t host_len = 0;
    const char* host_data = mobius_stack_getStringData(state, -arg_count, &host_len);
    std::string host(host_data ? host_data : "", host_len);
    mobius_stack_pop(state, arg_count);

    if (port < 0 || port > 65535) return mobius_error(state, "socket.listen() port must be in [0, 65535]");
    if (backlog <= 0) return mobius_error(state, "socket.listen() backlog must be > 0");
    if (!socket_platform_init()) return mobius_error(state, "socket.listen() socket platform init failed");

    char port_buffer[32];
    snprintf(port_buffer, sizeof(port_buffer), "%" PRId64, port);

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    int rc = getaddrinfo(host.empty() ? nullptr : host.c_str(), port_buffer, &hints, &results);
    if (rc != 0 || !results) return mobius_error(state, "socket.listen() address resolution failed");

    mobius_socket_handle handle = MOBIUS_INVALID_SOCKET_HANDLE;
    int last_error = 0;
    for (addrinfo* it = results; it; it = it->ai_next) {
        handle = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
            last_error = socket_last_error_code();
            continue;
        }
        int reuse = 1;
        setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        if (bind(handle, it->ai_addr, (socklen_t)it->ai_addrlen) == 0 &&
            listen(handle, (int)backlog) == 0) {
            break;
        }
        last_error = socket_last_error_code();
        socket_close_handle(handle);
        handle = MOBIUS_INVALID_SOCKET_HANDLE;
    }
    freeaddrinfo(results);

    if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
        return mobius_error(state, socket_error_message("socket.listen()", last_error).c_str());
    }

    return push_socket_userdata(state, handle, SocketKind::tcp_listener, TCP_LISTENER_TYPE);
}

static int socket_udp(MobiusState* state, int arg_count) {
    std::string host;
    int64_t port = 0;
    if (arg_count != 0 && arg_count != 2) {
        return mobius_error(state, "socket.udp() expects 0 or 2 arguments ([host, port])");
    }
    if (arg_count == 2) {
        if (!mobius_stack_isString(state, -2)) return mobius_error(state, "socket.udp() host must be a string");
        size_t host_len = 0;
        const char* host_data = mobius_stack_getStringData(state, -2, &host_len);
        host.assign(host_data ? host_data : "", host_len);
        port = mobius_stack_asInt64(state, -1);
    }
    mobius_stack_pop(state, arg_count);
    if (port < 0 || port > 65535) return mobius_error(state, "socket.udp() port must be in [0, 65535]");
    if (!socket_platform_init()) return mobius_error(state, "socket.udp() socket platform init failed");

    if (arg_count == 0) {
        mobius_socket_handle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
            return mobius_error(state, socket_error_message("socket.udp()", socket_last_error_code()).c_str());
        }
        return push_socket_userdata(state, handle, SocketKind::udp_socket, UDP_SOCKET_TYPE);
    }

    addrinfo* results = nullptr;
    std::string error;
    if (!resolve_socket_addresses(host, port, SOCK_DGRAM, IPPROTO_UDP, AI_PASSIVE, &results, error, "socket.udp()")) {
        return mobius_error(state, error.c_str());
    }

    mobius_socket_handle handle = MOBIUS_INVALID_SOCKET_HANDLE;
    int last_error = 0;
    for (addrinfo* it = results; it; it = it->ai_next) {
        handle = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
            last_error = socket_last_error_code();
            continue;
        }
        int reuse = 1;
        setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        if (bind(handle, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) break;
        last_error = socket_last_error_code();
        socket_close_handle(handle);
        handle = MOBIUS_INVALID_SOCKET_HANDLE;
    }
    freeaddrinfo(results);
    if (handle == MOBIUS_INVALID_SOCKET_HANDLE) {
        return mobius_error(state, socket_error_message("socket.udp()", last_error).c_str());
    }
    return push_socket_userdata(state, handle, SocketKind::udp_socket, UDP_SOCKET_TYPE);
}

static int socket_base_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "socket:close() expects 0 arguments");
    const char* type_name = nullptr;
    SocketObject* obj = static_cast<SocketObject*>(mobius_stack_getUserdata(state, 0, &type_name));
    if (!obj || !type_name) return mobius_error(state, "socket:close() self is not a socket");
    mobius_stack_pop(state, 1);
    if (!obj->closed) {
        socket_close_handle(obj->handle);
        obj->handle = MOBIUS_INVALID_SOCKET_HANDLE;
        obj->closed = true;
    }
    mobius_stack_pushBool(state, true);
    return 1;
}

static int socket_base_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "socket:is_closed() expects 0 arguments");
    const char* type_name = nullptr;
    SocketObject* obj = static_cast<SocketObject*>(mobius_stack_getUserdata(state, 0, &type_name));
    if (!obj || !type_name) return mobius_error(state, "socket:is_closed() self is not a socket");
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, obj->closed);
    return 1;
}

static int socket_base_set_timeout(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket:set_timeout() expects 1 argument (milliseconds)");
    const char* type_name = nullptr;
    SocketObject* obj = static_cast<SocketObject*>(mobius_stack_getUserdata(state, 0, &type_name));
    if (!obj || !type_name) return mobius_error(state, "socket:set_timeout() self is not a socket");
    int64_t timeout_ms = mobius_stack_asInt64(state, -1);
    if (timeout_ms < 0) return mobius_error(state, "socket:set_timeout() timeout must be >= 0");
    if (obj->closed) return mobius_error(state, "socket:set_timeout() socket is closed");
    if (apply_socket_timeout(obj, timeout_ms) != 0) {
        return mobius_error(state, socket_error_message("socket:set_timeout()", socket_last_error_code()).c_str());
    }
    mobius_stack_copy(state, 0);
    mobius_stack_pop(state, 2);
    return 1;
}

static int socket_base_local_addr(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "socket:local_addr() expects 0 arguments");
    const char* type_name = nullptr;
    SocketObject* obj = static_cast<SocketObject*>(mobius_stack_getUserdata(state, 0, &type_name));
    if (!obj || !type_name) return mobius_error(state, "socket:local_addr() self is not a socket");
    if (obj->closed) return mobius_error(state, "socket:local_addr() socket is closed");
    mobius_stack_pop(state, 1);
    return get_socket_name_table(state, obj, false);
}

static int socket_base_peer_addr(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "socket:peer_addr() expects 0 arguments");
    const char* type_name = nullptr;
    SocketObject* obj = static_cast<SocketObject*>(mobius_stack_getUserdata(state, 0, &type_name));
    if (!obj || !type_name || socket_object_is_listener(obj)) {
        return mobius_error(state, "socket:peer_addr() self is not a connected socket");
    }
    if (obj->closed) return mobius_error(state, "socket:peer_addr() socket is closed");
    mobius_stack_pop(state, 1);
    return get_socket_name_table(state, obj, true);
}

static int tcp_socket_send(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket:send() expects 1 argument");
    SocketObject* obj = get_socket_object(state, 0, TCP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:send() self is not a tcp socket");
    if (obj->closed) return mobius_error(state, "socket:send() socket is closed");

    std::vector<uint8_t> bytes;
    if (mobius_stack_isBuffer(state, -1)) {
        size_t len = 0;
        void* data = mobius_stack_getBufferData(state, -1, &len);
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        if (len > 0) bytes.assign(ptr, ptr + len);
    } else if (mobius_stack_isString(state, -1)) {
        size_t len = 0;
        const char* data = mobius_stack_getStringData(state, -1, &len);
        if (len > 0) bytes.assign((const uint8_t*)data, (const uint8_t*)data + len);
    } else {
        return mobius_error(state, "socket:send() expects a buffer or string");
    }

    size_t sent = 0;
    while (sent < bytes.size()) {
#ifdef _WIN32
        int rc = send(obj->handle, (const char*)bytes.data() + sent, (int)(bytes.size() - sent), 0);
#else
        ssize_t rc = send(obj->handle, bytes.data() + sent, bytes.size() - sent, 0);
#endif
        if (rc <= 0) {
            int err = socket_last_error_code();
            if (socket_error_is_timeout(err)) return mobius_error(state, "socket:send() timed out");
            return mobius_error(state, socket_error_message("socket:send()", err).c_str());
        }
        sent += (size_t)rc;
    }

    mobius_stack_pop(state, 2);
    mobius_stack_pushInt64(state, (int64_t)sent);
    return 1;
}

static int tcp_socket_recv(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket:recv() expects 1 argument (max_bytes)");
    SocketObject* obj = get_socket_object(state, 0, TCP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:recv() self is not a tcp socket");
    if (obj->closed) return mobius_error(state, "socket:recv() socket is closed");
    int64_t max_bytes = mobius_stack_asInt64(state, -1);
    if (max_bytes <= 0) return mobius_error(state, "socket:recv() max_bytes must be > 0");

    std::vector<uint8_t> buffer((size_t)max_bytes);
#ifdef _WIN32
    int rc = recv(obj->handle, (char*)buffer.data(), (int)buffer.size(), 0);
#else
    ssize_t rc = recv(obj->handle, buffer.data(), buffer.size(), 0);
#endif
    if (rc == 0) {
        mobius_stack_pop(state, 2);
        mobius_stack_pushNil(state);
        return 1;
    }
    if (rc < 0) {
        int err = socket_last_error_code();
        if (socket_error_is_timeout(err)) return mobius_error(state, "socket:recv() timed out");
        return mobius_error(state, socket_error_message("socket:recv()", err).c_str());
    }

    mobius_stack_pop(state, 2);
    mobius_stack_pushBufferCopy(state, buffer.data(), (size_t)rc);
    return 1;
}

static int tcp_socket_shutdown(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 3) {
        return mobius_error(state, "socket:shutdown() expects 0, 1, or 2 boolean arguments");
    }
    SocketObject* obj = get_socket_object(state, 0, TCP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:shutdown() self is not a tcp socket");
    if (obj->closed) return mobius_error(state, "socket:shutdown() socket is closed");

    bool read = true;
    bool write = true;
    if (arg_count == 2) {
        read = mobius_stack_asBool(state, -1);
        write = read;
    } else if (arg_count == 3) {
        read = mobius_stack_asBool(state, -2);
        write = mobius_stack_asBool(state, -1);
    }

    int how = 0;
#ifdef _WIN32
    if (read && write) how = SD_BOTH;
    else if (read) how = SD_RECEIVE;
    else if (write) how = SD_SEND;
    else return mobius_error(state, "socket:shutdown() requires read or write");
#else
    if (read && write) how = SHUT_RDWR;
    else if (read) how = SHUT_RD;
    else if (write) how = SHUT_WR;
    else return mobius_error(state, "socket:shutdown() requires read or write");
#endif
    if (shutdown(obj->handle, how) != 0) {
        return mobius_error(state, socket_error_message("socket:shutdown()", socket_last_error_code()).c_str());
    }
    mobius_stack_copy(state, 0);
    mobius_stack_pop(state, arg_count);
    return 1;
}

static int tcp_listener_accept(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "socket:accept() expects 0 arguments");
    SocketObject* obj = get_socket_object(state, 0, TCP_LISTENER_TYPE);
    if (!obj) return mobius_error(state, "socket:accept() self is not a tcp listener");
    if (obj->closed) return mobius_error(state, "socket:accept() listener is closed");

    sockaddr_storage storage;
    socklen_t len = (socklen_t)sizeof(storage);
#ifdef _WIN32
    SOCKET client = accept(obj->handle, (sockaddr*)&storage, &len);
#else
    int client = accept(obj->handle, (sockaddr*)&storage, &len);
#endif
    if (client == MOBIUS_INVALID_SOCKET_HANDLE) {
        int err = socket_last_error_code();
        if (socket_error_is_timeout(err)) return mobius_error(state, "socket:accept() timed out");
        return mobius_error(state, socket_error_message("socket:accept()", err).c_str());
    }

    mobius_stack_pop(state, 1);
    return push_socket_userdata(state, client, SocketKind::tcp_socket, TCP_SOCKET_TYPE);
}

static int udp_socket_connect(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "socket:connect() expects 2 arguments (host, port)");
    SocketObject* obj = get_socket_object(state, 0, UDP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:connect() self is not a udp socket");
    if (obj->closed) return mobius_error(state, "socket:connect() socket is closed");
    if (!mobius_stack_isString(state, -2)) return mobius_error(state, "socket:connect() host must be a string");

    size_t host_len = 0;
    const char* host_data = mobius_stack_getStringData(state, -2, &host_len);
    std::string host(host_data ? host_data : "", host_len);
    int64_t port = mobius_stack_asInt64(state, -1);
    if (port < 0 || port > 65535) return mobius_error(state, "socket:connect() port must be in [0, 65535]");

    addrinfo* results = nullptr;
    std::string error;
    if (!resolve_socket_addresses(host, port, SOCK_DGRAM, IPPROTO_UDP, 0, &results, error, "socket:connect()")) {
        return mobius_error(state, error.c_str());
    }
    int last_error = 0;
    bool ok = false;
    for (addrinfo* it = results; it; it = it->ai_next) {
        if (::connect(obj->handle, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) {
            ok = true;
            break;
        }
        last_error = socket_last_error_code();
    }
    freeaddrinfo(results);
    if (!ok) return mobius_error(state, socket_error_message("socket:connect()", last_error).c_str());
    mobius_stack_copy(state, 0);
    mobius_stack_pop(state, 3);
    return 1;
}

static int udp_socket_send(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket:send() expects 1 argument");
    SocketObject* obj = get_socket_object(state, 0, UDP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:send() self is not a udp socket");
    if (obj->closed) return mobius_error(state, "socket:send() socket is closed");

    std::vector<uint8_t> bytes;
    if (!value_to_bytes(state, -1, bytes)) return mobius_error(state, "socket:send() expects a buffer or string");
#ifdef _WIN32
    int rc = send(obj->handle, (const char*)bytes.data(), (int)bytes.size(), 0);
#else
    ssize_t rc = send(obj->handle, bytes.data(), bytes.size(), 0);
#endif
    if (rc < 0) {
        int err = socket_last_error_code();
        if (socket_error_is_timeout(err)) return mobius_error(state, "socket:send() timed out");
        return mobius_error(state, socket_error_message("socket:send()", err).c_str());
    }
    mobius_stack_pop(state, 2);
    mobius_stack_pushInt64(state, (int64_t)rc);
    return 1;
}

static int udp_socket_recv(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket:recv() expects 1 argument (max_bytes)");
    SocketObject* obj = get_socket_object(state, 0, UDP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:recv() self is not a udp socket");
    if (obj->closed) return mobius_error(state, "socket:recv() socket is closed");
    int64_t max_bytes = mobius_stack_asInt64(state, -1);
    if (max_bytes <= 0) return mobius_error(state, "socket:recv() max_bytes must be > 0");

    std::vector<uint8_t> buffer((size_t)max_bytes);
#ifdef _WIN32
    int rc = recv(obj->handle, (char*)buffer.data(), (int)buffer.size(), 0);
#else
    ssize_t rc = recv(obj->handle, buffer.data(), buffer.size(), 0);
#endif
    if (rc < 0) {
        int err = socket_last_error_code();
        if (socket_error_is_timeout(err)) return mobius_error(state, "socket:recv() timed out");
        return mobius_error(state, socket_error_message("socket:recv()", err).c_str());
    }
    mobius_stack_pop(state, 2);
    mobius_stack_pushBufferCopy(state, buffer.data(), (size_t)rc);
    return 1;
}

static int udp_socket_send_to(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "socket:send_to() expects 3 arguments (host, port, data)");
    SocketObject* obj = get_socket_object(state, 0, UDP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:send_to() self is not a udp socket");
    if (obj->closed) return mobius_error(state, "socket:send_to() socket is closed");
    if (!mobius_stack_isString(state, -3)) return mobius_error(state, "socket:send_to() host must be a string");

    size_t host_len = 0;
    const char* host_data = mobius_stack_getStringData(state, -3, &host_len);
    std::string host(host_data ? host_data : "", host_len);
    int64_t port = mobius_stack_asInt64(state, -2);
    if (port < 0 || port > 65535) return mobius_error(state, "socket:send_to() port must be in [0, 65535]");

    std::vector<uint8_t> bytes;
    if (!value_to_bytes(state, -1, bytes)) return mobius_error(state, "socket:send_to() expects a buffer or string payload");

    addrinfo* results = nullptr;
    std::string error;
    if (!resolve_socket_addresses(host, port, SOCK_DGRAM, IPPROTO_UDP, 0, &results, error, "socket:send_to()")) {
        return mobius_error(state, error.c_str());
    }
    int last_error = 0;
    ssize_t sent = -1;
    for (addrinfo* it = results; it; it = it->ai_next) {
#ifdef _WIN32
        int rc = sendto(obj->handle, (const char*)bytes.data(), (int)bytes.size(), 0, it->ai_addr, (int)it->ai_addrlen);
#else
        ssize_t rc = sendto(obj->handle, bytes.data(), bytes.size(), 0, it->ai_addr, (socklen_t)it->ai_addrlen);
#endif
        if (rc >= 0) {
            sent = rc;
            break;
        }
        last_error = socket_last_error_code();
    }
    freeaddrinfo(results);
    if (sent < 0) {
        if (socket_error_is_timeout(last_error)) return mobius_error(state, "socket:send_to() timed out");
        return mobius_error(state, socket_error_message("socket:send_to()", last_error).c_str());
    }
    mobius_stack_pop(state, 4);
    mobius_stack_pushInt64(state, (int64_t)sent);
    return 1;
}

static int udp_socket_recv_from(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "socket:recv_from() expects 1 argument (max_bytes)");
    SocketObject* obj = get_socket_object(state, 0, UDP_SOCKET_TYPE);
    if (!obj) return mobius_error(state, "socket:recv_from() self is not a udp socket");
    if (obj->closed) return mobius_error(state, "socket:recv_from() socket is closed");
    int64_t max_bytes = mobius_stack_asInt64(state, -1);
    if (max_bytes <= 0) return mobius_error(state, "socket:recv_from() max_bytes must be > 0");

    std::vector<uint8_t> buffer((size_t)max_bytes);
    sockaddr_storage storage;
    socklen_t addr_len = (socklen_t)sizeof(storage);
    memset(&storage, 0, sizeof(storage));
#ifdef _WIN32
    int rc = recvfrom(obj->handle, (char*)buffer.data(), (int)buffer.size(), 0, (sockaddr*)&storage, &addr_len);
#else
    ssize_t rc = recvfrom(obj->handle, buffer.data(), buffer.size(), 0, (sockaddr*)&storage, &addr_len);
#endif
    if (rc < 0) {
        int err = socket_last_error_code();
        if (socket_error_is_timeout(err)) return mobius_error(state, "socket:recv_from() timed out");
        return mobius_error(state, socket_error_message("socket:recv_from()", err).c_str());
    }

    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    int info_rc = getnameinfo((const sockaddr*)&storage, addr_len, host, sizeof(host), service, sizeof(service),
                              NI_NUMERICHOST | NI_NUMERICSERV);
    if (info_rc != 0) return mobius_error(state, "socket:recv_from() address formatting failed");

    mobius_stack_pop(state, 2);
    mobius_stack_pushNewTable(state, 5);
    int tbl = mobius_stack_size(state) - 1;
    mobius_stack_pushBufferCopy(state, buffer.data(), (size_t)rc);
    mobius_stack_setTableField(state, tbl, "payload");
    set_string_field(state, tbl, "host", host);
    set_int_field(state, tbl, "port", strtoll(service, nullptr, 10));
    if (storage.ss_family == AF_INET) set_string_field(state, tbl, "family", "ipv4");
    else if (storage.ss_family == AF_INET6) set_string_field(state, tbl, "family", "ipv6");
    else set_string_field(state, tbl, "family", "unknown");
    if (rc >= 0 && rc > 0) {
        bool utf8 = true;
        for (ssize_t i = 0; i < rc; i++) {
            if (buffer[(size_t)i] == 0) {
                utf8 = false;
                break;
            }
        }
        if (utf8) {
            mobius_stack_pushStringLength(state, (const char*)buffer.data(), (size_t)rc);
            mobius_stack_setTableField(state, tbl, "text");
        }
    } else {
        mobius_stack_pushString(state, "");
        mobius_stack_setTableField(state, tbl, "text");
    }
    return 1;
}

static void copy_module_function(MobiusState* state, int module_idx,
                                 const char* module_key, int target_idx,
                                 const char* target_key) {
    mobius_stack_getTableField(state, module_idx, module_key);
    mobius_stack_setTableField(state, target_idx, target_key);
}

static int init_socket_plugin(MobiusState* state) {
    if (!socket_platform_init()) return mobius_error(state, "socket module platform init failed");
    return 0;
}

static int socket_post_init(MobiusState* state) {
    const int module_idx = 0;

    mobius_stack_pushNewTable(state, 8);
    const int socket_proto_idx = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__close", socket_proto_idx, "close");
    copy_module_function(state, module_idx, "__is_closed", socket_proto_idx, "is_closed");
    copy_module_function(state, module_idx, "__set_timeout", socket_proto_idx, "set_timeout");
    copy_module_function(state, module_idx, "__local_addr", socket_proto_idx, "local_addr");
    copy_module_function(state, module_idx, "__peer_addr", socket_proto_idx, "peer_addr");
    copy_module_function(state, module_idx, "__send", socket_proto_idx, "send");
    copy_module_function(state, module_idx, "__recv", socket_proto_idx, "recv");
    copy_module_function(state, module_idx, "__shutdown", socket_proto_idx, "shutdown");
    mobius_set_userdata_type_metatable(state, TCP_SOCKET_TYPE);

    mobius_stack_pushNewTable(state, 6);
    const int listener_proto_idx = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__close", listener_proto_idx, "close");
    copy_module_function(state, module_idx, "__is_closed", listener_proto_idx, "is_closed");
    copy_module_function(state, module_idx, "__set_timeout", listener_proto_idx, "set_timeout");
    copy_module_function(state, module_idx, "__local_addr", listener_proto_idx, "local_addr");
    copy_module_function(state, module_idx, "__accept", listener_proto_idx, "accept");
    mobius_set_userdata_type_metatable(state, TCP_LISTENER_TYPE);

    mobius_stack_pushNewTable(state, 9);
    const int udp_proto_idx = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__close", udp_proto_idx, "close");
    copy_module_function(state, module_idx, "__is_closed", udp_proto_idx, "is_closed");
    copy_module_function(state, module_idx, "__set_timeout", udp_proto_idx, "set_timeout");
    copy_module_function(state, module_idx, "__local_addr", udp_proto_idx, "local_addr");
    copy_module_function(state, module_idx, "__peer_addr", udp_proto_idx, "peer_addr");
    copy_module_function(state, module_idx, "__udp_connect", udp_proto_idx, "connect");
    copy_module_function(state, module_idx, "__udp_send", udp_proto_idx, "send");
    copy_module_function(state, module_idx, "__udp_recv", udp_proto_idx, "recv");
    copy_module_function(state, module_idx, "__udp_send_to", udp_proto_idx, "send_to");
    copy_module_function(state, module_idx, "__udp_recv_from", udp_proto_idx, "recv_from");
    mobius_set_userdata_type_metatable(state, UDP_SOCKET_TYPE);

    const char* hidden_keys[] = {
        "__close", "__is_closed", "__set_timeout", "__local_addr",
        "__peer_addr", "__send", "__recv", "__shutdown", "__accept",
        "__udp_connect", "__udp_send", "__udp_recv", "__udp_send_to", "__udp_recv_from"
    };
    for (const char* key : hidden_keys) {
        mobius_stack_pushNil(state);
        mobius_stack_setTableField(state, module_idx, key);
    }
    return 0;
}

static void cleanup_socket_plugin(void) {
    socket_platform_cleanup();
}

} // namespace

static MobiusPluginFunction socket_functions[] = {
    {"connect", socket_connect, 2, MOBIUS_VAL_USERDATA, "Connect a TCP socket to host:port"},
    {"listen",  socket_listen,  SIZE_MAX, MOBIUS_VAL_USERDATA, "Bind and listen on a TCP address"},
    {"udp",     socket_udp,     SIZE_MAX, MOBIUS_VAL_USERDATA, "Create a UDP socket, optionally bound to host:port"},
    {"__close", socket_base_close, 1, MOBIUS_VAL_BOOL, "Internal socket close method"},
    {"__is_closed", socket_base_is_closed, 1, MOBIUS_VAL_BOOL, "Internal socket state method"},
    {"__set_timeout", socket_base_set_timeout, 2, MOBIUS_VAL_USERDATA, "Internal socket timeout method"},
    {"__local_addr", socket_base_local_addr, 1, MOBIUS_VAL_TABLE, "Internal socket address method"},
    {"__peer_addr", socket_base_peer_addr, 1, MOBIUS_VAL_TABLE, "Internal peer address method"},
    {"__send", tcp_socket_send, 2, MOBIUS_VAL_INT64, "Internal socket send method"},
    {"__recv", tcp_socket_recv, 2, MOBIUS_VAL_UNKNOWN, "Internal socket recv method"},
    {"__shutdown", tcp_socket_shutdown, SIZE_MAX, MOBIUS_VAL_USERDATA, "Internal socket shutdown method"},
    {"__accept", tcp_listener_accept, 1, MOBIUS_VAL_USERDATA, "Internal listener accept method"},
    {"__udp_connect", udp_socket_connect, 3, MOBIUS_VAL_USERDATA, "Internal udp connect method"},
    {"__udp_send", udp_socket_send, 2, MOBIUS_VAL_INT64, "Internal udp send method"},
    {"__udp_recv", udp_socket_recv, 2, MOBIUS_VAL_BUFFER, "Internal udp recv method"},
    {"__udp_send_to", udp_socket_send_to, 4, MOBIUS_VAL_INT64, "Internal udp send_to method"},
    {"__udp_recv_from", udp_socket_recv_from, 2, MOBIUS_VAL_TABLE, "Internal udp recv_from method"},
};

static MobiusPlugin socket_plugin = {
    .metadata = {
        .name = "socket",
        .version = "1.0.0",
        .description = "Plain TCP and UDP socket helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = socket_functions,
    .function_count = sizeof(socket_functions) / sizeof(socket_functions[0]),
    .init_plugin = init_socket_plugin,
    .cleanup_plugin = cleanup_socket_plugin,
    .post_init = socket_post_init,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &socket_plugin;
}
