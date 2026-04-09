# `fiber` Module

Import:

```mobius
import "fiber"
```

The `fiber` module provides helpers around concurrent execution primitives.
Language features such as `spawn`, `await`, `yield`, `shared`, and `atomic(...)`
are documented in the language reference.

Functions:

| Function | Description |
|---|---|
| `fiber.channel(capacity?)` | Create a channel, optionally bounded. |
| `fiber.all(futures)` | Wait for all futures in an array. |
| `fiber.any(futures)` | Wait for the first completed future in an array. |
| `fiber.sleep(milliseconds)` | Suspend the current fiber for a duration. |
| `fiber.cancel(future)` | Cancel a future. |
| `fiber.slice(array, start, length)` | Create an aliasing slice view into an array. |

Channel methods:

- `ch:send(value)`
- `ch:recv()`
- `ch:try_send(value)`
- `ch:try_recv()`
- `ch:close()`
- `ch:is_closed()`

Example:

```mobius
import "fiber"

var ch = fiber.channel(4)

spawn(func() {
    ch:send("ready")
})

print(ch:recv())
```
