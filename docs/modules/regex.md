# `regex` Module

Import:

```mobius
import "regex"
```

Functions:

| Function | Description |
|---|---|
| `regex.match(pattern, string)` | Match the entire string. Returns a match table or `nil`. |
| `regex.match(pattern, string, flags_or_options)` | Match with optional flags such as `"i"` or `{ignore_case: true}`. |
| `regex.search(pattern, string)` | Search for the first match anywhere in the string. |
| `regex.search(pattern, string, flags_or_options)` | Search with optional flags/options. |
| `regex.findall(pattern, string, flags_or_options)` | Return an array of match tables. |
| `regex.replace(pattern, string, replacement, flags_or_options)` | Replace all matches with optional flags/options. |
| `regex.split(pattern, string, flags_or_options)` | Split a string using a regex delimiter. |

Supported flags today:

- `i` for case-insensitive matching

Match tables include:

- `match` and `full` for the full matched text
- `groups` as an array, even when there are no captures
- `group_count`
- `start` and `end` offsets

Example:

```mobius
import "regex"

var m = regex.search("([a-z]+)([0-9]+)", "item42")
print(m.full)
print(m.groups[0])

var ci = regex.search("hello", "HeLLo world", "i")
print(ci.match)

var parts = regex.split("\\s+", "one   two   three")
print(parts[1])
```
