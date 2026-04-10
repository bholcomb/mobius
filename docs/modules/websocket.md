# `websocket` Module

Import:

```mobius
import "websocket"
```

The `websocket` module provides dependency-free WebSocket protocol helpers for
handshakes and frame encoding/decoding, plus a first plain-TCP client transport
API.

Functions:

| Function | Description |
|---|---|
| `websocket.accept_key(client_key)` | Compute the `Sec-WebSocket-Accept` value. |
| `websocket.is_upgrade_request(request)` | Check whether an HTTP request table is a websocket upgrade request. |
| `websocket.handshake_request(spec)` | Build a client handshake request and return `{request, key, path}`. |
| `websocket.handshake_response(request[, protocol])` | Build a server handshake response table. |
| `websocket.build_frame(spec)` | Build a WebSocket frame and return encoded bytes as a `buffer`. |
| `websocket.parse_frame(frame)` | Parse a frame `buffer` into a table with raw and decoded fields. |
| `websocket.connect(spec)` | Open a plain WebSocket client connection and return a connection object. |

Notes:

- `handshake_request(...)` and `handshake_response(...)` are designed to pair
  naturally with the `http` module.
- `connect(...)` is plain `ws://` only for now. Secure `wss://` support will
  layer on later with TLS.
- Frame I/O is buffer-first. `build_frame(...)` returns a `buffer`, and
  `parse_frame(...)` always returns `payload` as a `buffer`.
- For text frames, `parse_frame(...)` also includes `text` when the payload is
  valid UTF-8.
- For close frames, `parse_frame(...)` includes raw `payload` plus decoded
  `close_code` and `close_reason` when present.
- JSON is expected to sit on top of this layer: use `frame.text` with
  `json.parse(...)` or encode outbound JSON with `buffer_from_string(...)`.

`build_frame(spec)` accepts:

- `type` or `opcode`
- `payload: buffer` for arbitrary bytes
- `text: string` as a convenience for text frames
- `fin`, `rsv1`, `rsv2`, `rsv3`
- `mask` and optional `masking_key: buffer` (4 bytes)
- `close_code` and `close_reason` for close frames

`parse_frame(frame)` returns:

- `fin`, `rsv1`, `rsv2`, `rsv3`
- `masked`, `opcode`, `type`
- `payload_length`, `frame_length`
- `payload: buffer`
- `masking_key: buffer` when present
- `text` for valid UTF-8 text frames
- `close_code` / `close_reason` for close frames

`websocket.connect(spec)` accepts:

- `host` and optional `port` (default `80`)
- `path`, `query`, `origin`, `protocol`
- optional `timeout_ms`

Connection methods:

- `ws:send(data)` sends a `string` as a masked text frame or a `buffer` as a
  masked binary frame
- `ws:recv()` receives and parses the next frame into the same structure as
  `parse_frame(...)`
- `ws:close([code [, reason]])` sends a close frame and closes the connection
- `ws:is_closed()`
- `ws:set_timeout(milliseconds)`
- `ws:local_addr()`
- `ws:peer_addr()`
- `ws:protocol()` returns the negotiated subprotocol or `nil`

Example:

```mobius
import "http"
import "json"
import "websocket"

var outbound = websocket.build_frame({
    text: "{\"ok\":true}"
})

var frame = websocket.parse_frame(outbound)
print(frame.text)
print(json.parse(frame.text).ok)

var ws = websocket.connect({
    host: "127.0.0.1",
    port: 8080,
    path: "/chat",
    timeout_ms: 5000
})

ws:send("{\"ok\":true}")
print(ws:recv().text)
ws:close()
```
