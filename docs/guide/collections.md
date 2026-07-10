# Collections

Arrays, tables, and enums are the building blocks for structured data in Mobius.
Arrays and tables both use the `:` method-call syntax (see
[Method calls](#method-calls)).

[← Documentation home](../index.md) · [Guide: Error Handling](error-handling.md)

---

## Method calls — `.` vs `:`

Mobius uses a Lua-style dual syntax:

- **`.`** (dot) accesses a field, table key, or module member. No implicit
  argument is passed.
- **`:`** (colon) calls a **method**, automatically passing the receiver as the
  first argument (`self`).

```mobius
var arr = [3, 1, 2]
arr:sort()              // method call; arr is passed as self
print(arr:size())     // 3

var person = { name: "Alice" }
print(person.name)      // field access — no self
```

Method calls chain:

```mobius
var evens = [1, 2, 3, 4, 5]:filter(func(x, i) { return x % 2 == 0; })
print(evens:size())   // 2
```

---

## Arrays

Arrays are zero-indexed, growable, ordered sequences. They can hold mixed types.

```mobius
var numbers = [1, 2, 3, 4, 5]
var mixed   = [1, "hello", 3.14, true, nil]
var nested  = [[1, 2], [3, 4]]
var empty   = []
```

### Indexing

```mobius
var arr = [10, 20, 30]
print(arr[0])        // 10
arr[1] = 99
print(arr[1])        // 99

var matrix = [[1, 2], [3, 4]]
print(matrix[1][0])  // 3
```

To pre-size an array, use the global
[`array_create(capacity [, fill])`](../reference/standard-library.md#array_createcapacity--fill_value---array):

```mobius
var grid = array_create(10, 0)   // ten zeros
```

### Array methods

| Method                  | Description                                                  |
|-------------------------|--------------------------------------------------------------|
| `arr:push(value)`       | Append a value to the end                                    |
| `arr:pop()`             | Remove and return the last element (`nil` if empty)          |
| `arr:get(index)`        | Element at index (`nil` if out of bounds)                    |
| `arr:set(index, value)` | Set element at index                                         |
| `arr:size()`          | Number of elements                                           |
| `arr:slice(start, end)` | New sub-array (copy) over `[start, end)`                     |
| `arr:span(start, end)`  | Aliasing **view** over `[start, end)` — writes affect the original |
| `arr:concat(other, …)`  | New array combining `arr` with one or more arrays            |
| `arr:reverse()`         | Reverse in place and return the array                        |
| `arr:find(value)`       | Index of the first match, or `-1`                            |
| `arr:sort([cmp])`       | Sort in place; optional comparator `func(a, b) -> bool`      |
| `arr:map(fn)`           | New array of `fn(element, index)` results                    |
| `arr:filter(fn)`        | New array of elements where `fn(element, index)` is truthy   |
| `arr:reduce(fn, init)`  | Fold to a single value with `fn(accumulator, element)`       |
| `arr:foreach(fn)`       | Call `fn(element, index)` for each element                   |
| `arr:any(fn)`           | `true` if `fn(element)` is truthy for any element            |
| `arr:all(fn)`           | `true` if `fn(element)` is truthy for every element          |

The global [`size(arr)`](../reference/standard-library.md#strings)
also returns an array's length.

```mobius
var arr = []
arr:push("first")
arr:push("second")
print(arr:size())            // 2
print(arr:find("second"))      // 1

[1, 2, 3]:map(func(x, i) { return x * 10; })            // [10, 20, 30]
[1, 2, 3, 4]:filter(func(x, i) { return x > 2; })       // [3, 4]
[1, 2, 3]:reduce(func(acc, x) { return acc + x; }, 0)   // 6

var nums = [3, 1, 4, 1, 5]
nums:sort()                                  // [1, 1, 3, 4, 5]
nums:sort(func(a, b) { return a > b; })      // [5, 4, 3, 1, 1]
```

`arr:slice` and `arr:span` both take a half-open `[start, end)` range, but
differ in ownership: **`slice` copies** (the result is independent), while
**`span` is a view** (writes pass through to the original, and it can be handed
to a `spawn`ed fiber). See [Array spans](concurrency.md#array-spans).

Iterate with [`for … in`](control-flow.md#for--in) for index/element binding.

---

## Tables

Tables are key-value containers — like Lua tables, JS objects, or Python dicts.
Keys can be strings, integers, or computed expressions.

```mobius
// string keys (shorthand)
var person = {
    name: "Alice",
    age: 30,
    city: "New York"
}

// computed keys
var lookup = {
    [1] = "first",
    ["hello"] = "world",
    [2 + 3] = "five"
}
```

### Accessing fields

```mobius
print(person.name)       // "Alice"
print(person["age"])     // 30
person.city = "Boston"
```

### Table methods

| Method             | Description                              |
|--------------------|------------------------------------------|
| `tbl:has_key(key)` | `true` if the key exists                 |
| `tbl:size()`       | Number of entries                        |
| `tbl:pairs()`      | Array of `[key, value]` pairs            |
| `tbl:remove(key)`  | Remove a key                             |

```mobius
var config = {}
config["debug"] = true
config["version"] = "1.0"

print(config:has_key("debug"))   // true
print(config:size())             // 2

for (var k, v in config) {       // iterate with for … in
    print(k, v)
}

config:remove("debug")
```

`tbl:pairs()` is handy when you need an indexable snapshot of the entries:

```mobius
var all = person:pairs()
print(all[0][0], "=", all[0][1])   // e.g. name = Alice
```

### Iteration order

Table iteration order is **unspecified, and differs between runs of the same
program**. Keys are placed by hash, and the string hash is seeded with a random
value at process start so that a program accepting untrusted table keys (query
parameters, JSON object keys) cannot be fed keys crafted to collide. Do not rely
on the order of `pairs()` or of `for (var k in tbl)`; sort the keys if you need a
stable order.

To reproduce an order while debugging, pin the seed with the `MOBIUS_HASH_SEED`
environment variable (decimal, or `0x`-prefixed hex):

```bash
MOBIUS_HASH_SEED=42 bin/mobius script.mob
```

Arrays are ordered; only tables are affected.

---

## Metatables

A table can have a **metatable** that controls what happens on missing-key
lookups and operator application. This is the foundation for OOP-style patterns.

### `__index` — lookup fallback

When a key isn't found in a table, the runtime consults the metatable's
`__index`. If `__index` is a table, the lookup continues there:

```mobius
var defaults = { color: "blue", size: 10 }
var obj = { color: "red" }
setmetatable(obj, { ["__index"] = defaults })

print(obj.color)    // "red"  — found directly
print(obj.size)     // 10     — found via __index
```

Chaining `__index` gives prototype-based inheritance:

```mobius
var Animal = { species: "Unknown", legs: 0 }
var Mammal = { warm_blooded: true }
setmetatable(Mammal, { ["__index"] = Animal })

var dog = { species: "Dog", legs: 4 }
setmetatable(dog, { ["__index"] = Mammal })

print(dog.species)       // "Dog"   — from dog
print(dog.warm_blooded)  // true    — from Mammal
print(dog.legs)          // 4       — from dog
```

`__index` may instead be a **function**, called as `__index(self, key)` when a
key is missing — useful for computed or proxied properties:

```mobius
var proxy = {}
setmetatable(proxy, {
    ["__index"] = func(self, key) { return "computed:" + key }
})
print(proxy.anything)    // "computed:anything"
```

Resolution applies at **every level** of a chain: when a lookup misses, the
runtime follows table `__index` links and calls the first function `__index` it
encounters. A direct field always shadows `__index`. This works identically for
tables and for [userdata type metatables](../embedding/embedding-guide.md#type-metatables).

### Operator metamethods

Define `__add`, `__sub`, `__mul`, `__div` (and similar) to overload operators:

```mobius
var vec_meta = {
    ["__add"] = func(a, b) {
        return { x: a.x + b.x, y: a.y + b.y }
    }
}

var v1 = { x: 1, y: 2 }
var v2 = { x: 3, y: 4 }
setmetatable(v1, vec_meta)
setmetatable(v2, vec_meta)

var v3 = v1 + v2
print(v3.x, v3.y)    // 4  6
```

| Function                    | Description                   |
|-----------------------------|-------------------------------|
| `setmetatable(table, meta)` | Set the metatable for a table |
| `getmetatable(table)`       | Return the current metatable  |

---

## Enums

Enums define a set of named integer constants.

```mobius
enum Color {
    RED,       // 0
    GREEN,     // 1
    BLUE       // 2
}

var c = Color.RED
print(c)               // 0
print(c == Color.RED)  // true
```

### Explicit and typed values

Values after an explicit value auto-increment from it. The underlying integer
type may be `int64` (default) or `uint64`:

```mobius
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500
}

enum Direction : int64 {
    NORTH = -100,
    EAST,           // -99
    SOUTH = 50,
    WEST            // 51
}

enum Flags : uint64 {
    NONE = 0,
    READ = 1,
    WRITE = 2,
    EXECUTE = 4,
    ALL = 7
}
```

Allowed underlying types: `int64` and `uint64`. `typeof` reports an enum member
as `"enum"`. Enums pair naturally with `switch` — see
[Enum patterns](control-flow.md#enum-patterns).

---

Next: [Binary Data](binary-data.md).
