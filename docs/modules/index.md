# Module Reference

Modules add functionality beyond the language core. Import one by name:

```mobius
import "module_name"
```

See [Modules and Packages](../guide/modules-and-packages.md) for how `import`
resolves modules and how packages are installed.

[← Documentation home](../index.md)

---

## Built-in modules

| Module | Summary |
|--------|---------|
| [`compression`](compression.md) | Archive and stream compression (`zip`, `tar`, `gzip`, `zstd`) with a high-level API |
| [`crypto`](crypto.md)           | Dependency-free hashing, HMAC, encoding, checksums, UUID, and secure random |
| [`datetime`](datetime.md)       | Structured date/time tables, formatting, and ISO-8601 parsing |
| [`fiber`](fiber.md)             | Channels, futures helpers, sleep, cancellation, and array slices — a builtin global, **no import** |
| [`http`](http.md)               | HTTP request/response parsing and building, plus a plain-TCP client |
| [`json`](json.md)               | JSON parsing and serialization |
| [`math`](math.md)               | Trigonometry, logarithms, and number-theory functions and constants |
| [`os`](os.md)                   | Filesystem, paths, environment, processes, and time conversion |
| [`regex`](regex.md)             | Regular-expression match, search, find-all, replace, and split |
| [`socket`](socket.md)           | Plain TCP and UDP sockets with buffer-first I/O |
| [`toml`](toml.md)               | TOML parsing and serialization |
| [`url`](url.md)                 | URL parsing, building, percent-encoding, and query strings |
| [`web`](web.md)                 | Higher-level HTTP routing, middleware, and websocket server framework |
| [`websocket`](websocket.md)     | WebSocket handshakes, frame coding, and a plain-TCP client |
| [`yaml`](yaml.md)               | Dependency-free YAML subset parser and serializer |

## Packages

| Package | Summary |
|---------|---------|
| [`sqlite`](sqlite.md)     | SQLite bindings: databases, prepared statements, transactions |
| [Other packages](packages.md) | Experimental native packages: `glfw`, `monstro`, `vulkease` |

---

## Notes

- Core globals like `abs`, `sqrt`, `pow`, `min`, `max`, and the string and file
  helpers are available **without** `import` — see the
  [Standard Library](../reference/standard-library.md).
- `fiber` is a **builtin global table**, not an imported module — it needs no
  `import`. The concurrency language features (`spawn`, `await`, `yield`,
  `shared`, `atomic`) are part of the runtime too. Both are documented in
  [Concurrency](../guide/concurrency.md).
- The `http`, `socket`, and `websocket` modules are **plain transport only** for
  now — TLS (`https://`, `wss://`) is not yet included.
