# Mobius

**A small, fast scripting language with C-style syntax — built to be embedded, built for concurrency.**

Mobius keeps the lightweight feel of Lua — tables, `:` method calls, a tiny
runtime you can drop into any C/C++ application — and pairs it with the syntax
you already know from C, plus the things you always end up wanting: real
concurrency, pattern matching, enums, and a standard library that covers JSON,
HTTP, SQLite, and more out of the box.

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

Concurrency is part of the language, not a library bolted on. Spawn work onto
fibers, await the results, and let the runtime schedule them across worker
threads:

```mobius
func fetch_size(name: string): int64 {
    // stand-in for real work — each call runs on its own fiber
    return size(name) * 100
}

var jobs = []
for (var i = 0; i < 4; i++) {
    jobs:push(spawn fetch_size("file_" + str(i)))
}

var total = 0
for (var i = 0; i < 4; i++) {
    total += await jobs[i]
}
print("processed", total, "bytes")
```

## Why Mobius?

- **Familiar on day one.** C-style braces and operators, Lua-style tables and
  methods. If you've written C, JavaScript, or Lua, you can read Mobius already.
- **Fast by default, faster when you ask.** Mobius runs the standard benchmark
  suite [faster than Lua 5.4](#performance) and several times faster than
  CPython. Type annotations on hot functions are optional — but when you add
  them, the compiler uses them.
- **Concurrency that scales.** `spawn` / `await` fibers, channels, and `shared`
  data run across real worker threads. Data crossing between fibers is copied
  unless you explicitly share it — no accidental data races.
- **Made to be embedded.** A small, Lua-style C API: create a state, register
  functions, exchange values, expose your own types. One shared library, no
  exotic dependencies.
- **Batteries included.** JSON, YAML, TOML, HTTP (client and a `web` server
  framework), WebSockets, sockets, SQLite, regex, crypto, compression,
  datetime, math, OS, and binary buffers with zero-copy struct views.
- **Memory management you don't think about.** Automatic and low-pause:
  most garbage is reclaimed the instant it's unreachable, and a cycle
  collector quietly handles the rest.

## Quick start

Build with the bundled `buildy` tool, then run a script or open the REPL:

```bash
./buildy -r                 # build (release)
bin/mobius script.mob       # run a file
bin/mobius                  # interactive REPL
```

Your first program:

```mobius
// hello.mob
var name = "World"

func greet(who) {
    print("Hello,", who)
}

greet(name)
```

New to the language? The [Language Tour](docs/guide/language-tour.md) covers
all of it in one page.

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

From there you can register C functions, expose your own userdata types with
methods, and call Mobius functions back from C — see the
[Embedding Guide](docs/embedding/embedding-guide.md).

## What people build with it

- **A scripting layer** inside game engines, editors, and tools — the classic
  Lua role, with C-style syntax your users already know.
- **Standalone scripts and CLI tools** that need real libraries without a
  package-manager scavenger hunt.
- **Web services** with the bundled `web` framework, JSON, and SQLite.
- **Parallel data work** — fan out across fibers and worker threads without
  leaving the language.
- **Distributable packages** that bundle Mobius code and native C/C++ plugins
  together.

## Performance

Faster than Lua 5.4 on the overall benchmark suite, and ~3.7× faster than
CPython — measured with checksum-verified, apples-to-apples benchmarks
(medians of five runs, milliseconds, lower is better):

| Benchmark | Mobius | Lua | CPython | Mobius/Lua |
|-----------|-------:|----:|--------:|-----------:|
| Arithmetic (integer)           |  106.4 |  130.3 |   855.7 | **0.82×** |
| Recursive calls (fib 30)       |   39.1 |   32.8 |    71.6 | 1.19× |
| Array ops (dense numeric)      |   11.3 |   10.5 |    56.0 | 1.08× |
| Table ops (string-key map)     |   16.3 |   12.1 |    29.0 | 1.35× |
| String ops                     |   46.5 |   68.2 |    27.4 | **0.68×** |
| Nested loops                   |   51.1 |   52.8 |   284.7 | **0.97×** |
| Object create / destroy        |   63.3 |   78.6 |    54.8 | **0.81×** |
| Mixed workload                 |   41.1 |   41.0 |    30.8 | 1.00× |
| **Total**                      | **383.4** | **450.2** | **1421.7** | **0.85×** |

Two things worth knowing:

- You get this speed with plain, untyped code. Under the hood, a
  register-based bytecode VM infers and locks variable types so it can run
  type-specialized instructions.
- When a function really matters, annotate it —
  `func fib(n: int64): int64` — and the compiler drops another layer of
  runtime checks. That one change took the recursion benchmark from 1.8× to
  1.19× vs Lua.

Every benchmark emits a checksum, and the runner refuses to report timings
unless all three languages agree — so the implementations are provably doing
the same work. Reproduce with
`python3 benchmarks/run_benchmarks.py --runs 5`. As always, these are
indicative micro-benchmarks, not a rigorous cross-language study.

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

The [`examples/`](examples/) directory has embedding examples (C and C++),
plugin and userdata examples, networking servers, and pure-Mobius demo
scripts. Run the test suite with `./test_simple.sh`.

## Status

Mobius is version **0.1.0**. The language, VM, standard library, fiber
runtime, embedding API, and bundled modules are usable today. The package
system and the `http` / `socket` / `websocket` modules are plain-transport
only for now — TLS is not yet included.

## License

Mobius is released under the [MIT License](LICENSE).
