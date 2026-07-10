# Getting Started

This page gets you from a clean checkout to running your own Mobius scripts.

[← Documentation home](../index.md)

---

## Building

Mobius is built with [Buildy](https://github.com/). From the project root,
build the release configuration with the bundled `buildy` binary:

```bash
./buildy -r
```

(Use `./buildy` on its own for a debug build.) This produces, under
`build/<platform>-release/bin/` and staged into `bin/`:

- `bin/mobius` — the command-line interpreter and REPL
- `bin/libmobius-core.so` — the embeddable runtime library
- `bin/modules/<name>/` — the bundled modules (`json`, `os`, `http`, …) as
  loadable plugins

The C/C++ examples under `examples/` and the bundled module plugins are part of
the same Buildy workspace (see `buildy.yaml`), so a single `./buildy -r`
compiles everything.

---

## Running a script

```bash
bin/mobius script.mob
```

Create `hello.mob`:

```mobius
// hello.mob — your first Mobius program
print("Hello, world!")

var name = "Mobius"
var version = "0.1.0"

func greet(who) {
    print("Welcome to", who, version)
}

greet(name)
```

```bash
$ bin/mobius hello.mob
Hello, world!
Welcome to Mobius 0.1.0
```

A script's exit status is `0` on success and non-zero if a syntax or runtime
error occurs.

---

## The REPL

Running `bin/mobius` with no script file starts an interactive
read-eval-print loop:

```bash
$ bin/mobius
Mobius Scripting Language Interpreter v0.1.0
> var x = 21
> print(x * 2)
42
```

---

## Command-line options

```
Usage: mobius [options] [script_file] [script_args...]
```

| Flag             | Description                                              |
|------------------|----------------------------------------------------------|
| `--help`, `-h`   | Show usage and exit                                      |
| `--debug`, `-d`  | Enable debug mode (`config.debug_mode`)                  |

Options must come **before** the script file. Anything after the script file is
passed to the script as positional arguments.

### Script arguments — `argv`

Positional arguments that follow the script filename are exposed to the script
as a read-only global array named `argv`:

```bash
bin/mobius tools/example.mob alpha "two words" --flag
```

```mobius
print(argv[0])    // "alpha"
print(argv[1])    // "two words"
print(argv[2])    // "--flag"
print(size(argv))  // 3
```

`argv` contains only the arguments **after** the script name; the script name
itself is not included.

---

## How modules are found

Some functionality lives in modules you load with `import`:

```mobius
import "json"
import "os"

print(os.getcwd())
print(json.stringify({ok: true}))
```

The interpreter looks for modules in:

1. A `modules/` directory **next to the `mobius` executable** — this is where
   the bundled modules ship (`bin/modules/`, since the binary is `bin/mobius`).
2. Each directory listed in the `MOBIUS_MODULE_PATH` environment variable, a
   `:`-separated list (`;` on Windows).

Because resolution is relative to the executable (not the current directory),
scripts can `import` the bundled modules no matter where you run them from. To
make additional or installed packages importable, point `MOBIUS_MODULE_PATH` at
their directory, or place them under the executable's `modules/` directory — see
[Packaging](../embedding/packaging.md). When you embed Mobius yourself, you
choose the search paths with `mobius_add_plugin_directory()` — see the
[Embedding Guide](../embedding/embedding-guide.md#loading-plugins).

The `fiber` builtin (a global table) and all the functions in the
[Standard Library](../reference/standard-library.md) are built into the runtime
and need no `import`.

## Environment variables

| Variable | Purpose |
|----------|---------|
| `MOBIUS_MODULE_PATH` | Extra module search directories (`:`-separated; `;` on Windows). |
| `MOBIUS_HASH_SEED`   | Pin the string-hash seed (decimal or `0x…`). By default it is randomized per process, so [table iteration order](collections.md#iteration-order) varies between runs. Set this to reproduce an order while debugging. |

---

## Where to go next

- [Language Tour](language-tour.md) — see the whole language quickly
- [Values and Types](values-and-types.md) — the type-locking model in detail
- [Module Reference](../modules/index.md) — what each bundled module offers
