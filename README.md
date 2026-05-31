# Mobius

**Mobius is a small, fast, Lua-inspired scripting language** — equally at home
embedded inside a C/C++ application or running standalone scripts. It keeps
Lua's lightweight feel and familiar table / `:` method model, wraps it in
C-style syntax, and adds modern conveniences: inferred **type locking**,
fiber-based **concurrency**, a **pattern-matching `switch`**, enums, and a
batteries-included **standard library**.

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
([`benchmarks/benchmark_comprehensive.*`](benchmarks/)), run on one machine and
averaged over five runs — **milliseconds, lower is better**:

| Benchmark | Mobius | Lua | CPython |
|-----------|-------:|----:|--------:|
| Arithmetic                     | 22.6 | 17.1 | 115.3 |
| Recursive calls (Fibonacci)    | 13.9 |  4.5 |   8.6 |
| Array ops (dense numeric)      | 0.55 | 0.37 |  0.78 |
| Table ops (string-key map)     | 0.75 | 0.64 |  0.75 |
| String ops                     | 1.02 | 0.53 |  0.21 |
| Nested loops                   | 3.20 | 1.64 | 10.61 |
| Object create / destroy        | 10.7 |  5.5 |   2.8 |
| Mixed workload                 | 1.50 | 0.47 |  0.35 |
| **Total**                      | **58.8** | **32.7** | **140.1** |

Takeaways:

- **~2.4× faster than CPython** overall on this suite, and within roughly
  **1.8× of Lua**.
- Type locking pays off most on **arithmetic and tight numeric loops**, where
  Mobius is several times faster than CPython.
- Lua still leads on deep recursion and object churn; CPython remains
  competitive or ahead on recursion-heavy, string, and allocation-heavy code —
  areas with room to improve.

These are indicative micro-benchmarks, not a rigorous cross-language benchmark.
Reproduce them with the harness in [`benchmarks/`](benchmarks/).

## Status

Mobius is version **0.1.0**. The language, VM, standard library, fiber runtime,
embedding API, and bundled modules are usable today. The package system and the
`http` / `socket` / `websocket` modules are plain-transport only for now — TLS
is not yet included.

## License

Mobius is released under the [MIT License](LICENSE).
