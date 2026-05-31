# Concurrency

Mobius has built-in fiber-based concurrency. The keywords `spawn`, `await`,
`yield`, `shared`, and `atomic` are part of the language; channels and a few
helpers live on the `fiber` builtin — a global table that is always available
with **no `import`**.

Each interpreter (`MobiusState`) owns a pool of fibers and a set of worker
threads, so fibers can run truly in parallel. Reference counts are atomic and
shared containers use locks, making safe parallelism the default when you use
`shared` and `atomic`.

[← Documentation home](../index.md) · [Guide: Binary Data](binary-data.md)

---

## Spawn and await

`spawn` launches a function call on a separate fiber and immediately returns a
**future**. `await` blocks until the future resolves and yields its value.

```mobius
func compute(n) {
    var sum = 0
    for (var i = 0; i < n; i += 1) { sum += i }
    return sum
}

var f = spawn compute(1000)
// ... do other work ...
var result = await f      // blocks until compute() finishes
print(result)             // 499500
```

Many fibers can run at once:

```mobius
func double(x) { return x * 2 }

var a = spawn double(10)
var b = spawn double(20)
var c = spawn double(30)
print(await a, await b, await c)   // 20 40 60
```

Futures are single-assignment: once resolved (or rejected), they never change,
and awaiting an already-resolved future returns the cached value immediately. If
the spawned function throws, the exception is re-raised at the `await`.

### Value semantics across spawn

Both **arguments** and a spawned closure's **captured upvalues** follow the same
Mobius value semantics as they cross the fiber boundary:

- Scalars are copied by value.
- Non-shared arrays and tables are **deep-copied** — each spawn gets its own
  independent copy.
- `shared` values are passed by reference to the same synchronized cell.
- Array spans (`arr:span(...)`) stay aliased to their parent array, even across
  `spawn`.

So a closure that captures variables can be spawned, and capture follows the
same rules as argument passing: non-shared captured data is **snapshotted**
(each spawn gets its own copy), while a captured `shared` variable keeps its
identity and is **shared by reference**. Spawning the same closure many times
therefore gives each fiber its own isolated copy of captured non-shared data:

```mobius
func make_worker(rows) {           // captures `rows` (non-shared → copied)
    func run() { return rows:size() }
    return run
}
var futures = []
for (var i = 0; i < 8; i++) { futures:push(spawn make_worker(load_chunk(i))()) }
```

To **share** mutable state across fibers, use a `shared` variable — whether you
capture it or pass it as an argument, all fibers see the same synchronized cell:

```mobius
shared var totals = [0, 0, 0]

// captured:
func make_adder(t) {
    func add(i, v) { atomic(t[i] = t[i] + v) }
    return add
}
var add = make_adder(totals)
spawn add(0, 10)                   // totals shared by reference (captured)

// or passed as an argument:
func add_arg(t, i, v) { atomic(t[i] = t[i] + v) }
spawn add_arg(totals, 1, 5)        // also shared by reference
```

### Restrictions

- Native (C) functions cannot be spawned.

## yield

`yield` cooperatively yields the current fiber so others can make progress:

```mobius
for (var i = 0; i < 1000; i += 1) {
    // do a chunk of work
    yield
}
```

---

## Shared variables

By default, values are not synchronized across fibers. Prefix a `var`
declaration with `shared` to store the binding in a synchronized cell:

```mobius
shared var counter = 0
shared var data = [0, 0, 0, 0]
shared var config = { debug: false }
```

Reads and writes *through* the shared variable are synchronized — for scalars,
arrays, and tables. Sharing attaches to the **binding**, not recursively to
every nested mutable value reachable through it; to share a nested value
independently, give it its own `shared` binding.

Individual reads and writes are safe. Shared scalar `++`, `--`, `+=`, and `-=`
are atomic. Compound updates to array/table *elements* still need `atomic()`.

## Atomic operations

`atomic(expr)` runs a compound read-modify-write expression on a shared value
atomically — the shared cell's lock is held across the whole expression,
preventing lost updates:

