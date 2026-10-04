# `regex` Module

Import:

```mobius
import "regex"
```

[← Module reference](index.md)

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

## Syntax

Patterns are POSIX extended regular expressions, the same on every platform:

| Syntax | Meaning |
|---|---|
| `.` | Any byte (newlines included) |
| `[abc]`, `[a-z]`, `[^abc]` | A byte in (or not in) the set. `]` first and `-` first or last are literal; `\` is literal inside brackets. |
| `[[:alpha:]]` | A class: `alpha`, `digit`, `alnum`, `upper`, `lower`, `space`, `blank`, `punct`, `print`, `graph`, `cntrl`, `xdigit` (ASCII) |
| `^`, `$` | Start, end of the string |
| `a\|b` | Either |
| `(...)` | A group (captured) |
| `*`, `+`, `?` | 0 or more, 1 or more, 0 or 1 |
| `{m}`, `{m,}`, `{m,n}` | Exactly m, at least m, m to n (up to 32767) |
| `\w`, `\W` | A word byte (letter, digit, `_`), or not |
| `\s`, `\S` | A whitespace byte, or not |
| `\b`, `\B` | A word boundary, or not |
| `\<`, `\>` | Start, end of a word |
| `\.` etc. | Any other escaped character is itself (`\d` is `d`) |

The match found is the leftmost-longest one: `regex.search("a|ab", "ab")` matches
`ab`. Matching is by bytes, and never backtracks: time grows with the length of
the input times the size of the pattern, so any pattern is safe on any input,
including inside fibers. Back-references (`\1`) are not supported. A bad
pattern raises an error that says what is wrong.

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
