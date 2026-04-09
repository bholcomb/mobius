# `http` Module

Import:

```mobius
import "http"
```

The `http` module provides dependency-free HTTP protocol helpers for text
messages. It currently focuses on parsing and building requests, responses, and
header blocks. It does not provide sockets or TLS yet.

Functions:

| Function | Description |
|---|---|
| `http.status_text(code)` | Return the standard reason phrase for a status code. |
| `http.parse_headers(text)` | Parse raw header lines into a lowercase-keyed table. |
| `http.build_headers(headers)` | Build CRLF-separated header lines from a table. |
| `http.parse_request(text)` | Parse a raw HTTP request string into a table. |
| `http.parse_response(text)` | Parse a raw HTTP response string into a table. |
| `http.build_request(spec)` | Build a raw HTTP request string from a table. |
| `http.build_response(spec)` | Build a raw HTTP response string from a table. |

Request/response tables include normalized lowercase header keys under
`headers`, plus convenience fields like `method`, `target`, `path`, `query`,
`status`, `reason`, `body`, `content_length`, and `keep_alive` where relevant.

Example:

```mobius
import "http"

var req = http.build_request({
    method: "POST",
    path: "/submit",
    host: "example.com",
    headers: {"content-type": "application/json"},
    body: "{\"ok\":true}"
})

var parsed = http.parse_request(req)
print(parsed.method)
print(parsed.headers.host)
```
