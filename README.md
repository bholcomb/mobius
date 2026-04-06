# Mobius

Mobius is a type-inferred scripting language designed to be easy to extend
with C functions. Variables are type-locked — their type is inferred from the
first non-nil assignment and cannot change, enabling the compiler to emit
specialized opcodes and eliminate runtime type checks. Mobius takes a lot of
inspiration from Lua and adds features like type locking, enums, a rich
`switch` with pattern matching, and familiar C-style syntax.

The interpreter ships with a bytecode VM (default) and an older tree-walk
backend selectable via `--tree-walk`.

## Quick Start

```bash
# Run a script
mobius script.mob

# Start the REPL
mobius

# Use the tree-walk interpreter
mobius --tree-walk script.mob
```

```mobius
var name = "World"

func greet(who) {
    print("Hello,", who)
}

greet(name)
```

## Documentation

| Guide | Description |
|-------|-------------|
| [Language Reference](docs/language_reference.md) | Syntax, types, control flow, functions, closures, tables, enums |
| [Standard Library Reference](docs/stdlib_reference.md) | Built-in functions: math, string, array, table, utility |
| [Embedding Guide](docs/embedding_guide.md) | How to embed Mobius in a C/C++ application |
| [Plugin Guide](docs/plugin_guide.md) | How to write native plugin modules (.so/.dll) |
| [Formal Grammar (BNF)](docs/BNF.md) | Canonical grammar specification |

## Building

Mobius uses [Buildy](https://github.com/) as its build system. From the
project root:

```bash
buildy build
```

## Examples

See the [`examples/`](examples/) directory for:

- **simple_embedding** — Minimal C program embedding Mobius
- **embedding_example** — Value exchange, custom functions, error handling
- **game_engine** — Game host with Mobius scripting
- **text_processing_plugin** — Example plugin with string utilities
- **multi_environment_demo** — Running multiple interpreter instances
- **demo_scripts** — Pure Mobius scripts demonstrating language features

## Tests

```bash
./test_simple.sh         # Run all tests (VM)
./test_simple_tw.sh      # Run all tests (tree-walk)
```

Test scripts live in [`tests/`](tests/) organized by category: `basic/`,
`types/`, `tables/`, `errors/`, `functions/`, and `integration/`.
