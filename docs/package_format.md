# Package Format

Mobius packages are distributed as `.mz` archives. In v1, `.mz` is a zip-based
container created with the `compression` module.

## Installed Layout

Installed packages live under a modules root using a directory-per-package
layout:

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

- script-only
- native-only
- hybrid with both native code and a companion `.mob` file

At least one of `entry.script` or `platforms.<target>.module_library` must be
present.

## `module.yaml`

Minimal v1 example:

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

- `name` is the logical import name and should match the package directory name
- `version` is required
- `entry.script` is optional
- `platforms` is optional for script-only packages
- `runtime_libraries` is optional, but if present should live beside
  `module_library`

Canonical platform keys:

- `linux-x86_64`
- `linux-aarch64`
- `windows-x86_64`
- `macos-x86_64`
- `macos-aarch64`

## Creating Packages

Package archives are created with a Mobius script:

```bash
./bin/mobius tools/pack_module.mob <package_dir> [output.mz]
```

The tool:

1. validates `module.yaml`
2. validates referenced files in the package directory
3. creates a zip-based `.mz` archive using the `compression` module

If `output.mz` is omitted, the tool writes `packages/<name>-<version>.mz`.

The `packages/` folder is intended for built package artifacts that can later be
installed into a modules root such as `./modules/`.

## Installing Packages

Installed packages are unpacked with a Mobius script:

```bash
./bin/mobius tools/install_module.mob <package.mz> [modules_root]
```

The installer:

1. validates the archive layout
2. extracts into a temporary directory
3. validates `module.yaml` for the current `os.platform`
4. moves the package into the target modules root

If `modules_root` is omitted, the installer writes into `./modules`.

## Current Scope

The runtime loader, package archive builder, and package installer are all in
place. Built-in first-party modules are staged into the same
`modules/<name>/module.yaml` layout used by installable packages, so there is
no separate legacy flat-module path for shipped modules.

The next follow-up task is building and packaging the first external modules.
