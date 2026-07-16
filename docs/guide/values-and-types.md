# Values and Types

Mobius is dynamically evaluated but **statically type-locked**: a variable's type
is inferred from its first non-nil assignment and then cannot change. This is the
language's defining feature — it keeps scripts simple to write while letting the
compiler emit type-specialized opcodes.

[← Documentation home](../index.md) · [Guide: Language Tour](language-tour.md)

---

## Declaring variables

Variables are declared with `var`. An initializer is optional and defaults to
`nil`:

```mobius
var count = 0
var name = "Alice"
var pending             // nil until first assigned
```

## Type locking

A variable's type locks at its **first non-nil assignment**, and the type is
inferred — there is no type syntax required.

```mobius
var x = 42          // x is locked to int64
var s = "hello"     // s is locked to string
var t = {}          // t is locked to table

x = 99              // OK — integer to integer
x = nil             // OK — nil is always valid
```

Violating a lock rejects the script at compile time:

```mobius
var x = 42
x = "oops"
```

```text
Compile error [script.mob:2]: cannot assign string to variable 'x' locked to int64
Error: Bytecode compilation failed
```

The rules:

- The type is inferred from the initializer expression. Literals, arithmetic
  results, array/table literals, and function return values (when the function
  is defined before the call site) are all inferred at compile time.
- **`nil` is valid for any variable** regardless of its locked type. A variable
  locked to `int64` may hold an integer or `nil` (meaning "no value").
- `var x = nil` and `var x` (no initializer) leave the type undetermined; it
  locks on the first non-nil assignment.
- All non-nil `return` paths in a function must agree on a single type — returning
  different non-nil types from different branches is a compile error.

```mobius
var v = nil
v = 42              // v is now locked to int64
v = 99              // OK
v = nil             // OK

func find(arr, target) {
    for (var i = 0; i < arr:size(); i++) {
        if (arr[i] == target) { return i }   // int64
    }
    return nil                               // OK — nil is compatible
}
```

(After `v = 42` above, a later `v = "hello"` would be the same compile error
shown earlier — deferred locking is still locking.)

Enforcement happens as early as possible: when the compiler can prove both
the variable's locked type and the assigned expression's type, a violation
is a **compile error** and the script never runs. When a type can only be
known at runtime (say, a value read from a table), the lock is enforced at
the assignment with a runtime error instead — catchable with `try`/`catch`.

Type locking is what lets `a + b` compile to a fast integer-only add when both
operands are known integers, with no runtime type dispatch.

## Conversions

Because variables are type-locked, convert by storing the result in a **new**
variable rather than reassigning:

```mobius
var n = 42
var ns = str(n)      // "42"   — new string variable
var nf = float(n)    // 42.0   — new float variable

var y = 3.14
var yi = int(y)      // 3      — truncates toward zero
```

