# Mobius

**A lightweight scripting language for embedding — with real concurrency,
real performance, and a large standard library built in.**

Mobius gives C and C++ applications a scripting language with familiar C-style
syntax and built-in support for fibers and channels. Its compact bytecode VM
delivers performance competitive with Lua 5.4 in our benchmarks, while
providing a much richer standard environment out of the box, including JSON,
HTTP, WebSockets, and more; SQLite comes as an installable package.

Mobius is equally at home embedded inside an application (the scripting layer
of a game engine, editor, or tool) or running standalone for scripts, CLI
tools, web services, and parallel data work.

```mobius
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
  A [.NET 10 binding](bindings/csharp/README.md) brings the same API to C# hosts.
  The core runtime ships as a single shared library—about 1.5 MB on disk, a few MB resident,
  and typically starts in under 10 ms—with no unusual dependencies. Optional
  modules stay separate, so you only ship what you use.

* **Real concurrency, without accidental races.** `spawn` and `await` fibers, channels,
  and `shared` data run across actual worker threads. Values passed between
  fibers are copied by default; memory is shared only when you explicitly ask for it.

* **Fast by design.** On the standard benchmark suite, Mobius delivers performance
  competitive with Lua 5.4 and several times faster than CPython overall.

* **Type locking for clean, efficient code.** Variables may be explicitly typed or
  inferred from their first use, then retain that type. This keeps ordinary Mobius
  code concise and annotation-light while allowing the VM to execute type-specialized
  instructions. Explicit types are optional, but can give the compiler more
  information to optimize performance-critical code.

* **Batteries included.** JSON, YAML, TOML, HTTP clients and a `web` server framework,
  WebSockets, sockets, regex, cryptography, compression, datetime, math,
  OS integration, and binary buffers with zero-copy struct views are built in;
  SQLite comes as an installable package.

* **Automatic, low-pause memory management.** Most objects are reclaimed immediately
  when they become unreachable, while a cycle collector handles the uncommon cases
  that reference counting alone cannot resolve.

Mobius is also deliberately familiar: C-style syntax and operators, 
flexible table-like data structures, and lightweight method syntax. 
If you've worked in C, JavaScript, Lua, or similar languages, Mobius should 
be readable from the start.

## Status

Mobius is version **0.1.0**. The language, VM, standard library, fiber
runtime, embedding API, and bundled modules are usable today. Packages install
from local `.mz` files with `tools/install_module.mob` (there is no package
download yet), and the `http`, `socket`, and `websocket` modules have no TLS
yet.

## Getting Mobius

Each [release](https://github.com/bholcomb/mobius/releases) has a
`mobius-linux-x64.tar.gz` holding `bin/` (the `mobius` interpreter,
`libmobius-core`, and the bundled modules), `packages/`, the C headers in
`include/`, and the C# binding in `dotnet/`. Windows and macOS releases come
later.

To build from source, put [buildy](https://github.com/bholcomb/buildy) on your
`PATH` (see [Getting Started](docs/guide/getting-started.md#building)). From
the repository root:

```bash
buildy -r                   # build (release) into bin/
```

The commands below run from the repository root after a build, or from the
top directory of a release archive.

## Quick start

Run a script or open the REPL:

```bash
bin/mobius script.mob                              # run a file
bin/mobius                                         # interactive REPL
MOBIUS_MODULE_PATH=packages bin/mobius script.mob  # with the packages (sqlite, …)
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
cc -Iinclude -o app app.c -Lbin -lmobius-core
LD_LIBRARY_PATH=bin ./app
```

From there you can register C functions, expose your own userdata types with
methods, and call Mobius functions back from C — see the
[Embedding Guide](docs/embedding/embedding-guide.md).

## Performance

Measured against both Lua 5.4 and CPython 3 with checksum-verified,
apples-to-apples benchmarks (medians of five runs, milliseconds, lower is
better; the ratio columns are Mobius ÷ that language — **below 1.0 means
Mobius is faster**):

| Benchmark | Mobius | Lua | CPython | vs Lua | vs Python |
|-----------|-------:|----:|--------:|-------:|----------:|
| Arithmetic (integer)           |  113.1 |  152.9 |   912.0 | **0.74×** | **0.12×** |
| Recursive calls (fib 30)       |   42.6 |   35.3 |    75.7 | 1.21× | **0.56×** |
| Array ops (dense numeric)      |   12.7 |   11.1 |    62.5 | 1.14× | **0.20×** |
| Table ops (string-key map)     |   17.8 |   13.7 |    30.6 | 1.30× | **0.58×** |
| String ops                     |   48.8 |   89.6 |    29.6 | **0.55×** | 1.65× |
| Nested loops                   |   48.4 |   50.4 |   301.5 | **0.96×** | **0.16×** |
| Object create / destroy        |   66.5 |   86.3 |    57.2 | **0.77×** | 1.16× |
| Mixed workload                 |   47.6 |   52.8 |    34.6 | **0.90×** | 1.38× |
| **Total**                      | **421.1** | **527.8** | **1555.0** | **0.80×** | **0.27×** |

Measured on an AMD Ryzen 7 7840HS (Linux, Lua 5.4.6, CPython 3.12.3) on
2026-10-12 with `python3 benchmarks/run_benchmarks.py`, which writes
[`benchmarks/benchmark_summary_latest.json`](benchmarks/benchmark_summary_latest.json).


## Documentation

Full documentation lives in [`docs/`](docs/index.md).

| Guide | Description |
|-------|-------------|
| [Getting Started](docs/guide/getting-started.md)        | Build, run, the REPL, the CLI, and your first program |
| [Language Tour](docs/guide/language-tour.md)            | The whole language in one page |
| [Language Guide](docs/index.md#guide)                   | Types, control flow, functions, collections, binary data, concurrency |
| [Standard Library](docs/reference/standard-library.md)  | Built-in functions available without `import` |
| [Module Reference](docs/modules/index.md)               | Built-in modules (`json`, `os`, `http`, `web`, …) and packages (`sqlite`, …) |
| [Embedding Guide](docs/embedding/embedding-guide.md)    | Embed Mobius in a C/C++ application |
| [Plugin Guide](docs/embedding/plugin-guide.md)          | Write native `.so`/`.dll` modules |
| [C# binding](bindings/csharp/README.md)                 | Embed Mobius in a .NET 10 application |
| [Grammar (BNF)](docs/reference/grammar.md)              | The canonical formal grammar |

The [`examples/`](examples/) directory has embedding examples (C and C++),
plugin and userdata examples, networking servers, and pure-Mobius demo
scripts.

## License

Mobius is released under the [MIT License](LICENSE).
