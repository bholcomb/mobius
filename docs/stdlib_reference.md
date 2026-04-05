# Mobius Standard Library Reference

These functions are available in every Mobius script after the host
application calls `mobius_init_stdlib()`. No `import` statement is needed.

For functions provided by **plugins** (such as the `math` plugin's
trigonometric functions), see
[Modules and Imports](language_reference.md#modules-and-imports).

---

## Table of Contents

1. [Core Functions](#core-functions)
2. [Math Functions](#math-functions)
3. [String Functions](#string-functions)
4. [Array Functions](#array-functions)
5. [Table Functions](#table-functions)
6. [Type System Functions](#type-system-functions)
7. [Utility Functions](#utility-functions)
8. [Fiber / Concurrency Functions](#fiber--concurrency-functions)
9. [Math Plugin Functions](#math-plugin-functions)

---

## Core Functions

### print(...)

Print one or more values to standard output, separated by spaces, followed by
a newline. Accepts any number of arguments of any type.

```mobius
print("Hello", "World")       // Hello World
print(42, true, nil)           // 42 true nil
print()                        // (blank line)
```

### typeof(value) -> string

Return the type name of a value as a string.

```mobius
typeof(42)          // "int64"
typeof(3.14)        // "float64"
typeof("hello")     // "string"
typeof(true)        // "bool"
typeof(nil)         // "nil"
typeof([1, 2])      // "array"
typeof({a: 1})      // "table"
```

### str(value) -> string

Convert any value to its string representation.

```mobius
str(42)       // "42"
str(3.14)     // "3.14"
str(true)     // "true"
str(nil)      // "nil"
```

### int(value) -> integer

Convert a value to an integer. Truncates floats toward zero. Parses numeric
strings.

```mobius
int(3.14)     // 3
int("42")     // 42
int(true)     // 1
```

### float(value) -> float

Convert a value to a floating-point number.

```mobius
float(42)       // 42.0
float("3.14")   // 3.14
```

---

## Math Functions

### abs(x) -> number

Return the absolute value of a number.

```mobius
abs(-5)       // 5
abs(3.14)     // 3.14
abs(-3.14)    // 3.14
```

### min(a, b, ...) -> number

Return the smallest of two or more numeric arguments.

```mobius
min(1, 2, 3)       // 1
min(10, -5)        // -5
```

### max(a, b, ...) -> number

Return the largest of two or more numeric arguments.

```mobius
max(1, 2, 3)       // 3
max(10, -5)        // 10
```

### pow(base, exponent) -> number

Return `base` raised to the power of `exponent`.

```mobius
pow(2, 8)     // 256
pow(3, 0)     // 1
pow(2, 0.5)   // 1.41421...
```

### sqrt(x) -> number

Return the square root of a non-negative number.

```mobius
sqrt(16)      // 4
sqrt(2)       // 1.41421...
```

### floor(x) -> number

Round down to the nearest integer (toward negative infinity).

```mobius
floor(3.7)    // 3
floor(-2.3)   // -3
```

### ceil(x) -> number

Round up to the nearest integer (toward positive infinity).

```mobius
ceil(3.2)     // 4
ceil(-2.7)    // -2
```

### round(x) -> number

Round to the nearest integer (half rounds away from zero).

```mobius
round(3.5)    // 4
round(3.4)    // 3
round(-2.5)   // -3
```

---

## String Functions

### len(value) -> integer

Return the length of a string (in bytes) or the number of elements in an
array.

```mobius
len("Hello")       // 5
len("")            // 0
len([1, 2, 3])     // 3
```

### upper(str) -> string

Return a copy with all characters converted to uppercase.

```mobius
upper("hello")     // "HELLO"
upper("Hello!")    // "HELLO!"
```

### lower(str) -> string

Return a copy with all characters converted to lowercase.

```mobius
lower("HELLO")     // "hello"
lower("Hello!")    // "hello!"
```

### substr(str, start, length) -> string

Return a substring starting at `start` (zero-based) with the given `length`.

```mobius
substr("Hello World", 0, 5)     // "Hello"
substr("Hello World", 6, 5)     // "World"
```

### concat(a, b, ...) -> string

Concatenate two or more string arguments.

```mobius
concat("Hello", " ", "World")   // "Hello World"
```

Note: You can also use the `+` operator for string concatenation.

### contains(haystack, needle) -> bool

Return `true` if `haystack` contains `needle`.

```mobius
contains("Hello World", "World")    // true
contains("Hello World", "xyz")      // false
```

---

## Array Functions

### array_create(capacity?) -> array

Create a new empty array. An optional capacity hint can be provided.

```mobius
var arr = array_create()      // empty array
var arr = array_create(100)   // pre-allocated for 100 elements
```

### array_push(arr, value)

Append a value to the end of an array.

```mobius
var arr = []
array_push(arr, 10)
array_push(arr, 20)
// arr is now [10, 20]
```

### array_pop(arr) -> value

Remove and return the last element. Errors if the array is empty.

```mobius
var arr = [1, 2, 3]
var last = array_pop(arr)    // 3; arr is now [1, 2]
```

### array_get(arr, index) -> value

Return the element at the given zero-based index.

```mobius
var arr = [10, 20, 30]
array_get(arr, 1)    // 20
```

Note: You can also use bracket syntax: `arr[1]`.

### array_set(arr, index, value)

Set the element at the given zero-based index.

```mobius
var arr = [10, 20, 30]
array_set(arr, 1, 99)    // arr is now [10, 99, 30]
```

Note: You can also use bracket assignment: `arr[1] = 99`.

### array_length(arr) -> integer

Return the number of elements in the array.

```mobius
array_length([1, 2, 3, 4])    // 4
array_length([])               // 0
```

### array_slice(arr, start, end) -> array

Return a new array containing elements from `start` (inclusive) to `end`
(exclusive).

```mobius
var arr = [1, 2, 3, 4, 5]
array_slice(arr, 1, 4)    // [2, 3, 4]
array_slice(arr, 0, 2)    // [1, 2]
```

### array_concat(a, b) -> array

Return a new array that is the concatenation of `a` and `b`.

```mobius
array_concat([1, 2], [3, 4])    // [1, 2, 3, 4]
```

### array_reverse(arr) -> array

Return a new array with elements in reverse order. The original is unchanged.

```mobius
array_reverse([1, 2, 3])    // [3, 2, 1]
```

### array_find(arr, value) -> integer

Return the index of the first occurrence of `value`, or `-1` if not found.

```mobius
array_find([10, 20, 30], 20)    // 1
array_find([10, 20, 30], 99)    // -1
```

---

## Table Functions

### table_remove(table, key)

Remove a key and its associated value from a table.

```mobius
var t = {name: "Alice", age: 30}
table_remove(t, "age")
print(table_has_key(t, "age"))    // false
```

### table_has_key(table, key) -> bool

Return `true` if the table contains the given key.

```mobius
var t = {name: "Alice"}
table_has_key(t, "name")    // true
table_has_key(t, "age")     // false
```

### table_size(table) -> integer

Return the number of key-value pairs in the table.

```mobius
table_size({a: 1, b: 2, c: 3})    // 3
table_size({})                     // 0
```

### setmetatable(table, metatable)

Set the metatable for a table. The metatable controls fallback behavior for
missing keys (`__index`) and operator overloading (`__add`, `__sub`, etc.).

```mobius
var defaults = {color: "blue"}
var meta = {["__index"] = defaults}
var obj = {}
setmetatable(obj, meta)
print(obj.color)    // "blue" (from defaults via __index)
```

### getmetatable(table) -> table | nil

Return the metatable of a table, or `nil` if none is set.

```mobius
var mt = getmetatable(obj)
```

### pairs(table) -> array

Return an array of `[key, value]` pairs from the table. Useful for iteration.

```mobius
var t = {name: "Alice", age: 30}
var p = pairs(t)
for (var i = 0; i < len(p); i++) {
    print("Key:", p[i][0], "Value:", p[i][1])
}
```

---

## Type System Functions

### get_type_config() -> table

Return a table describing the current type system configuration, including
whether strict types and type warnings are enabled.

```mobius
var config = get_type_config()
print(config)
```

Note: To change type system settings, use `#pragma` directives rather than
function calls. See [Pragmas](language_reference.md#pragmas).

---

## Utility Functions

### random() -> float
### random(max) -> integer
### random(min, max) -> integer

Generate random numbers.

- **No arguments**: returns a float between 0.0 and 1.0.
- **One argument**: returns an integer between 0 and `max - 1`.
- **Two arguments**: returns an integer between `min` and `max` (inclusive).

```mobius
random()         // 0.734...  (float in [0, 1))
random(10)       // 7         (integer in [0, 9])
random(5, 15)    // 11        (integer in [5, 15])
```

### time() -> integer

Return the current time as a Unix timestamp (seconds since epoch).

```mobius
var now = time()
print("Timestamp:", now)
```

### clock() -> float

Return the CPU time used by the process in seconds. Useful for benchmarking.

```mobius
var start = clock()
// ... do some work ...
var elapsed = clock() - start
print("Took", elapsed, "seconds")
```

### load(filename)

Execute another Mobius script file. Globals defined in the loaded file become
available in the current scope.

```mobius
load("helpers.mob")
// functions and variables from helpers.mob are now accessible
```

### id(value) -> integer

Return an internal identity value (memory address) for reference types
(tables, arrays, functions, userdata). Returns 0 for value types. Useful for
checking whether two variables refer to the same object.

```mobius
var a = [1, 2, 3]
var b = a
print(id(a) == id(b))    // true — same array

var c = [1, 2, 3]
print(id(a) == id(c))    // false — different array
```

---

## Fiber / Concurrency Functions

These functions work with Mobius fibers, futures, channels, and array slices. See the [Language Reference](language_reference.md#concurrency) for an overview of the concurrency model.

### fiber_channel(capacity)

Creates a bounded channel with the given capacity (default 1). Channels are used for message-passing between fibers.

```mobius
var ch = fiber_channel(10)
```

**Returns:** A channel value.

### fiber_send(channel, value)

Sends a value into the channel. Blocks if the channel is full until space is available. Returns `false` if the channel is closed.

```mobius
fiber_send(ch, 42)
```

### fiber_recv(channel)

Receives a value from the channel. Blocks if the channel is empty until a value is available. Returns `nil` if the channel is closed and empty.

```mobius
var msg = fiber_recv(ch)
```

### fiber_try_send(channel, value)

Non-blocking send. Returns `true` if the value was enqueued, `false` if the channel is full or closed.

```mobius
if (fiber_try_send(ch, 42)) {
    print("sent")
}
```

### fiber_try_recv(channel)

Non-blocking receive. Returns the value if one is available, or `nil` if the channel is empty.

```mobius
var msg = fiber_try_recv(ch)
```

### fiber_close(channel)

Closes the channel. Subsequent sends return `false`. Pending receivers are unblocked. Remaining buffered values can still be received.

```mobius
fiber_close(ch)
```

### fiber_cancel(future)

Requests cancellation of the fiber associated with the given future. The fiber will throw a `CancellationError` at its next cancellation check point (loop back-edges, yield points).

```mobius
var f = spawn long_task()
fiber_cancel(f)
```

### fiber_all(futures)

Waits for all futures in the given array to resolve. Returns an array of results in the same order. If any future rejects with an error, the error is propagated.

```mobius
var results = fiber_all([spawn a(), spawn b(), spawn c()])
// results == [a_result, b_result, c_result]
```

### fiber_any(futures)

Waits for the first future in the array to resolve and returns its result. If a future errors, it is skipped (unless all futures error).

```mobius
var fastest = fiber_any([spawn route_a(), spawn route_b()])
```

### fiber_sleep(milliseconds)

Suspends the current fiber for at least the given number of milliseconds. Other fibers can execute during this time.

```mobius
fiber_sleep(100)  // sleep for ~100ms
```

### fiber_slice(array, start, length)

Creates a lightweight array slice (a view into the parent array). Reads and writes through the slice pass through to the underlying array. Useful for dividing work among fibers.

```mobius
var data = shared [1, 2, 3, 4, 5, 6]
var first_half = fiber_slice(data, 0, 3)
var second_half = fiber_slice(data, 3, 3)
print(first_half[0])  // 1
first_half[0] = 99
print(data[0])        // 99 (write-through)
```

**Parameters:**
- `array` — The source array (should be `shared` for concurrent access).
- `start` — Zero-based starting index.
- `length` — Number of elements in the slice.

---

## Math Plugin Functions

These additional functions are provided by the built-in `math` plugin module.
They require an import statement:

```mobius
import "math"
```

### Trigonometric

| Function              | Description                     |
|-----------------------|---------------------------------|
| `math.sin(x)`        | Sine (radians)                  |
| `math.cos(x)`        | Cosine (radians)                |
| `math.tan(x)`        | Tangent (radians)               |
| `math.asin(x)`       | Arcsine (x in [-1, 1])         |
| `math.acos(x)`       | Arccosine (x in [-1, 1])       |
| `math.atan(x)`       | Arctangent                      |
| `math.atan2(y, x)`   | Two-argument arctangent         |

### Hyperbolic

| Function              | Description                     |
|-----------------------|---------------------------------|
| `math.sinh(x)`       | Hyperbolic sine                 |
| `math.cosh(x)`       | Hyperbolic cosine               |
| `math.tanh(x)`       | Hyperbolic tangent              |

### Logarithmic and Exponential

| Function              | Description                     |
|-----------------------|---------------------------------|
| `math.log(x)`        | Natural logarithm (x > 0)      |
| `math.log10(x)`      | Base-10 logarithm (x > 0)      |
| `math.exp(x)`        | e raised to the power x        |

### Rounding and Clamping

| Function                   | Description                          |
|----------------------------|--------------------------------------|
| `math.sqrt(x)`             | Square root                          |
| `math.pow(base, exp)`      | Exponentiation                       |
| `math.abs(x)`              | Absolute value                       |
| `math.floor(x)`            | Round down                           |
| `math.ceil(x)`             | Round up                             |
| `math.round(x)`            | Round to nearest                     |
| `math.min(a, b, ...)`      | Minimum of arguments                 |
| `math.max(a, b, ...)`      | Maximum of arguments                 |
| `math.clamp(val, min, max)`| Clamp value to range                 |
| `math.sign(x)`             | Returns -1, 0, or 1                  |

### Conversion

| Function              | Description                                |
|-----------------------|--------------------------------------------|
| `math.deg2rad(deg)`   | Convert degrees to radians                 |
| `math.rad2deg(rad)`   | Convert radians to degrees                 |

### Number Theory

| Function              | Description                                |
|-----------------------|--------------------------------------------|
| `math.factorial(n)`   | Factorial (0 <= n <= 20)                   |
| `math.gcd(a, b)`      | Greatest common divisor                    |
| `math.lcm(a, b)`      | Least common multiple                      |

### Constants

| Function              | Returns                                    |
|-----------------------|--------------------------------------------|
| `math.pi()`           | 3.14159265358979...                        |
| `math.e()`            | 2.71828182845904...                        |

### Random

| Function              | Description                                |
|-----------------------|--------------------------------------------|
| `math.random()`       | Random float in [0, 1)                     |

### Example

```mobius
import "math"

var angle = math.deg2rad(45)
print("sin(45°) =", math.sin(angle))
print("cos(45°) =", math.cos(angle))

print("5! =", math.factorial(5))
print("gcd(12, 8) =", math.gcd(12, 8))

print("π =", math.pi())
print("e =", math.e())

print("clamp(15, 0, 10) =", math.clamp(15, 0, 10))
```
