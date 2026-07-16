# Modules and Packages

[← Documentation home](../index.md) · [Guide: Concurrency](concurrency.md)

Mobius keeps the language core small and ships extra functionality as
**modules** you load with `import`. There are two ways to bring code into a
script: `import` for modules, and `load` for plain `.mob` files.

---

## import

`import` loads a module and exposes its functions under a namespace named after
the module:

```mobius
import "math"
import "json"

print(math.hypot(3, 4))             // 5.0
print(json.stringify({ ok: true })) // {"ok":true}
```

### Import with an alias

```mobius
import "math" as m

print(m.cos(0))      // 1.0
print(m.sign(-16))   // -1
```

### Where modules come from

A module is either:

- a **builtin global** (`fiber`), always available with no `import` at all, or
- a **plugin module** — a shared library (`.so`/`.dll`), optionally paired with a
  `.mob` companion script that wraps it in a friendlier API.

The CLI searches for plugin modules:

1. In a `modules/` directory next to the `mobius` executable (where the bundled
   `math`, `os`, `json`, `http`, … modules ship).
2. In each directory in the `MOBIUS_MODULE_PATH` environment variable (a
   `:`-separated list, `;` on Windows).

Because the bundled modules are resolved relative to the executable, `import`
works no matter what directory you run a script from. The full list is in the
[Module Reference](../modules/index.md). When you embed Mobius, you set the
search paths yourself with `mobius_add_plugin_directory()` — see the
[Embedding Guide](../embedding/embedding-guide.md#loading-plugins).

Modules are loaded lazily on first `import` and cached for the life of the
interpreter.

---

## load

`load(path)` executes another Mobius source file. Globals defined in that file
(functions and variables) become available in the calling scope:

```mobius
load("utils.mob")
// functions and globals from utils.mob are now in scope

print(my_helper(42))
```

Use `load` to split a program across files; use `import` to pull in a packaged
module by name.

---

## Packages

Larger or third-party modules are distributed as **packages**: a directory
containing a `module.yaml` manifest, an optional `.mob` entry script, and
platform-specific native libraries. Packages are shipped as `.mz` archives
(zip-based) and installed into a `modules/` root.

A package directory looks like:

```text
modules/
  sqlite/
    module.yaml
    sqlite.mob
    native/
      linux-x86_64/
        sqlite.so
```

Install a package archive with the bundled tool:

```bash
bin/mobius tools/install_module.mob <package.mz> [modules_root]
```

Install it where the interpreter will find it — the `modules/` directory next
to the `mobius` executable, or any directory on `MOBIUS_MODULE_PATH` — then
import it like any other module:

```mobius
import "sqlite"

var db = sqlite.open(":memory:")
```

The bundled first-party modules are staged into this same
`modules/<name>/module.yaml` layout, so there is no separate path for shipped
versus installed modules. For the full manifest format and how to build a `.mz`,
see [Packaging](../embedding/packaging.md).

The repository also includes several **experimental** native packages —
`sqlite`, `glfw`, `monstro` (immediate-mode UI), and `vulkease` (Vulkan). Of
these, `sqlite` is documented in the [Module Reference](../modules/sqlite.md);
the others are previewed in [Packages](../modules/packages.md).

---

This is the end of the guide. Continue to the
[Standard Library reference](../reference/standard-library.md) or the
[Module Reference](../modules/index.md).
