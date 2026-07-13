# Mobius

**Mobius is a small, fast, Lua-inspired scripting language** — equally at home
embedded inside a C/C++ application or running standalone scripts. It keeps
Lua's lightweight feel and familiar table / `:` method model, wraps it in
C-style syntax, and adds modern conveniences: inferred type locking,
fiber-based concurrency, a pattern-matching `switch`, enums, and a
batteries-included standard library.

```mobius
enum Suit { CLUBS, DIAMONDS, HEARTS, SPADES }

func describe(rank: int64): string {
    switch (rank) {
        case 1:                       return "Ace"
        case 11..13:                  return "Face card"
        case is int64 when rank < 1:  return "invalid"
        default:                      return str(rank)
    }
}

print(describe(12))    // "Face card"
```

## What you can build with it

- **Embed a scripting layer** in a C/C++ application — game engines, editors,
  tools, plugin systems — through a small, Lua-style C stack API.
- **Write standalone scripts, CLI tools, and web servers** using bundled
  modules for files, the OS, JSON/YAML/TOML, sockets, HTTP, a `web` server framework,
  WebSockets, SQLite, and more.
- **Run concurrent and parallel workloads** with fibers, channels, and
  `shared` / `atomic` data, scheduled across worker threads.
- **Extend and distribute** with native plugins and packages — bundle Mobius
  and C/C++ code into installable packages to share across projects.

## Highlights

- **Lua-inspired, C-flavored** — Lua's tables, `:` method calls, and lightweight
  runtime, with C-style braces and operators. Source files use `.mob`.
- **Pattern-matching `switch`** — match values, ranges (`1..10`), comparisons
  (`>= 100`), runtime types (`is int64`), and enum members, with optional
  `when` guards.
- **Type locking** — a variable's type is inferred from its first non-nil value
  and then fixed, so the bytecode VM emits type-specialized opcodes and skips
  runtime type checks — no type syntax required in your code.
- **Real concurrency** — `spawn` / `await` fibers, bounded channels, `shared`
  variables, and `atomic` updates, scheduled across worker threads.
- **Binary data, built in** — growable byte buffers and zero-copy **struct
  views** (packed or native layout) for parsing and emitting binary formats and
  for sharing memory with embedding C/C++ code.
- **Batteries included** — JSON, YAML, TOML, HTTP, WebSocket, sockets, regex,
  crypto, compression, datetime, math, and OS modules.
- **Clean embedding API** — exchange values, register native functions, expose
  userdata types with methods, and call back into scripts.

## Quick start

Build with the bundled `buildy` tool (release config):

```bash
./buildy -r
```

Run a script, or start the REPL:

```bash
bin/mobius script.mob     # run a file
bin/mobius                # interactive REPL
```

```mobius
// hello.mob
var name = "World"

func greet(who) {
    print("Hello,", who)
}

greet(name)
```

## Embedding in a few lines

```c
#include <mobius/mobius.h>

int main(void) {
    MobiusState* state = mobius_new_state(NULL);
    mobius_init_stdlib(state);
    mobius_exec_string(state, "print(\"Hello from Mobius!\")");
    mobius_free_state(state);
}
```

```bash
g++ -o app app.c -lmobius-core -ldl
```

You can register C functions, exchange values through a Lua-style stack, expose
your own userdata types, and call Mobius functions back from C. See the
[Embedding Guide](docs/embedding/embedding-guide.md).

## Documentation

Full documentation lives in [`docs/`](docs/index.md).

