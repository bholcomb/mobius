# `toml` Module

Import:

```mobius
import "toml"
```

[← Module reference](index.md)

Functions:

| Function | Description |
|---|---|
| `toml.parse(string)` | Parse a TOML string into a table. |
| `toml.parsefile(path)` | Read and parse a TOML file from disk. |
| `toml.stringify(table)` | Serialize a table to TOML. |
| `toml.stringify(table, options)` | Serialize with options such as stable key ordering via `sort_keys`. |

Notes:

- The module is intended for configuration-style data.
- `toml.stringify(...)` expects a table at the top level.

Example:

```mobius
import "toml"

var cfg = toml.parsefile("mobius.toml")
print(cfg.server.host)

var out = toml.stringify({
    app: {
        name: "mobius"
    }
})
print(out)

var stable = toml.stringify({z: 1, a: 2}, {sort_keys: true})
print(stable)
```