```mobius
shared var counter = [0]

func increment(counter, n) {
    for (var i = 0; i < n; i += 1) {
        atomic(counter[0] = counter[0] + 1)
    }
    return n
}

shared var stats = { hits: 0 }
atomic(stats["hits"] = stats["hits"] + 1)

shared var total = 0
atomic(total = total + 1)
```

Without `atomic()`, `counter[0] = counter[0] + 1` compiles to separate read and
write steps; two fibers could read the same value, both increment, and lose one
update.

Rules:

- The expression must operate on a shared variable or a shared array/table
  element. Using `atomic()` on a non-shared target is a **runtime error**.
- The shared cell uses a recursive mutex, so nested `atomic()` calls on the same
  target do not deadlock.

---

## Channels

Channels provide bounded message passing between fibers. They are created from
the `fiber` builtin and use `:` method syntax:

```mobius
var ch = fiber.channel(10)    // bounded channel, capacity 10

func producer(ch) {
    ch:send(42)
    ch:close()
}
spawn producer(ch)

var msg = ch:recv()
print(msg)    // 42
```

| Method               | Description                                                          |
|----------------------|----------------------------------------------------------------------|
| `ch:send(value)`     | Send; blocks if full. Returns `false` if the channel is closed.      |
| `ch:recv()`          | Receive; blocks if empty. Raises `ChannelClosedError` if closed & empty. |
| `ch:try_send(value)` | Non-blocking send; `true` if enqueued, `false` if full/closed.       |
| `ch:try_recv()`      | Non-blocking receive; the value, or `nil` if empty.                  |
| `ch:close()`         | Close the channel; pending receivers unblock, buffered values drain. |
| `ch:is_closed()`     | `true` if the channel has been closed.                               |

---

## The fiber builtin

`fiber` is a global table — no `import` required.

| Function                          | Description                                                  |
|-----------------------------------|--------------------------------------------------------------|
| `fiber.channel([capacity])`       | Create a channel, optionally bounded.                        |
| `fiber.all(futures)`              | Wait for all futures; return results in order.               |
| `fiber.any(futures)`              | Return the result of the first future to resolve.            |
| `fiber.sleep(milliseconds)`       | Suspend the current fiber for at least the given time.       |
| `fiber.cancel(future)`            | Request cancellation of the fiber behind a future.           |

### Structured concurrency

```mobius
func work(id) { return id * 10 }

var futures = [spawn work(1), spawn work(2), spawn work(3)]
var results = fiber.all(futures)    // [10, 20, 30]
```

`fiber.all` propagates the first error if any future rejects; `fiber.any`
returns the first successful result.

### Cancellation

```mobius
var f = spawn long_task()
fiber.cancel(f)
```

A cancelled fiber throws a `CancellationError` at its next cancellation check
point (loop back-edges and yield points).

### Array spans

`arr:span(start, end)` creates a lightweight **view** into an existing array
over the half-open range `[start, end)`. Reads and writes through the span affect
the underlying array, which makes spans ideal for dividing data among fibers.
(This is the aliasing counterpart to [`arr:slice`](collections.md#array-methods),
which returns an independent copy.)

```mobius
shared var data = [1, 2, 3, 4, 5, 6, 7, 8]
var left  = data:span(0, 4)   // view of [1,2,3,4]
var right = data:span(4, 8)   // view of [5,6,7,8]

left[0] = 99
print(data[0])    // 99 — write-through
```

Spans are always views, even from a non-shared array. While any span is alive,
structural mutations that resize the parent (`push`/`pop`) fail at runtime. If
the parent array is `shared`, span access synchronizes through it — so a span
can be safely handed to a `spawn`ed fiber.

---

## Tuning the runtime

When you embed Mobius, the fiber pool and worker-thread count are set through
`MobiusConfig` (`fiber_stack_size`, `main_fiber_stack_size`,
`initial_fiber_pool_size`, `max_fiber_pool_size`, `max_worker_threads`). The
top-level script runs on a dedicated main fiber with a larger stack
(`main_fiber_stack_size`) than pooled worker fibers (`fiber_stack_size`), so
deep native calls the script makes have room. See the
[Embedding Guide](../embedding/embedding-guide.md#concurrency-and-fibers).

---

Next: [Modules and Packages](modules-and-packages.md).
