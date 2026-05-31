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
var x = 42          // x is locked to integer
var s = "hello"     // s is locked to string
var t = {}          // t is locked to table

x = 99              // OK — integer to integer
x = nil             // OK — nil is always valid
// x = "oops"       // ERROR: cannot assign string to a variable of type integer
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
v = 42              // v is now locked to integer
v = 99              // OK
v = nil             // OK
// v = "hello"      // ERROR: variable is locked to integer

func find(arr, target) {
    for (var i = 0; i < arr:size(); i++) {
        if (arr[i] == target) { return i }   // int64
    }
    return nil                               // OK — nil is compatible
}
```

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

Variables may be annotated with a numeric type: **`int64`**, **`uint64`**, or
**`float64`**. Annotated variables are type-locked to the annotated type:

```mobius
var age: int64 = 25
var count: uint64 = 0
var pi: float64 = 3.14159
```

Under `#pragma strict_types true`, the interpreter enforces these annotations at
runtime (see [Pragmas](#pragmas)). Otherwise they document intent and aid type
specialization.

Function parameters and return values accept a wider set of annotation names —
see [Functions](functions.md#type-annotations).

### Type names

| Context                       | Accepted names |
|-------------------------------|----------------|
| Variable annotation (`var x:`)| `int64`, `uint64`, `float64` |
| Function param/return         | `int64`, `uint64`, `float64`, `bool`, `boolean`, `string`, `array`, `table`, `function`, `nil`, `userdata`, `enum` |
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

#pragma override_behavior error  // error on function/global redefinition (default)
#pragma override_behavior warn   // warn but allow
#pragma override_behavior quiet  // silently allow
```

### `strict_types`

```mobius
#pragma strict_types true

var x: int64 = 42        // ok
var y: int64 = "hello"   // runtime error: type mismatch
```

### `override_behavior`

```mobius
#pragma override_behavior warn

func greet() { print("hello") }
func greet() { print("hi") }     // warning printed, but allowed
```

The current configuration can be inspected at runtime with
[`get_type_config()`](../reference/standard-library.md#get_type_config---table).
Use pragmas (not function calls) to change the settings.

---

Next: [Control Flow](control-flow.md).
