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
| Arithmetic (integer)           |  113.9 |  119.8 |   844.8 | **0.95×** |
| Recursive calls (fib 30)       |   65.4 |   33.2 |    69.8 | 1.97× |
| Array ops (dense numeric)      |   21.3 |    9.7 |    54.9 | 2.19× |
| Table ops (string-key map)     |   20.1 |   11.8 |    27.7 | 1.70× |
| String ops                     |   74.0 |   66.4 |    27.3 | 1.11× |
| Nested loops                   |   44.0 |   36.9 |   280.1 | 1.19× |
| Object create / destroy        |  130.8 |   79.6 |    53.5 | 1.64× |
| Mixed workload                 |   78.9 |   40.4 |    31.1 | 1.95× |
| **Total**                      | **575.3** | **421.4** | **1410.3** | **1.37×** |

Takeaways:

- **~2.5× faster than CPython** overall, and within **1.4× of Lua**.
- Type locking pays off most on **arithmetic and tight numeric loops** — Mobius
  is *faster than Lua* on integer arithmetic, and 7× faster than CPython on it.
- Strings are near parity with Lua (**1.1×**): computed strings are refcounted
  rather than interned, so they are reclaimed instead of accumulating.
- Lua still leads on **recursion** (parameters are the values the compiler cannot
  type from an initializer) and on **object creation**. CPython, with its heavily
  tuned string and dict machinery, stays ahead on string-heavy code.

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
