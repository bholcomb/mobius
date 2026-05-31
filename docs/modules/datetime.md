# `datetime` Module

Import:

```mobius
import "datetime"
```

[← Module reference](index.md)

The `datetime` module provides structured date/time values on top of the lower
level time helpers in `os`.

Functions:

| Function | Description |
|---|---|
| `datetime.now()` | Current local datetime as a table. |
| `datetime.utc_now()` | Current UTC datetime as a table. |
| `datetime.from_unix(ts)` | Convert a Unix timestamp to a local datetime table. |
| `datetime.from_unix_utc(ts)` | Convert a Unix timestamp to a UTC datetime table. |
| `datetime.to_unix(dt)` | Convert a datetime table back to a Unix timestamp. |
| `datetime.format(fmt, value)` | Format a datetime table or timestamp with `strftime` syntax. |
| `datetime.isoformat(dt)` | Render a datetime table as ISO-8601 text. |
| `datetime.parse_iso(text)` | Parse an ISO-8601 date or datetime string. |

Datetime tables contain fields such as:

- `year`, `month`, `day`
- `hour`, `min`, `sec`
- `wday`, `yday`, `isdst`
- `utc`, `timestamp`
- `offset_minutes` when a parsed ISO string includes an explicit timezone offset

Example:

```mobius
import "datetime"

var now = datetime.utc_now()
print(datetime.isoformat(now))

var parsed = datetime.parse_iso("2024-06-15T12:30:45Z")
print(datetime.to_unix(parsed))
print(datetime.format("%Y-%m-%d", parsed))
```
