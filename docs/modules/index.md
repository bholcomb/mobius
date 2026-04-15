# Module Reference

Mobius ships a small set of namespaced modules that are imported with:

```mobius
import "module_name"
```

Current built-in first-party modules:

- [`compression`](compression.md) - archive/compression helpers with a high-level Mobius API
- [`crypto`](crypto.md) - dependency-free hashing, encoding, UUID, and random helpers
- [`datetime`](datetime.md) - structured date/time conversion and ISO helpers
- [`fiber`](fiber.md) - concurrency helpers, channels, and slices
- [`http`](http.md) - HTTP request, response, and header parsing/building helpers
- [`json`](json.md) - JSON parsing and serialization
- [`math`](math.md) - extended mathematical functions and constants
- [`os`](os.md) - filesystem, path, process, and host time utilities
- [`regex`](regex.md) - regular expression matching and replacement
- [`socket`](socket.md) - plain TCP client/listener APIs with buffer-first I/O
- [`toml`](toml.md) - TOML parsing and serialization
- [`url`](url.md) - URL parsing, encoding, and query helpers
- [`web`](web.md) - high-level HTTP routing, middleware, and websocket helpers
- [`websocket`](websocket.md) - websocket handshake and frame protocol helpers
- [`yaml`](yaml.md) - dependency-free YAML subset parser and serializer

Notes:

- Core globals like `abs`, `sqrt`, `pow`, `min`, and `max` are documented in [`../stdlib_reference.md`](../stdlib_reference.md) because they are available without `import`.
- `fiber` is a module, but language features such as `spawn`, `await`, `yield`, `shared`, and `atomic(...)` are part of the language/runtime and are documented in [`../language_reference.md`](../language_reference.md).
