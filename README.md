# Mobius

**A lightweight scripting language for embedding — with real concurrency,
real performance, and a large standard library built in.**

Mobius fills the same role as Lua — a small runtime you drop into a C/C++
application — with C-style syntax, fibers and channels for concurrency, a VM
that benchmarks faster than Lua 5.4, and batteries included: JSON, HTTP,
SQLite, WebSockets, and more without leaving the box. It works just as well
standalone for scripts and services.

```mobius
enum Suit { CLUBS, DIAMONDS, HEARTS, SPADES }

func describe(rank) {
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
func fetch_size(name) {
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

- **Made to be embedded.** A small, Lua-style C API: create a state, register
  functions, exchange values, expose your own types. One shared library —
  ~1 MB on disk, a few MB resident (tunable), sub-10ms startup — with no
  exotic dependencies. Modules ship separately, only if you use them.
- **Concurrency that scales.** `spawn` / `await` fibers, channels, and `shared`
  data run across real worker threads. Data crossing between fibers is copied
  unless you explicitly share it — no accidental data races.
- **Fast.** Mobius runs the standard benchmark suite
  [faster than Lua 5.4](#performance) and several times faster than CPython.
- **Type locking: readable code that performs.** A variable's type is inferred
  from its first value and then locked, so your code stays clean and
  annotation-free while the VM runs type-specialized instructions. Optional
  type annotations can help the compiler generate optimal bytecode where it
  matters.
- **Batteries included.** JSON, YAML, TOML, HTTP (client and a `web` server
  framework), WebSockets, sockets, SQLite, regex, crypto, compression,
  datetime, math, OS, and binary buffers with zero-copy struct views.
- **Memory management you don't think about.** Automatic and low-pause:
  most garbage is reclaimed the instant it's unreachable, and a cycle
  collector quietly handles the rest.

Familiar on day one, too: C-style braces and operators, Lua-style tables and
methods. If you've written C, JavaScript, or Lua, you can already read Mobius.

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

Measured against both Lua 5.4 and CPython 3 with checksum-verified,
apples-to-apples benchmarks (medians of five runs, milliseconds, lower is
better; the ratio columns are Mobius ÷ that language — **below 1.0 means
Mobius is faster**):

| Benchmark | Mobius | Lua | CPython | vs Lua | vs Python |
|-----------|-------:|----:|--------:|-------:|----------:|
| Arithmetic (integer)           |  106.2 |  130.6 |   850.9 | **0.81×** | **0.12×** |
| Recursive calls (fib 30)       |   40.2 |   32.7 |    69.8 | 1.23× | **0.58×** |
| Array ops (dense numeric)      |   11.3 |   10.9 |    55.5 | 1.03× | **0.20×** |
| Table ops (string-key map)     |   14.5 |   11.9 |    28.1 | 1.22× | **0.52×** |
| String ops                     |   45.9 |   64.0 |    27.3 | **0.72×** | 1.69× |
| Nested loops                   |   43.8 |   52.1 |   285.9 | **0.84×** | **0.15×** |
| Object create / destroy        |   61.4 |   78.4 |    54.0 | **0.78×** | 1.14× |
| Mixed workload                 |   35.7 |   39.7 |    31.1 | **0.90×** | 1.15× |
| **Total**                      | **373.9** | **443.3** | **1424.8** | **0.84×** | **0.26×** |

How to read it: the two comparisons measure different things. Lua is the
benchmark for interpreter speed — beating it means the VM itself is fast.
CPython is slow per-instruction but its dictionaries and strings are
heavyweight, hand-tuned C — so it stays competitive exactly where a workload
reduces to those primitives (string-building, object churn), while pure
language execution (arithmetic, loops, calls) runs 2–8× faster in Mobius. As 
always, these are indicative micro-benchmarks, not a rigorous cross-language study.

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
