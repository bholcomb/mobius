# Mobius Documentation

Mobius is a type-inferred scripting language designed to be embedded in C and
C++ applications. Variables are **type-locked** — a variable's type is inferred
from its first non-nil assignment and cannot change — which lets the bytecode VM
emit type-specialized opcodes and skip runtime type checks on the hot path.
Mobius takes inspiration from Lua and adds type locking, enums, a rich `switch`
with pattern matching, fibers with channels, and a familiar C-style syntax.

Source files use the `.mob` extension.

```mobius
enum Suit { CLUBS, DIAMONDS, HEARTS, SPADES }

func describe(card) {
    switch (card.rank) {
        case 1:        return "Ace"
        case 11..13:   return "Face card"
        default:       return str(card.rank)
    }
}

var card = { rank: 12, suit: Suit.HEARTS }
print(describe(card))    // "Face card"
```

---

## Where to start

| If you want to…                                   | Read                                              |
|---------------------------------------------------|---------------------------------------------------|
| Build Mobius and run your first script            | [Getting Started](guide/getting-started.md)       |
| See the whole language in one page                | [Language Tour](guide/language-tour.md)           |
| Learn the language in depth                       | The [Guide](#guide) pages below                   |
| Look up a built-in function                       | [Standard Library](reference/standard-library.md) |
| Use a bundled module (`json`, `os`, `http`, …)    | [Module Reference](modules/index.md)              |
| Embed Mobius in a C/C++ program                   | [Embedding Guide](embedding/embedding-guide.md)   |
| Write a native plugin module                      | [Plugin Guide](embedding/plugin-guide.md)         |
| Read the exact grammar                            | [Grammar (BNF)](reference/grammar.md)             |

---

## Guide

A progressive tour of the language. Each page builds on the last, but they also
stand alone as references for a single topic.

1. [Getting Started](guide/getting-started.md) — build, run, the REPL, the CLI, and your first program
2. [Language Tour](guide/language-tour.md) — a fast, example-driven overview of everything
3. [Values and Types](guide/values-and-types.md) — type locking, annotations, literals, conversions
4. [Control Flow](guide/control-flow.md) — `if`/`while`/`for` and the pattern-matching `switch`
5. [Functions](guide/functions.md) — declarations, annotations, first-class values, closures
6. [Error Handling](guide/error-handling.md) — `throw`, `try`/`catch`/`finally`
7. [Collections](guide/collections.md) — arrays, tables, metatables, OOP patterns, enums
8. [Binary Data](guide/binary-data.md) — buffers and `struct` views over raw memory
9. [Concurrency](guide/concurrency.md) — fibers, `spawn`/`await`, `shared`, `atomic`, channels
10. [Modules and Packages](guide/modules-and-packages.md) — `import`, `load`, and installable packages

## Reference

- [Standard Library](reference/standard-library.md) — built-in globals available without `import`
- [Module Reference](modules/index.md) — the bundled importable modules
- [Grammar (BNF)](reference/grammar.md) — the canonical formal grammar

## Embedding & Extending

- [Embedding Guide](embedding/embedding-guide.md) — run Mobius from C/C++, exchange values, register functions
- [Plugin Guide](embedding/plugin-guide.md) — author native `.so`/`.dll` modules
- [Packaging](embedding/packaging.md) — `module.yaml` and the `.mz` package format

---

## Project status

Mobius is at version **0.1.0**. The language, VM, standard library, fiber
runtime, embedding API, and the bundled modules are usable today. The package
manager (`.mz` archives) and several modules (`http`, `socket`, `websocket`) are
explicitly plain-transport-only for now — TLS is not yet included.
