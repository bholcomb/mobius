# Mobius

**A lightweight scripting language for embedding — with real concurrency,
real performance, and a large standard library built in.**

Mobius is a lightweight, embeddable scripting language designed for C and 
C++ applications, with familiar C-style syntax and built-in support for fibers
and channels. Its compact bytecode VM delivers performance competitive with 
Lua 5.4 in our benchmarks, while providing a much richer standard environment
out of the box, including JSON, HTTP, SQLite, WebSockets, and more.

Mobius is equally at home embedded inside an application or running 
standalone for scripts, tools, and services.

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

Concurrency is part of the language, not a library bolted on. Spawn work as
fibers, await the results, and let the runtime schedule them across real worker
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

* **Built to embed.** Mobius has a small, straightforward C API for creating a VM,
  registering native functions, exchanging values, and exposing application-defined types. 
  The core runtime ships as a single shared library—about 1 MB on disk, a few MB resident,
  and typically starts in under 10 ms—with no unusual dependencies. Optional
  modules stay separate, so you only ship what you use.

* **Real concurrency, without accidental races.** `spawn` and `await` fibers, channels,
  and `shared` data run across actual worker threads. Values passed between
  fibers are copied by default; memory is shared only when you explicitly ask for it.

* **Fast by design.** On the standard benchmark suite, Mobius delivers performance
  competitive with Lua 5.4 and several times faster than CPython.

* **Type locking for clean, efficient code.** Variables may be explicitly typed or
  inferred from their first use, then retain that type. This keeps ordinary Mobius
  code concise and annotation-light while allowing the VM to execute type-specialized
  instructions. Explicit types are optional, but can give the compiler more
  information to optimize performance-critical code.

* **Batteries included.** JSON, YAML, TOML, HTTP clients and a `web` server framework,
  WebSockets, sockets, SQLite, regex, cryptography, compression, datetime, math,
  OS integration, and binary buffers with zero-copy struct views are available out of the box.

* **Automatic, low-pause memory management.** Most objects are reclaimed immediately
  when they become unreachable, while a cycle collector handles the uncommon cases
  that reference counting alone cannot resolve.

Mobius is also deliberately familiar: C-style syntax and operators, 
flexible table-like data structures, and lightweight method syntax. 
If you've worked in C, JavaScript, Lua, or similar languages, Mobius should 
be readable from the start.

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

- **A scripting layer** inside game engines, editors, and tools, with C-style syntax your users already know.
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
only for now — TLS is not yet supported.

## License

Mobius is released under the [MIT License](LICENSE).
