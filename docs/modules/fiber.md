# `fiber` Builtin

[← Module reference](index.md)

`fiber` is a **builtin global table** — it is always available, with no
`import` required. It provides the constructors and helpers around concurrent
execution: channels, structured-concurrency combinators, sleeping,
and cancellation. (Aliasing array views are created with the `arr:span(start,
end)` method — see [Concurrency](../guide/concurrency.md#array-spans).)

The concurrency language features themselves — `spawn`, `await`, `yield`,
`shared`, and `atomic(...)` — and channel methods are also always available.
They are documented in [Concurrency](../guide/concurrency.md).

Functions:

| Function | Description |
|---|---|
| `fiber.channel(capacity?)` | Create a channel, optionally bounded. |
| `fiber.all(futures)` | Wait for all futures in an array. |
| `fiber.any(futures)` | Wait for the first completed future in an array. |
| `fiber.sleep(milliseconds)` | Suspend the current fiber for a duration. |
| `fiber.cancel(future)` | Cancel a future. |

Channel methods:

- `ch:send(value)`
- `ch:recv()`
- `ch:try_send(value)`
- `ch:try_recv()`
- `ch:close()`
- `ch:is_closed()`

Example:

```mobius
var ch = fiber.channel(4)

// Pass the channel as an argument so the fiber shares it by reference.
func producer(ch) {
    ch:send("ready")
    ch:close()
}
spawn producer(ch)

print(ch:recv())    // "ready"
```
