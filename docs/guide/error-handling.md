# Error Handling

[← Documentation home](../index.md) · [Guide: Functions](functions.md)

Mobius has exception-style error handling with `throw`, `try`, `catch`, and an
optional `finally`. Any value can be thrown, and the thrown value is bound in the
`catch` clause.

---

## throw

`throw` raises an exception with any value — a string, number, table, or array:

```mobius
func parse_port(s) {
    var n = int(s)
    if (n < 1 or n > 65535) {
        throw "port out of range: " + s
    }
    return n
}
```

## try / catch / finally

A `try` block must be followed by a `catch` clause that names a variable to hold
the thrown value. A `finally` block is optional and always runs — whether the
`try` completed normally or an exception was caught.

```mobius
try {
    var port = parse_port("99999")
} catch err {
    print("failed:", err)         // err holds the thrown value
} finally {
    print("done")                 // always runs
}
```

The caught value keeps the type it was thrown with:

```mobius
try { throw 42 }      catch e { print(typeof(e)) }   // int64
try { throw "msg" }   catch e { print(typeof(e)) }   // string
try { throw [1, 2] }  catch e { print(typeof(e)) }   // array
```

### Ordering

`try` runs first; if it throws, control transfers to `catch`; then `finally`
runs last in all cases:

```mobius
var order = []
try {
    order:push("try")
    throw "boom"
} catch err {
    order:push("catch")
} finally {
    order:push("finally")
}
print(order)    // ["try", "catch", "finally"]
```

### Nesting and re-throwing

`try`/`catch` blocks nest, and a `catch` may `throw` again to propagate (or wrap)
an error to an outer handler:

```mobius
try {
    try {
        throw "inner"
    } catch e {
        throw "outer: " + e       // re-throw, wrapped
    }
} catch e {
    print(e)                      // "outer: inner"
}
```

### In loops

A `try`/`catch` inside a loop contains the error to a single iteration:

```mobius
var failures = 0
for (var i = 0; i < 10; i++) {
    try {
        if (i % 3 == 0) { throw "skip " + str(i) }
        // ... work ...
    } catch e {
        failures++
    }
}
```

---

## Errors from modules

Many module functions throw on failure (for example, the `sqlite` module throws
on SQL errors, and argument validators throw type errors). Wrap calls that can
fail in `try`/`catch` when you want to recover:

```mobius
import "json"

try {
    var data = json.parse(untrusted_input)
} catch e {
    print("invalid JSON:", e)
}
```

## Exiting the program

To stop the whole script immediately (rather than raising a catchable
exception), call [`exit([code])`](../reference/standard-library.md#exitcode):

```mobius
if (!file_exists(path)) {
    print("missing config:", path)
    exit(1)
}
```

---

Next: [Collections](collections.md).
