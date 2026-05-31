# Standard Library Reference

These functions are **global** — available in every script once the host calls
`mobius_init_stdlib()` (the `bin/mobius` CLI does this for you). No `import` is
needed.

Methods on arrays, tables, buffers, and channels are documented with their types
in the guide:
[Arrays](../guide/collections.md#array-methods) ·
[Tables](../guide/collections.md#table-methods) ·
[Buffers](../guide/binary-data.md#buffer-methods) ·
[Channels](../guide/concurrency.md#channels).
Namespaced module functions (`math.sin`, `json.parse`, `os.join`, …) are in the
[Module Reference](../modules/index.md).

[← Documentation home](../index.md)

---

## Contents

- [Globals](#globals) — `argv`
- [Core](#core)
- [Math](#math)
- [Strings](#strings)
- [Files](#files)
- [Containers](#containers)
- [Metatables](#metatables)
- [Type system](#type-system)
- [Utility](#utility)

---

## Globals

### argv

A read-only array of the positional arguments passed to the script after its
filename on the command line.

```bash
bin/mobius app.mob alpha "two words" --flag
```

```mobius
print(argv[0])    // "alpha"
print(size(argv))  // 3
```

---

## Core

### print(...)

Print the arguments to standard output, separated by spaces, followed by a
newline. Accepts any number of values of any type.

```mobius
print("Hello", "World")    // Hello World
print(42, true, nil)       // 42 true nil
print()                    // (blank line)
```

### typeof(value) -> string

Return the runtime type name: `"int64"`, `"uint64"`, `"float64"`, `"string"`,
`"bool"`, `"char"`, `"nil"`, `"array"`, `"table"`, `"function"`, or `"enum"`.
These are the same names used in type annotations and `case is` patterns.

```mobius
typeof(42)        // "int64"
typeof(3.14)      // "float64"
typeof("hi")      // "string"
typeof([1, 2])    // "array"
```

### str(value) -> string

Convert any value to its string representation.

```mobius
str(42)      // "42"
str(3.14)    // "3.14"
str(true)    // "true"
str(nil)     // "nil"
```

### int(value) -> int64

Convert to an integer. Truncates floats toward zero; parses numeric strings.

```mobius
int(3.9)     // 3
int("42")    // 42
int(true)    // 1
```

### float(value) -> float64

Convert to a floating-point number.

```mobius
float(42)      // 42.0
float("3.14")  // 3.14
```

### exit([code])

Stop the script immediately with an optional integer exit code (default `0`).
Unlike [`throw`](../guide/error-handling.md), this is not catchable.

```mobius
if (!ok) { exit(1) }
```

---

## Math

Core numeric helpers are global; the [`math` module](../modules/math.md) adds
trigonometry, logarithms, and more.

| Function          | Returns | Description                                            |
|-------------------|---------|--------------------------------------------------------|
| `abs(x)`          | number  | Absolute value; preserves int/float type of `x`.       |
| `min(a, b, …)`    | number  | Smallest of two or more arguments.                     |
| `max(a, b, …)`    | number  | Largest of two or more arguments.                      |
| `pow(base, exp)`  | float   | `base` raised to `exp`.                                |
| `sqrt(x)`         | float   | Square root.                                           |
| `floor(x)`        | float   | Round down toward negative infinity (e.g. `3.0`).      |
| `ceil(x)`         | float   | Round up toward positive infinity.                     |
| `round(x)`        | float   | Round to nearest; halves round away from zero.         |
| `isnan(x)`        | bool    | `true` if `x` is NaN.                                  |
| `isinf(x)`        | bool    | `true` if `x` is infinite.                             |
| `isfinite(x)`     | bool    | `true` if `x` is finite (not NaN or infinite).         |

```mobius
abs(-5)        // 5      (integer)
abs(-3.0)      // 3.0    (float)
pow(2, 8)      // 256
floor(3.7)     // 3.0
round(3.5)     // 4.0
min(3, 1, 2)   // 1
```

---

## Strings

| Function                       | Returns | Description                                          |
|--------------------------------|---------|------------------------------------------------------|
| `size(s)`                      | integer | Number of bytes in a string (also works on arrays, tables, and buffers). |
| `upper(s)`                     | string  | Uppercase copy.                                      |
| `lower(s)`                     | string  | Lowercase copy.                                      |
| `substr(s, start, length)`     | string  | Substring from zero-based `start` of `length` bytes. |
| `concat(a, b, …)`              | string  | Concatenate two or more strings.                     |
| `contains(haystack, needle)`   | bool    | Whether `needle` occurs in `haystack`.               |
| `find(haystack, needle)`       | integer | Index of first occurrence, or `-1`.                  |
| `startswith(s, prefix)`        | bool    | Whether `s` begins with `prefix`.                    |
| `endswith(s, suffix)`          | bool    | Whether `s` ends with `suffix`.                      |
| `split(s, delimiter)`          | array   | Split `s` on `delimiter` into an array of strings.   |
| `join(array, separator)`       | string  | Join array elements with `separator`.                |
| `trim(s)`                      | string  | Trim whitespace from both ends.                      |
| `replace(s, old, new)`         | string  | Replace **all** occurrences of `old` with `new`.     |
| `repeat(s, n)`                 | string  | `s` repeated `n` times.                              |

```mobius
substr("Hello World", 0, 5)        // "Hello"
find("hello", "ll")                // 2
split("a,b,c", ",")                // ["a", "b", "c"]
join(["a", "b", "c"], "-")         // "a-b-c"
replace("aXaXa", "X", "_")         // "a_a_a"
repeat("ab", 3)                    // "ababab"
```

The `+` operator also concatenates when either operand is a string:
`"Count: " + 42` → `"Count: 42"`.

---

## Files

Simple, blocking file helpers for text and line-oriented I/O. For richer
filesystem and path operations, see the [`os` module](../modules/os.md).

| Function                   | Returns | Description                                            |
|----------------------------|---------|--------------------------------------------------------|
| `readfile(path)`           | string  | Read an entire file as a string.                       |
| `readlines(path)`          | array   | Read a file into an array of lines.                    |
| `writefile(path, content)` | bool    | Write `content` to a file, overwriting it.             |
| `appendfile(path, content)`| bool    | Append `content` to a file.                            |
| `file_exists(path)`        | bool    | Whether a file exists at `path`.                       |

```mobius
if (file_exists("notes.txt")) {
    for (var line in readlines("notes.txt")) {
        print(line)
    }
}
writefile("out.txt", "hello\n")
appendfile("out.txt", "world\n")
```

---

## Containers

These globals construct containers; their methods are documented in the guide.

### array_create(capacity [, fill_value]) -> array

Create an array with capacity for `capacity` elements. With `fill_value`, the
array is pre-filled with that many copies.

```mobius
var a = array_create(100)      // empty, room for 100
var z = array_create(10, 0)    // [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
```

See [Array methods](../guide/collections.md#array-methods).

### buffer_create(size [, fill_byte]) -> buffer

Create a mutable byte buffer of `size` bytes, optionally filled with `fill_byte`
(`0`–`255`).

### buffer_from_string(s) -> buffer

Copy a string's raw bytes into a new buffer.

```mobius
var buf = buffer_create(4, 255)        // [255, 255, 255, 255]
var msg = buffer_from_string("PING")
```

See [Buffer methods](../guide/binary-data.md#buffer-methods).

---

## Metatables

### setmetatable(table, metatable)

Set a table's metatable, controlling missing-key lookup (`__index`) and operator
overloading (`__add`, etc.). See [Metatables](../guide/collections.md#metatables).

### getmetatable(table) -> table | nil

Return a table's metatable, or `nil`.

---

## Type system

### get_type_config() -> table

Return a table describing the current type configuration (whether strict types
and type warnings are enabled). To **change** these settings, use
[`#pragma`](../guide/values-and-types.md#pragmas), not a function call.

```mobius
var cfg = get_type_config()
print(cfg)
```

---

## Utility

### random() / random(max) / random(min, max)

- no arguments → a float in `[0.0, 1.0)`
- one argument → an integer in `[0, max - 1]`
- two arguments → an integer in `[min, max]` (inclusive)

```mobius
random()       // 0.734...
random(10)     // 0..9
random(5, 15)  // 5..15
```

### randomseed(seed)

Seed the random number generator for reproducible sequences.

### time() -> int64

Current Unix timestamp in seconds.

### clock() -> int64

A monotonic timestamp in nanoseconds. Subtract two readings to measure elapsed
time; divide by `1000000000.0` for seconds.

```mobius
var start = clock()
// ... work ...
var seconds = float(clock() - start) / 1000000000.0
```

### load(path)

Execute another Mobius script file; its globals become available in the current
scope. See [Modules and Packages](../guide/modules-and-packages.md#load).

### id(value) -> int64

Return the memory address of a heap-allocated value (table, array, function,
buffer, userdata), or `0` for value types. Useful for identity comparisons.

```mobius
var a = [1, 2, 3]
var b = a
print(id(a) == id(b))    // true — same object
```

---

For fibers, channels, and the `fiber` builtin, see
[Concurrency](../guide/concurrency.md). For the formal grammar, see
[Grammar](grammar.md).