The built-in conversion functions are [`str`](../reference/standard-library.md#strvalue---string),
[`int`](../reference/standard-library.md#intvalue---int64), and
[`float`](../reference/standard-library.md#floatvalue---float64). Inspect a value's
type at runtime with [`typeof`](../reference/standard-library.md#typeofvalue---string):

```mobius
typeof(42)      // "int64"
typeof(3.14)    // "float64"
typeof("hi")    // "string"
typeof(true)    // "bool"
typeof(nil)     // "nil"
typeof([1, 2])  // "array"
typeof({a: 1})  // "table"
```

## Literals

**Integers** — decimal, hexadecimal (`0x`), and binary (`0b`):

```mobius
var dec = 255
var hex = 0xFF
var bin = 0b11111111
```

Integers are stored as 64-bit values. A literal larger than `INT64_MAX` is
stored as `uint64`.

**Floats** — decimal and scientific notation:

```mobius
var f   = 3.14
var g   = 0.5
var avo = 6.022e23
var tiny = 1.6e-19
```

> Numeric literals do **not** allow `_` digit separators.

**Strings** — double-quoted, with escape sequences:

```mobius
var s = "Hello\tWorld\n"
// escapes: \n  \t  \r  \\  \"  \'  \0
```

**Triple-quoted strings** preserve embedded newlines and allow unescaped `"`
inside the body (escape sequences are still processed):

```mobius
var html = """<!doctype html>
<html>
  <body>
    <h1>"quoted" heading</h1>
  </body>
</html>"""
```

**Characters** use single quotes; **booleans** are `true`/`false`; **nil** is
the absence of a value:

```mobius
var ch = 'A'
var yes = true
var nothing = nil
```

## Type annotations

Annotations are optional everywhere. You can state a type explicitly on
variables, function parameters, and return types:

```mobius
var age: int64 = 25
var pi: float64 = 3.14159

func clamp(v: int64, lo: int64, hi: int64): int64 {
    if (v < lo) { return lo }
    if (v > hi) { return hi }
    return v
}
```

Each position behaves a little differently:

- **Variables** accept the numeric annotations and validate the value at the
  declaration: a float assigned to an `int64` variable converts (`var x:
  int64 = 3.5` stores `3`), a non-numeric value is a runtime error, and
  `#pragma strict_types true` turns the conversions into errors too (see
  [Pragmas](#pragmas)).
- **Parameters** accept the full set of type names and type the parameter
  throughout the body — the compiler trusts the annotation, so the body
  compiles to type-specialized instructions. Calls to a named function are
  checked at compile time: a provably wrong-typed argument is a compile
  error. Calls through function *values* (callbacks, table fields) cannot
  be checked and are trusted as annotated — pass the right type.
- **Return types** declare what every `return` must produce. A mismatched
  return is a compile error; returning `nil` is always allowed.

```text
Compile error [script.mob:2]: argument 1 of 'clamp' is string, but the parameter is annotated int64
```

Beyond documentation value, annotations help the compiler generate optimal
bytecode where types can't be inferred — chiefly parameters, whose types
depend on the caller. Annotating the standard benchmark's recursive `fib`
took it from 1.8× to 1.2× of Lua's speed.

See also [Functions](functions.md#type-annotations).

### Type names

| Context                       | Accepted names |
|-------------------------------|----------------|
| Variable annotation (`var x:`)| `int64`, `uint64`, `float64` |
| Function param/return         | `int64`, `uint64`, `float64`, `bool`, `boolean`, `string`, `array`, `table`, `buffer`, `function`, `nil`, `userdata` |
| `switch … case is <type>`     | `int64`, `uint64`, `float64`, `bool`, `boolean`, `nil`, `string`, `array`, `table`, `function` |

The numeric type names are spelled the same everywhere — `int64`, `uint64`,
`float64`. `typeof` reports runtime categories as strings: `"int64"` for whole
numbers up to `INT64_MAX`, `"uint64"` for larger whole numbers, `"float64"` for
floating-point values, plus `"string"`, `"bool"`, `"char"`, `"nil"`, `"array"`,
`"table"`, `"function"`, and `"enum"`. (These match the annotation
keywords `int64`/`uint64`/`float64`.)

## Pragmas

Pragmas adjust interpreter behavior from within a script. They are processed at
parse time and apply to the rest of the file (or until changed).

```mobius
#pragma strict_types true        // enforce type annotations at runtime
#pragma strict_types false       // disable (default)

#pragma type_warnings true       // warn on implicit conversions
#pragma type_warnings false

#pragma override_behavior error  // reject overriding read-only globals (default)
#pragma override_behavior warn   // allow, but print a warning per override
#pragma override_behavior quiet  // allow silently
```

### `strict_types`

```mobius
#pragma strict_types true

var x: int64 = 42        // ok
var y: int64 = "hello"   // runtime error: Cannot convert string to int64
```

### `override_behavior`

Function names — your own and the builtins — are **read-only** once
defined. By default, redefining one is an error, which catches accidents
like a top-level `var size = ...` clobbering the builtin `size`:

```text
Error [script.mob:2:0]: Cannot assign to read-only variable 'print'
```

A chunk that *means* to override declares it up front. The pragma is
consumed at compile time and applies to the chunk it appears in:

```mobius
#pragma override_behavior quiet   // or warn, to log each override

func print(msg) {
    // intercept all print output — every caller sees this version,
    // including code defined before the override ran
}
```

In a permissive chunk, function definitions also stay overridable (they
skip the read-only marking), and the chunk's calls dispatch through the
global slot — slightly slower, confined to the chunk that opted in. The
REPL compiles every line in `quiet` mode, so redefining a function
mid-session just works.

Two limits: an override must keep the variable's locked type (`print` must
stay a function), and the four intrinsified builtins — `size`, `str`,
`concat`, `array_push` — cannot be overridden at all (their call sites
compile to dedicated instructions); attempting it is a compile error.

Embedders can set the default mode for all chunks with
`MobiusConfig.override_behavior`.

The current configuration can be inspected at runtime with
[`get_type_config()`](../reference/standard-library.md#get_type_config---table).
Use pragmas (not function calls) to change the settings.

---

Next: [Control Flow](control-flow.md).
