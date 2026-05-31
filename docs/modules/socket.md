# `socket` Module

Import:

```mobius
import "socket"
```

[← Module reference](index.md)

The `socket` module provides the plain transport layer for Mobius. It is
intentionally small and blocking: the goal is to establish the base API for
`http` and `websocket` transports before adding TLS.

Current scope:

- TCP client connections with `socket.connect(host, port)`
- TCP listeners with `socket.listen(host, port [, backlog])`
- UDP sockets with `socket.udp()` or `socket.udp(host, port)`
- Binary I/O through `buffer`
- String convenience for `send`
- Per-userdata prototype objects for `TcpSocket`, `TcpListener`, and `UdpSocket`

Functions:

- `socket.connect(host, port)` -> `TcpSocket`
- `socket.listen(host, port [, backlog])` -> `TcpListener`
- `socket.udp()` -> unbound `UdpSocket`
- `socket.udp(host, port)` -> bound `UdpSocket`

`TcpSocket` methods:

- `sock:send(data)` sends a `buffer` or `string` and returns bytes written
- `sock:recv(max_bytes)` returns a `buffer`, or `nil` on clean EOF
- `sock:shutdown([read [, write]])` shuts down one or both directions
- `sock:set_timeout(milliseconds)` applies send/recv timeouts
- `sock:local_addr()` returns `{ host, port, family }`
- `sock:peer_addr()` returns `{ host, port, family }`
- `sock:close()` closes the socket
- `sock:is_closed()` reports closure state

`TcpListener` methods:

- `listener:accept()` waits for a connection and returns a `TcpSocket`
- `listener:set_timeout(milliseconds)` applies accept timeouts
- `listener:local_addr()` returns `{ host, port, family }`
- `listener:close()` closes the listener
- `listener:is_closed()` reports closure state

`UdpSocket` methods:

- `sock:connect(host, port)` sets a default peer and returns `self`
- `sock:send(data)` sends a datagram to the connected peer
- `sock:recv(max_bytes)` receives a datagram from the connected peer as a `buffer`
- `sock:send_to(host, port, data)` sends a datagram without connecting first
- `sock:recv_from(max_bytes)` returns `{ payload, host, port, family, text? }`
- `sock:set_timeout(milliseconds)` applies send/recv timeouts
- `sock:local_addr()` returns `{ host, port, family }`
- `sock:peer_addr()` returns `{ host, port, family }` after `connect(...)`
- `sock:close()` closes the socket
- `sock:is_closed()` reports closure state

Notes:

- This module is plain TCP only. HTTPS and secure WebSocket support will layer on
  later with TLS.
- Calls are blocking. They work well with Mobius fibers when other worker
  threads are available, but this is not yet a nonblocking event-loop API.
- `recv()` is buffer-first by design so higher-level protocols can decide how to
  decode the payload.
- UDP `recv_from()` is also buffer-first and includes a `text` helper for valid
  UTF-8 payloads.

Example:

```mobius
import "socket"

var listener = socket.listen("127.0.0.1", 0)
var port = listener:local_addr().port

var task = spawn func(p) {
    var client = socket.connect("127.0.0.1", p)
    client:send("ping")
    var reply = client:recv(16)
    client:close()
    return reply:to_string()
}(port)

var server = listener:accept()
var incoming = server:recv(16)
server:send(buffer_from_string("pong"))
server:close()
listener:close()

print(incoming:to_string())
print(await task)
```
