# `http` Module

Import:

```mobius
import "http"
```

[← Module reference](index.md)

The `http` module provides dependency-free HTTP protocol helpers plus a first
plain-TCP client request path. TLS is not included yet.

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
| `http.request(spec)` | Perform a plain HTTP request over TCP and return a parsed response table. |

Request/response tables include normalized lowercase header keys under
`headers`, plus convenience fields like `method`, `target`, `path`, `query`,
`status`, `reason`, `body`, `content_length`, and `keep_alive` where relevant.

`http.request(spec)` notes:

- Plain HTTP only. HTTPS will come later with TLS.
- `spec` accepts `host`, optional `port`, optional `method`, `path`, `query`,
  `target`, `headers`, optional `body` as either `string` or `buffer`, and
  optional `timeout_ms`.
- The returned response is buffer-first: `response.body` is a `buffer`.
- When the response body is valid UTF-8, `response.text` is also provided as a
  convenience helper.
- `Transfer-Encoding: chunked` responses are decoded automatically.

Example:

```mobius
import "http"

var req = http.build_request({
    method: "POST",
    path: "/submit",
    host: "example.com",
    headers: { ["content-type"] = "application/json" },
    body: "{\"ok\":true}"
})

var parsed = http.parse_request(req)
print(parsed.method)
print(parsed.headers.host)

var response = http.request({
    host: "example.com",
    path: "/",
    timeout_ms: 5000
})

print(response.status)
print(typeof(response.body))
```
