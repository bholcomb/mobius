# `websocket` Module

Import:

```mobius
import "websocket"
```

The `websocket` module provides dependency-free WebSocket protocol helpers for
handshakes and frame encoding/decoding. It currently focuses on protocol data,
not socket transport.

Functions:

| Function | Description |
|---|---|
| `websocket.accept_key(client_key)` | Compute the `Sec-WebSocket-Accept` value. |
| `websocket.is_upgrade_request(request)` | Check whether an HTTP request table is a websocket upgrade request. |
| `websocket.handshake_request(spec)` | Build a client handshake request and return `{request, key, path}`. |
| `websocket.handshake_response(request[, protocol])` | Build a server handshake response table. |
| `websocket.build_frame(spec)` | Build a WebSocket frame and return hex-encoded bytes. |
| `websocket.parse_frame(frame_hex)` | Parse a hex-encoded WebSocket frame into a table. |

Notes:

- `handshake_request(...)` and `handshake_response(...)` are designed to pair
  naturally with the `http` module.
- `build_frame(...)` returns hex because Mobius strings are currently text-first
  and not a general binary buffer type.
- `parse_frame(...)` always returns `payload_hex`, and returns `payload` when the
  decoded payload can be represented safely as a string.

Example:

```mobius
import "http"
import "websocket"

var req = websocket.handshake_request({
    host: "example.com",
    path: "/chat"
})

var parsed = http.parse_request(req.request)
print(websocket.is_upgrade_request(parsed))
```
