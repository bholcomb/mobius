# Language Tour

A fast, example-driven tour of Mobius. Every section links to a deeper page.
If you have written Lua, C, or JavaScript, most of this will feel familiar.

[← Documentation home](../index.md)

---

## Comments and statements

```mobius
// single-line comment
/* multi-line
   comment */

var x = 10      // newline terminates a statement
var y = 20;     // semicolons are optional
```

## Variables and type locking

`var` declares a variable. Its type is **locked** to the type of its first
non-nil value and cannot change afterward — but `nil` is always allowed.

```mobius
var count = 0           // locked to integer
count = 99              // OK
count = nil             // OK — nil is valid for any variable
// count = "oops"       // ERROR: cannot assign string to integer variable

var name = "Alice"      // locked to string
var ratio = 3.14        // locked to float
```

Convert by storing the result in a **new** variable:

```mobius
var n = 42
var s = str(n)          // "42"
var f = float(n)        // 42.0
```

See [Values and Types](values-and-types.md).

## Literals

```mobius
var dec = 255
var hex = 0xFF
var bin = 0b11111111
var pi  = 3.14
var avo = 6.022e23      // scientific notation
var ch  = 'A'
var yes = true
var s   = "tab\tnewline\n"
var doc = """multi-line
"quotes" allowed here"""
```

## Operators

```mobius
15 / 4        // 3.75   (division produces a float)
15 % 4        // 3
pow(2, 8)     // 256    (exponent is the builtin pow, not an operator)
0xF0 | 0x0F   // bitwise: & | ^ ~ << >>
a and b       // also: a && b
not a         // also: !a
"Count: " + 42   // "Count: 42"  (+ concatenates if either side is a string)
```

`++` and `--` work in both prefix and postfix form. See
[full precedence table](../reference/grammar.md#operator-precedence-low-to-high).

## Control flow

```mobius
if (x > 0) {
    print("positive")
} elif (x == 0) {
    print("zero")
} else {
    print("negative")
}

var i = 0
while (i < 3) { print(i); i++ }

for (var j = 0; j < 3; j++) {
    if (j == 1) continue
    print(j)
}
```

### `switch` with pattern matching

`switch` does far more than match constants — it matches ranges, comparisons,
runtime types, and enum members, and cases fall through (use `break` to stop):

```mobius
switch (score) {
    case 90..100:      grade = "A"; break
    case is string:    grade = "?"; break
    case >= 60:        grade = "pass"; break
    case 0, 1, 2:      grade = "low"; break
    default:           grade = "F"
}
```

See [Control Flow](control-flow.md).

## Functions

```mobius
func add(a, b) {
    return a + b
}

// Optional type annotations on parameters and return value
func fib(n: int64): int64 {
    if (n <= 1) return n
    return fib(n - 1) + fib(n - 2)
}
```

Functions are first-class values and form closures:

```mobius
func make_counter() {
    var count = 0
    func next() { count = count + 1; return count }
    return next
}

var c = make_counter()
print(c(), c(), c())    // 1 2 3
```

See [Functions](functions.md).

## Arrays

Zero-indexed, with a rich method set called using `:` syntax:

```mobius
var nums = [3, 1, 4, 1, 5]
nums:push(9)
nums:sort()
print(nums:size())                                  // 6
print(nums:filter(func(x, i) { return x > 2; }))      // [3, 4, 5, 9]
print(nums:map(func(x, i) { return x * 10; }))        // [...]
```

## Tables

Key-value containers with optional metatables for inheritance and operator
overloading:

```mobius
var person = { name: "Alice", age: 30 }
person.city = "Boston"
print(person.name)        // "Alice"
print(person:has_key("age"))   // true
```

See [Collections](collections.md).

## Enums

```mobius
enum Color { RED, GREEN, BLUE }            // 0, 1, 2
enum Flags : uint64 { READ = 1, WRITE = 2, EXEC = 4 }

var c = Color.GREEN
print(c == Color.GREEN)   // true
```

## Binary data

Buffers are fixed byte sequences; `struct` declarations describe layouts over
them:

```mobius
struct Vec2 packed { x: float32; y: float32 }

var buf = buffer_create(Vec2.size)
var v = buf:view_as(Vec2)
v.x = 1.5
v.y = 2.5
```

See [Binary Data](binary-data.md).

## Concurrency

`spawn` runs a function on a fiber and returns a future; `await` waits for it:

```mobius
func work(n) {
    var sum = 0
    for (var i = 0; i < n; i++) { sum += i }
    return sum
}

var f = spawn work(1000)
print(await f)            // 499500
```

Use `shared var`, `atomic(...)`, and channels for safe data sharing. See
[Concurrency](concurrency.md).

## Modules

```mobius
import "math"
import "json" as j

print(math.sqrt(2))
print(j.stringify([1, 2, 3]))
```

See [Modules and Packages](modules-and-packages.md) and the
[Module Reference](../modules/index.md).
