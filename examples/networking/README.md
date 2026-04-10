# Networking Examples

This directory contains script-only examples built on top of the `socket`,
`http`, `websocket`, `json`, `url`, and `crypto` modules.

Files:

- `http_server_utils.mob` - shared helpers for reading requests and writing responses
- `http_hello_server.mob` - minimal dynamic HTML server
- `http_rest_site.mob` - a larger example that serves both HTML pages and a JSON REST API
- `websocket_echo_server.mob` - plain `ws://` echo server built on `socket` + `http` + `websocket`

Run them with:

```bash
./bin/mobius examples/networking/http_hello_server.mob
./bin/mobius examples/networking/http_rest_site.mob
./bin/mobius examples/networking/websocket_echo_server.mob
```

Suggested requests:

```bash
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8081/api/health
curl http://127.0.0.1:8081/api/todos
curl -X POST http://127.0.0.1:8081/api/todos -H "content-type: application/json" -d '{"title":"Ship examples"}'
curl -X POST http://127.0.0.1:8081/api/todos/2/toggle
```

Notes:

- These are teaching examples, not production servers.
- The HTTP helper reads a full request and handles `Content-Length`, which is
  enough for common demos and forms.
- The WebSocket example keeps the server intentionally simple and expects small,
  non-fragmented frames.
