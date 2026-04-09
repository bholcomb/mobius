# `yaml` Module

Import:

```mobius
import "yaml"
```

The `yaml` module provides a dependency-free YAML parser and serializer for the
common configuration subset:

- mappings (`key: value`)
- sequences (`- item`)
- nested indentation-based documents
- plain, quoted, boolean, null, integer, and float scalars
- basic flow-style arrays and objects (`[a, b]`, `{key: value}`)
- root inline documents like `{...}`, `[...]`, and quoted scalars
- common single-document markers (`---` and `...`)
- comments introduced by `#`

Functions:

| Function | Description |
|---|---|
| `yaml.parse(text)` | Parse a YAML string into Mobius values. |
| `yaml.parsefile(path)` | Parse a YAML file from disk. |
| `yaml.stringify(value)` | Serialize Mobius arrays/tables/scalars to YAML. |

Current limitations:

- This is intentionally a useful subset, not the full YAML spec.
- Anchors, aliases, tags, block scalars (`|` / `>`), and other advanced YAML
  features are not supported yet.
- Malformed trailing commas in flow collections are rejected.
- `yaml.stringify(...)` sorts table keys for stable output.

Example:

```mobius
import "yaml"

var cfg = yaml.parse("
service: mobius
ports:
  - 8080
  - 8081
")

print(cfg.service)
print(yaml.stringify(cfg))
```
