# Packaging

Mobius packages bundle a module's manifest, optional `.mob` entry script, and
platform-specific native libraries so they can be distributed and installed.
Packages are shipped as `.mz` archives (a zip-based container, built with the
[`compression`](../modules/compression.md) module).

[← Documentation home](../index.md) · [Plugin Guide](plugin-guide.md)

---

## Installed layout

Installed packages live under a modules root, one directory per package:

```text
modules/
  sqlite/
    module.yaml
    sqlite.mob
    native/
      linux-x86_64/
        sqlite.so
        libsqlite3.so
      windows-x86_64/
        sqlite.dll
        sqlite3.dll
```

A package may be:

- **script-only** (just a `.mob` entry),
- **native-only** (just a module library), or
- **hybrid** (native code plus a companion `.mob` — see the
  [`.mob` companion pattern](plugin-guide.md#the-mob-companion-pattern)).

At least one of `entry.script` or `platforms.<target>.module_library` must be
present.

---

## `module.yaml`

```yaml
name: sqlite
version: 0.1.0
entry:
  script: sqlite.mob
platforms:
  linux-x86_64:
    module_library: native/linux-x86_64/sqlite.so
    runtime_libraries:
      - native/linux-x86_64/libsqlite3.so
dependencies:
  mobius_modules: []
metadata:
  description: SQLite bindings for Mobius
```

Rules:

- `name` is the logical import name and should match the package directory name.
- `version` is required.
- `entry.script` is optional.
- `platforms` is optional for script-only packages.
- `runtime_libraries` is optional; if present it should live beside
  `module_library`.

Canonical platform keys: `linux-x86_64`, `linux-aarch64`, `windows-x86_64`,
`macos-x86_64`, `macos-aarch64`. (`os.platform` reports the same keys — see the
[`os` module](../modules/os.md).)

---

## Creating packages

Build a `.mz` archive with the bundled tool:

```bash
bin/mobius tools/pack_module.mob <package_dir> [output.mz]
```

It validates `module.yaml`, validates the referenced files, and creates a
zip-based `.mz`. If `output.mz` is omitted, it writes
`packages/<name>-<version>.mz`.

## Installing packages

```bash
bin/mobius tools/install_module.mob <package.mz> [modules_root]
```

The installer validates the archive layout, extracts to a temporary directory,
validates `module.yaml` for the current `os.platform`, and moves the package
into the target modules root (default `./modules`).

For the CLI to find an installed package, that modules root must be one the
interpreter searches: the `modules/` directory next to the `mobius` executable,
or a directory named in `MOBIUS_MODULE_PATH`. So either install into the
executable's `modules/` directory, or point `MOBIUS_MODULE_PATH` at the install
root. See [Modules and Packages](../guide/modules-and-packages.md#packages).

---

## Standalone executables

`tools/bundle.mob` turns an application into one executable file that runs
on another machine without Mobius installed. Describe the application in an
`app.yaml` (paths relative to it):

```yaml
name: mytool            # output file name (default: the entry script's name)
entry: main.mob         # the script to run
files: [lib, data]      # more scripts and data files: files or directories
modules: [json, process]  # installed modules it imports; dependencies are added
module_paths: [vendor]  # optional: more directories holding installed modules
```

Modules are looked up in the runtime's `modules/` directory, then the
`packages/` directory beside it (where packages such as `sqlite` and `glfw`
are staged), then `module_paths`. A package's own native dependencies (for
`sqlite`, `libsqlite3.so`) are part of the package and travel with it.

Build the distribution runtime once, then bundle:

```sh
tools/build_portable.sh                       # dist/portable/bin, for any x86-64-v2 CPU
bin/mobius tools/bundle.mob app.yaml -o mytool
./mytool arg1 arg2
```

The bundle is a small launcher with an archive appended: the core library,
the listed modules (as installed, native libraries included), the entry
script and the files. On first run it extracts the archive to
`~/.cache/mobius/bundles/<hash>/` (`$XDG_CACHE_HOME` and
`MOBIUS_BUNDLE_CACHE` are honored) and runs the entry script there; later
runs reuse that directory, and a bundle with different content gets its own.
Native libraries must be files to be loaded, so extracting is how a bundle
carries them.

Inside a bundle:

- `argv` holds the command-line arguments, as for `mobius main.mob args...`;
- `exit(code)` ends the program with that code; an uncaught error exits
  with 1;
- `import` finds bundled scripts next to the entry script and the bundled
  modules;
- `bundle_dir` is the directory holding the bundle's files, so data files
  are read as `readfile(bundle_dir + "/data/config.toml")`. Keep files a
  program writes elsewhere (the extraction directory is a cache).

The target machine needs the C and C++ runtime libraries (`libc`,
`libstdc++`) and a CPU at least at x86-64-v2 (any x86-64 CPU from about 2009
on). The regular build in `bin/` is tuned for the build machine's CPU
(`-march=native`), which is why bundles use `dist/portable`. Bundles are
Linux-only for now, and stored uncompressed.

## Notes

The runtime loader, archive builder, and installer are all in place. The
bundled first-party modules are staged into this same
`modules/<name>/module.yaml` layout, so there is no separate path for shipped
versus installed modules.
