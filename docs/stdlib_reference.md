# Mobius Standard Library Reference

These functions are available in every Mobius script after the host
application calls `mobius_init_stdlib()`. No `import` statement is needed
(except for the `fiber` module — see below).

Mobius uses a **dual-syntax** convention inspired by Lua:

- **`.`** (dot) accesses fields, module namespaces, and table keys — no
  implicit `self` is passed.
- **`:`** (colon) calls a method, implicitly passing the object as the
  first argument (`self`).

Array, table, and channel operations use the `:` method syntax. Global
utility functions like `len`, `setmetatable`, and `array_create` remain
as plain function calls.

For namespaced module APIs (such as `math.sin(...)`, `json.parse(...)`, or
`os.join(...)`), see the [Module Reference](modules/index.md).

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
9. [Module Reference](#module-reference)

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

Arrays support both global functions and method syntax using `:`.

### Global: array_create(capacity [, fill_value]) -> array

Create a new array with the given capacity. If `fill_value` is provided,
the array is pre-filled with that many copies of the value.

```mobius
var arr = array_create(100)           // empty array, capacity for 100
var grid = array_create(10, 0)        // [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
```

### arr:push(value)

Append a value to the end of an array.

```mobius
var arr = []
arr:push(10)
arr:push(20)
// arr is now [10, 20]
```

### arr:pop() -> value

Remove and return the last element. Returns `nil` if the array is empty.

```mobius
var arr = [1, 2, 3]
var last = arr:pop()    // 3; arr is now [1, 2]
```

### arr:get(index) -> value

Return the element at the given zero-based index, or `nil` if out of bounds.

```mobius
var arr = [10, 20, 30]
arr:get(1)    // 20
```

Note: You can also use bracket syntax: `arr[1]`.

### arr:set(index, value)

Set the element at the given zero-based index. Errors if out of bounds.

```mobius
var arr = [10, 20, 30]
arr:set(1, 99)    // arr is now [10, 99, 30]
```

Note: You can also use bracket assignment: `arr[1] = 99`.

### arr:length() -> integer

Return the number of elements in the array.

```mobius
[1, 2, 3, 4]:length()    // 4
[]:length()               // 0
```

### arr:slice(start, end) -> array

Return a new array containing elements from `start` (inclusive) to `end`
(exclusive).

```mobius
var arr = [1, 2, 3, 4, 5]
arr:slice(1, 4)    // [2, 3, 4]
arr:slice(0, 2)    // [1, 2]
```

### arr:concat(other, ...) -> array

Return a new array that is the concatenation of `arr` with one or more
other arrays.

```mobius
[1, 2]:concat([3, 4])    // [1, 2, 3, 4]
```

### arr:reverse() -> array

Reverse the array in-place and return it.

```mobius
var arr = [1, 2, 3]
arr:reverse()    // arr is now [3, 2, 1]
```

### arr:find(value) -> integer

Return the index of the first occurrence of `value`, or `-1` if not found.

```mobius
[10, 20, 30]:find(20)    // 1
[10, 20, 30]:find(99)    // -1
```

### arr:sort([comparator]) -> array

Sort the array in-place and return it. Without a comparator, uses default
ordering (numeric or string). With a comparator function, calls it with
two elements and expects a truthy return to indicate the first should come
before the second.

```mobius
var nums = [3, 1, 4, 1, 5]
nums:sort()                                      // [1, 1, 3, 4, 5]
nums:sort(func(a, b) { return a > b; })          // [5, 4, 3, 1, 1]
```

### arr:map(fn) -> array

Apply `fn(element, index)` to each element and return a new array of results.

```mobius
[1, 2, 3]:map(func(x, i) { return x * 2; })    // [2, 4, 6]
```

### arr:filter(fn) -> array

Return a new array containing only elements for which `fn(element, index)`
returns a truthy value.

```mobius
[1, 2, 3, 4, 5]:filter(func(x, i) { return x % 2 == 0; })    // [2, 4]
```

### arr:reduce(fn, initial) -> value

Fold the array to a single value by applying `fn(accumulator, element)` for
each element, starting with `initial`.

```mobius
[1, 2, 3, 4]:reduce(func(acc, x) { return acc + x; }, 0)    // 10
```

### arr:foreach(fn)

Call `fn(element, index)` for each element. Returns nothing (side effects
only).

```mobius
["a", "b", "c"]:foreach(func(x, i) {
    print(i, ":", x)
})
```

### arr:any(fn) -> bool

Return `true` if `fn(element)` returns a truthy value for at least one
element.

```mobius
[1, 2, 100, 3]:any(func(x) { return x > 50; })    // true
```

### arr:all(fn) -> bool

Return `true` if `fn(element)` returns a truthy value for every element.

```mobius
[2, 4, 6]:all(func(x) { return x % 2 == 0; })    // true
```

---

## Table Functions

Tables support both global functions and method syntax using `:`.

### tbl:remove(key)

Remove a key and its associated value from a table.

```mobius
var t = {name: "Alice", age: 30}
t:remove("age")
print(t:has_key("age"))    // false
```

### tbl:has_key(key) -> bool

Return `true` if the table contains the given key.

```mobius
var t = {name: "Alice"}
t:has_key("name")    // true
t:has_key("age")     // false
```

### tbl:size() -> integer

Return the number of key-value pairs in the table.

```mobius
{a: 1, b: 2, c: 3}:size()    // 3
{}:size()                     // 0
```

### tbl:pairs() -> array

Return an array of `[key, value]` pairs from the table. Useful for iteration.

```mobius
var t = {name: "Alice", age: 30}
var p = t:pairs()
for (var i = 0; i < len(p); i++) {
    print("Key:", p[i][0], "Value:", p[i][1])
}
```

### Global: setmetatable(table, metatable)

Set the metatable for a table. The metatable controls fallback behavior for
missing keys (`__index`) and operator overloading (`__add`, `__sub`, etc.).

```mobius
var defaults = {color: "blue"}
var meta = {["__index"] = defaults}
var obj = {}
setmetatable(obj, meta)
print(obj.color)    // "blue" (from defaults via __index)
```

### Global: getmetatable(table) -> table | nil

Return the metatable of a table, or `nil` if none is set.

```mobius
var mt = getmetatable(obj)
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

### clock() -> int64

Return a monotonic timestamp in nanoseconds. Subtract two `clock()` readings to
measure elapsed time, then divide by `1_000_000_000.0` to convert nanoseconds
to seconds.

```mobius
func ns_to_sec(ns) {
    return float(ns) / 1000000000.0
}

var start = clock()
// ... do some work ...
var elapsed = ns_to_sec(clock() - start)
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

These functions and keywords work with Mobius fibers, futures, channels, shared
containers, and array slices. See the [Language Reference](language_reference.md#concurrency)
for an overview of the concurrency model.

Channels are accessed via the `fiber` module and support method syntax using `:`.

### Creating Channels

```mobius
import "fiber"
var ch = fiber.channel(10)    // bounded channel with capacity 10
```

### ch:send(value)

Sends a value into the channel. Blocks if the channel is full until space is available. Returns `false` if the channel is closed.

```mobius
ch:send(42)
```

### ch:recv() -> value

Receives a value from the channel. Blocks if the channel is empty until a value
is available. Raises `ChannelClosedError` if the channel is closed and empty.

```mobius
var msg = ch:recv()
```

### ch:try_send(value) -> bool

Non-blocking send. Returns `true` if the value was enqueued, `false` if the channel is full or closed.

```mobius
if (ch:try_send(42)) {
    print("sent")
}
```

### ch:try_recv() -> value

Non-blocking receive. Returns the value if one is available, or `nil` if the channel is empty.

```mobius
var msg = ch:try_recv()
```

### ch:close()

Closes the channel. Subsequent sends return `false`. Pending receivers are unblocked. Remaining buffered values can still be received.

```mobius
ch:close()
```

### shared var

Declares a variable that is safe for concurrent access by multiple fibers. The
binding is wrapped in a synchronized shared cell, and the `shared` keyword must
prefix `var`.

```mobius
shared var total = 0
shared var counter = [0]
shared var config = { debug: false }
```

Reads and writes through the shared variable are synchronized. This applies to
scalars, arrays, and tables. Sharing is attached to the binding, not
recursively to every nested mutable value reachable through it.

### atomic(expression)

Executes a compound read-modify-write expression atomically on a shared
value. The shared cell lock is held across the entire expression,
preventing lost updates from concurrent fibers.

```mobius
shared var counter = [0]
atomic(counter[0] = counter[0] + 1)

shared var stats = { hits: 0 }
atomic(stats["hits"] = stats["hits"] + 1)

shared var total = 0
atomic(total = total + 1)
```

Without `atomic()`, `counter[0] = counter[0] + 1` compiles to separate read
and write instructions — two fibers can read the same value, both increment,
and one update is lost. `atomic()` prevents this.

**Rules:**
- Shared scalar `++`, `--`, `+=`, and `-=` are already atomic.
- The expression inside `atomic()` must operate on a shared variable or a
  shared array/table element.
- Using `atomic()` on a non-shared variable is a runtime error.
- Nested `atomic()` calls on the same target do not deadlock (recursive
  mutex).

### fiber.cancel(future)

Requests cancellation of the fiber associated with the given future. The fiber will throw a `CancellationError` at its next cancellation check point (loop back-edges, yield points).

```mobius
var f = spawn long_task()
fiber.cancel(f)
```

### fiber.all(futures)

Waits for all futures in the given array to resolve. Returns an array of results in the same order. If any future rejects with an error, the error is propagated.

```mobius
var results = fiber.all([spawn a(), spawn b(), spawn c()])
// results == [a_result, b_result, c_result]
```

### fiber.any(futures)

Waits for the first future in the array to resolve and returns its result. If a future errors, it is skipped (unless all futures error).

```mobius
var fastest = fiber.any([spawn route_a(), spawn route_b()])
```

### fiber.sleep(milliseconds)

Suspends the current fiber for at least the given number of milliseconds. Other fibers can execute during this time.

```mobius
fiber.sleep(100)  // sleep for ~100ms
```

### fiber.slice(array, start, length)

Creates a lightweight array slice (a view into the parent array). Reads and writes through the slice pass through to the underlying array. Useful for dividing work among fibers.

```mobius
shared var data = [1, 2, 3, 4, 5, 6]
var first_half = fiber.slice(data, 0, 3)
var second_half = fiber.slice(data, 3, 3)
print(first_half[0])  // 1
first_half[0] = 99
print(data[0])        // 99 (write-through)
```

Slices are always views, even when the source array is not shared. While any
slice is alive, structural mutations that resize the parent array fail at
runtime. If the parent array is shared, slice access synchronizes through that
shared parent.

**Parameters:**
- `array` — The source array.
- `start` — Zero-based starting index.
- `length` — Number of elements in the slice.

---

## Module Reference

Namespaced modules live under [`docs/modules`](modules/index.md):

- [`crypto`](modules/crypto.md)
- [`datetime`](modules/datetime.md)
- [`fiber`](modules/fiber.md)
- [`json`](modules/json.md)
- [`math`](modules/math.md)
- [`os`](modules/os.md)
- [`regex`](modules/regex.md)
- [`toml`](modules/toml.md)
- [`url`](modules/url.md)
- [`yaml`](modules/yaml.md)
