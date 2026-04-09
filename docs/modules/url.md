# `url` Module

Import:

```mobius
import "url"
```

The `url` module provides URL parsing, rebuilding, percent-encoding, and query
string helpers.

Functions:

| Function | Description |
|---|---|
| `url.parse(text)` | Parse a URL into a table of components. |
| `url.build(parts)` | Build a URL string from a parts table. |
| `url.encode(text)` | Percent-encode a URL component. |
| `url.decode(text)` | Percent-decode a URL component. |
| `url.parse_query(text)` | Parse a query string into a table. |
| `url.build_query(table)` | Build a query string from a table. |

Parsed URL tables include fields such as:

- `href`
- `scheme`
- `username`
- `password`
- `host`
- `port`
- `path`
- `query`
- `fragment`
- `has_authority`

Example:

```mobius
import "url"

var parsed = url.parse("https://example.com/search?q=mobius#top")
print(parsed.host)
print(parsed.query)

var query = url.build_query({q: "mobius lang", page: "1"})
print(query)
```
