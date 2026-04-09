# `json` Module

Import:

```mobius
import "json"
```

Functions:

| Function | Description |
|---|---|
| `json.parse(string)` | Parse a JSON string into Mobius values. |
| `json.parsefile(path)` | Read and parse a JSON file from disk. |
| `json.stringify(value)` | Serialize a value to compact JSON. |
| `json.stringify(value, indent)` | Serialize with pretty-print indentation. |

Value mapping:

- JSON objects become Mobius tables.
- JSON arrays become Mobius arrays.
- JSON strings, booleans, and `null` map to Mobius strings, bools, and `nil`.
- Numbers are parsed as integer or float depending on the literal.

Example:

```mobius
import "json"

var config = json.parsefile("config.json")
print(config.name)

var text = json.stringify({
    enabled: true,
    retries: 3
}, 2)
print(text)
```
