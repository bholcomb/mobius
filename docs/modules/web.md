# `web` Module

Import:

```mobius
import "web"
```

The `web` module is a higher-level server framework built in Mobius on top of
`socket`, `http`, `websocket`, `fiber`, and `channel` primitives. It provides a
router, middleware, request context helpers, and a worker-fan-out server loop.

Utility helpers:

- `web.html_escape(text)` escapes `&`, `<`, `>`, and `"` for safe HTML output.

## Quick Example

```mobius
import "web"

var app = web.app()

app:use(func(ctx, next) {
    ctx:set_header("x-powered-by", "mobius-web")
    return next()
})

app:get("/hello/:name", func(ctx) {
    return ctx:text("hello " + ctx:param("name"))
})

app:get("/api/health", func(ctx) {
    return ctx:json({ok: true})
})

app:listen("127.0.0.1", 8080, {
    worker_count: 4,
    channel_capacity: 16
})
```

A runnable example lives at `examples/networking/web_hello_server.mob`.

## Core API

### `web.app()`

Create a new application/router object.

Application methods:

- `app:use(handler)`
- `app:get(path, handler)`
- `app:post(path, handler)`
- `app:put(path, handler)`
- `app:delete(path, handler)`
- `app:websocket(path, handler)`
- `app:on_error(handler)`
- `app:on_not_found(handler)`
- `app:serve(listener, options)`
- `app:listen(host, port, options)`
- `app:serve_async(listener, options)`
- `app:listen_async(host, port, options)`

Route paths support named parameters such as `/users/:id`.

## Middleware

Middleware receives `(ctx, next)` and can modify the response before or after
calling `next()`.

```mobius
app:use(func(ctx, next) {
    ctx:set_header("x-request-path", ctx.path)
    return next()
})
```

## Context Helpers

HTTP route handlers receive a `ctx` object.

Request helpers:

- `ctx:param(name)`
- `ctx:query(name, default_value)`
- `ctx:header(name, default_value)`
- `ctx:form_body()`
- `ctx:json_body()`
- `ctx:text_body()`

Response helpers:

- `ctx:status(code)`
- `ctx:set_header(name, value)`
- `ctx:send(response_table)`
- `ctx:text(body)`
- `ctx:html(body)`
- `ctx:json(value)`
- `ctx:redirect(location)`

`ctx:status(...)` and `ctx:set_header(...)` compose with the later body helper:

```mobius
return ctx:status(201):json({ok: true})
```

Useful context fields:

- `ctx.method`
- `ctx.path`
- `ctx.target`
- `ctx.headers`
- `ctx.body`
- `ctx.remote_addr`

## WebSocket Routes

Register a websocket endpoint with `app:websocket(path, handler)`. The handler
receives `(ws, ctx)`.

WebSocket session methods:

- `ws:send_text(text)`
- `ws:send_json(value)`
- `ws:recv()`
- `ws:recv_text()`
- `ws:recv_json()`
- `ws:close(code, reason)`
- `ws:is_closed()`

Example:

```mobius
app:websocket("/ws/:room", func(ws, ctx) {
    var msg = ws:recv_text()
    ws:send_text("room=" + ctx:param("room") + ":" + msg)
    ws:close(1000, "bye")
})
```

## Server Options

`app:serve(...)` and `app:listen(...)` accept an optional `options` table.

Common fields:

- `worker_count`: number of worker fibers consuming accepted connections
- `channel_capacity`: buffered connection queue size
- `max_connections`: stop after handling N accepted connections
- `timeout_ms`: socket timeout applied to each accepted connection
- `backlog`: only for `app:listen(...)`, passed to `socket.listen(...)`

`max_connections` is especially useful for tests or one-shot example runs.