| Guide | Description |
|-------|-------------|
| [Getting Started](docs/guide/getting-started.md)        | Build, run, the REPL, the CLI, and your first program |
| [Language Tour](docs/guide/language-tour.md)            | The whole language in one page |
| [Language Guide](docs/index.md#guide)                   | Types, control flow, functions, collections, binary data, concurrency |
| [Standard Library](docs/reference/standard-library.md)  | Built-in functions available without `import` |
| [Module Reference](docs/modules/index.md)               | Bundled modules: `json`, `os`, `http`, `web`, `sqlite`, … |
| [Embedding Guide](docs/embedding/embedding-guide.md)    | Embed Mobius in a C/C++ application |
| [Plugin Guide](docs/embedding/plugin-guide.md)          | Write native `.so`/`.dll` modules |
| [Grammar (BNF)](docs/reference/grammar.md)              | The canonical formal grammar |

## Examples & tests

The [`examples/`](examples/) directory has embedding examples (C and C++),
plugin and userdata examples, networking servers, and pure-Mobius demo scripts
(see [examples/README.md](examples/README.md)). Run the test suite with:

```bash
./test_simple.sh
```

## Performance

Mobius compiles to a register-based bytecode VM with type-specialized opcodes.
The table below is a mixed micro-benchmark suite
([`benchmarks/benchmark_comprehensive.*`](benchmarks/)) against Lua 5.4.7 and
CPython 3, run on one machine — **median of five runs, milliseconds, lower is
better**:

| Benchmark | Mobius | Lua | CPython | Mobius/Lua |
|-----------|-------:|----:|--------:|-----------:|
| Arithmetic (integer)           |  117.1 |  133.8 |   847.8 | **0.87×** |
| Recursive calls (fib 30)       |   44.5 |   32.5 |    69.4 | 1.37× |
| Array ops (dense numeric)      |   14.7 |   10.6 |    55.5 | 1.39× |
| Table ops (string-key map)     |   18.3 |   11.7 |    28.6 | 1.57× |
| String ops                     |   47.5 |   66.4 |    27.5 | **0.72×** |
| Nested loops                   |   45.6 |   51.7 |   282.8 | **0.88×** |
| Object create / destroy        |   67.2 |   76.8 |    53.8 | **0.87×** |
| Mixed workload                 |   43.6 |   41.5 |    31.0 | 1.05× |
| **Total**                      | **413.6** | **452.8** | **1413.9** | **0.91×** |

The goal is to be as fast as reasonably possible. Current state:

- **Faster than Lua overall** (0.91×) and **~3.4× faster than CPython**.
  Mobius beats Lua outright on integer arithmetic, nested loops, string ops,
  and object create/destroy.
- Type locking pays off most on **arithmetic and tight numeric loops**;
  hot stdlib builtins (`size`, `str`, `concat`) compile to dedicated opcodes
  instead of native calls, and small strings, tables, arrays, and closures
  come from per-thread pools.
- **Annotating hot function parameters** (`func fib(n: int64): int64`) is
  the idiomatic fast path for recursion-heavy code: it removes shared-value
  identity tracking and unlocks type-specialized opcodes inside the body
  (the fib benchmark uses it). The remaining gaps are untyped **recursion**
  and **table/array micro-ops**. CPython, with its heavily tuned string and
  dict machinery, stays ahead on string-heavy code.

Memory is managed by **reference counting plus a tracing cycle collector**:
acyclic garbage is reclaimed immediately (cache-hot, deterministic), while a
mark-sweep collector runs at VM safepoints to reclaim reference cycles that
refcounting cannot see. The collector's budget adapts — it backs off
geometrically when collections find nothing (pure-churn workloads pay almost
nothing) and tightens when cycles are actually accumulating.
`MOBIUS_GC_STRESS=1` forces a collection at every safepoint (the test suite
passes under it with AddressSanitizer), and `MOBIUS_GC_THRESHOLD` overrides
the base budget.

Every benchmark reports an integer checksum, and the runner refuses to print
timings unless all three languages agree on every one — the three
implementations are guaranteed to be computing the same thing. Reproduce with:

```bash
python3 benchmarks/run_benchmarks.py --runs 5
```

These are indicative micro-benchmarks, not a rigorous cross-language benchmark.

## Status

Mobius is version **0.1.0**. The language, VM, standard library, fiber runtime,
embedding API, and bundled modules are usable today. The package system and the
`http` / `socket` / `websocket` modules are plain-transport only for now — TLS
is not yet included.

## License

Mobius is released under the [MIT License](LICENSE).
