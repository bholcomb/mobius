# `regex` Module

Import:

```mobius
import "regex"
```

Functions:

| Function | Description |
|---|---|
| `regex.match(pattern, string)` | Match the entire string. Returns a match table or `nil`. |
| `regex.search(pattern, string)` | Search for the first match anywhere in the string. |
| `regex.findall(pattern, string)` | Return an array of match tables. |
| `regex.replace(pattern, string, replacement)` | Replace all matches. |
| `regex.split(pattern, string)` | Split a string using a regex delimiter. |

Match tables include the matched text, capture groups, and start/end offsets.

Example:

```mobius
import "regex"

var m = regex.search("([a-z]+)([0-9]+)", "item42")
print(m.full)
print(m.groups[0])

var parts = regex.split("\\s+", "one   two   three")
print(parts[1])
```
