# Mobius Language Reference

Mobius is a type-inferred scripting language designed for easy embedding in
C and C++ applications. Variables are type-locked — their type is inferred
from the first non-nil assignment and cannot change. Mobius draws inspiration
from Lua while adding features like type locking, enums, a rich `switch`
statement with pattern matching, and a familiar C-style syntax.

Mobius source files use the `.mob` extension.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Lexical Structure](#lexical-structure)
3. [Variables and Types](#variables-and-types)
4. [Operators](#operators)
5. [Control Flow](#control-flow)
6. [Functions](#functions)
7. [Arrays](#arrays)
8. [Tables](#tables)
9. [Enums](#enums)
10. [Modules and Imports](#modules-and-imports)
11. [Pragmas](#pragmas)
12. [Concurrency](#concurrency)
13. [Formal Grammar](#formal-grammar)

---

## Quick Start

```mobius
// hello.mob — your first Mobius program
print("Hello, world!")

var name = "Mobius"
var version = 1

func greet(who) {
    print("Welcome to", who, "v" + str(version))
}

greet(name)
```

Run it:

```bash
mobius hello.mob
```

Or start an interactive session:

```bash
mobius
```

### Command-Line Options

| Flag               | Description                                     |
|--------------------|-------------------------------------------------|
| `--help`, `-h`     | Show help                                       |
| `--debug`, `-d`    | Enable debug output                             |
| `--decompile`, `-d`  | Dump bytecode disassembly                               |
| `--list-modules`   | List all loaded modules and their functions      |

---

## Lexical Structure

### Comments

```mobius
// This is a single-line comment

/* This is a
   multi-line comment */
```

### Statement Terminators

Statements are terminated by a **newline** or a **semicolon**. Semicolons are
optional — bare newlines work as terminators everywhere.

```mobius
var x = 10    // newline terminates
var y = 20;   // semicolon terminates
```

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or
underscores:

```
my_var  _private  count2  MAX_SIZE
```

### Keywords

```
and     break    case     continue  default  else   elif
enum    false    for      func      if       import is
nil     not      or       return    switch   true   var
while
```

---

## Variables and Types

### Declaration

Variables are declared with `var`. An initializer is optional (defaults to
`nil`).

```mobius
var count = 0
var name = "Alice"
var uninitialized       // nil
```

### Type Locking

A variable's type is **locked** at its first non-nil assignment and cannot
change. The type is inferred automatically from the initializer — no
explicit type syntax is required.

```mobius
var x = 42          // x is locked to integer
var s = "hello"     // s is locked to string
var t = {}          // t is locked to table

x = 99              // OK — integer to integer
x = nil             // OK — nil is always valid
x = "oops"          // ERROR: cannot assign string to variable of type integer
```

**Rules:**

- The type is inferred from the initializer expression. Literals, arithmetic
  results, array/table literals, and function return values (when the function
  is defined before the call site) are all inferred at compile time.
- `nil` is a valid value for any variable regardless of its locked type. A
  variable locked to `integer` can hold `nil` (meaning "no value") or an
  integer.
- `var x = nil` and `var x` (no initializer) create a variable whose type is
  not yet determined. The type locks on the first non-nil assignment.
- All non-nil return paths in a function must agree on type. Returning
  different non-nil types from different branches is a compile error.
- To convert between types, use the `int()`, `float()`, or `str()` functions
  and store the result in a new variable.

```mobius
// Type locks on first non-nil assignment
var v = nil
v = 42              // v is now locked to integer
v = 99              // OK
v = nil             // OK — nil is always valid
v = "hello"         // ERROR: cannot assign string to variable of type integer

// Conversions go into new variables
var n = 42
var s = str(n)      // s is locked to string — new variable, no conflict

// Function return type consistency
func find(arr, target) {
    for (var i = 0; i < arr:length(); i++) {
        if (arr[i] == target) { return i }   // integer
    }
    return nil                               // OK — nil is compatible
}

// COMPILE ERROR: inconsistent return types
// func bad(flag) {
//     if (flag) { return 42 }       // integer
//     else { return "hello" }       // string — mismatch!
// }
```

Type locking enables the compiler to emit specialized opcodes for arithmetic
and comparisons when both operand types are known, eliminating runtime type
checks on the hot path.

### Optional Type Annotations

You can annotate a variable with **`int64`**, **`uint64`**, or **`float64`**.
When `#pragma strict_types` is enabled, the interpreter enforces these at
runtime. Type annotations work alongside type locking — an annotated variable
is locked to the annotated type.

```mobius
var age: int64 = 25
var count: uint64 = 0
var pi: float64 = 3.14159
```

Functions also support optional type annotations on parameters and return
values (see [Functions](#functions) for details):

```mobius
func add(a: int, b: int): int {
    return a + b
}
```

### Available Types

Variable declarations accept **`int64`**, **`uint64`**, and **`float64`** as
type annotations. Function parameter and return type annotations accept the
full set of type names: **`int`** (alias `integer`, `int64`), **`uint64`**,
**`float`** (alias `float64`), **`bool`** (alias `boolean`), **`string`**,
**`array`**, **`table`**, **`function`**, **`nil`**, **`userdata`**, and
**`enum`**.

| Category  | In annotations (`var x: …`) | Runtime values (`typeof`, etc.) |
|-----------|-----------------------------|----------------------------------|
| Integers  | `int64`, `uint64`           | Reported as `int64` (and similar) for whole numbers |
| Floats    | `float64`                   | Floating-point values            |
| Other     | —                           | `string`, `bool`, `char`, `nil`  |
| Compound  | —                           | `array`, `table`, `function`     |

### Type Conversions

Use the built-in conversion functions. Since variables are type-locked, store
the converted result in a new variable rather than reassigning:

```mobius
var x = 42
var xs = str(x)      // "42" — new string variable
var xf = float(x)    // 42.0 — new float variable

var y = 3.14
var yi = int(y)      // 3 — new integer variable

typeof(x)    // "int64"
typeof("hi") // "string"
```

### Literals

**Integers** support decimal, hexadecimal, and binary:

```mobius
var dec = 255
var hex = 0xFF
var bin = 0b11111111
```

**Floats**:

```mobius
var f = 3.14
var g = 0.5
```

**Strings** use double quotes with escape sequences:

```mobius
var s = "Hello\tWorld\n"
// Escapes: \n \t \r \\ \" \' \0
```

**Characters** use single quotes:

```mobius
var ch = 'A'
```

**Booleans**:

```mobius
var yes = true
var no = false
```

**Nil** represents the absence of a value:

```mobius
var nothing = nil
```

---

## Operators

### Precedence Table (Low to High)

| Prec | Operators                     | Assoc | Description            |
|------|-------------------------------|-------|------------------------|
| 1    | `=` `+=` `-=` `*=` `/=`      | right | Assignment             |
| 2    | `or` `\|\|`                   | left  | Logical OR             |
| 3    | `and` `&&`                    | left  | Logical AND            |
| 4    | `\|`                          | left  | Bitwise OR             |
| 5    | `^`                           | left  | Bitwise XOR            |
| 6    | `&`                           | left  | Bitwise AND            |
| 7    | `==` `!=`                     | left  | Equality               |
| 8    | `<` `<=` `>` `>=`             | left  | Comparison             |
| 9    | `<<` `>>`                     | left  | Bit shift              |
| 10   | `+` `-`                       | left  | Addition / subtraction |
| 11   | `*` `/` `%`                   | left  | Multiply / divide / mod|
| 12   | `!` `-` `not` `+` `~`        | right | Unary                  |
| 13   | `()` `[]` `.` `:` `++` `--`  | left  | Call / index / method / postfix |

### Arithmetic

```mobius
var a = 10 + 5     // 15
var b = 10 - 3     // 7
var c = 4 * 3      // 12
var d = 15 / 4     // 3.75
var e = 15 % 4     // 3
```

### Compound Assignment

```mobius
var x = 10
x += 5    // x is now 15
x -= 3    // x is now 12
x *= 2    // x is now 24
x /= 4   // x is now 6
```

### Increment / Decrement

Both prefix and postfix forms are supported:

```mobius
var a = 5
print(++a)   // 6 — increments then returns
print(a++)   // 6 — returns then increments
print(a)     // 7

var b = 10
print(--b)   // 9
print(b--)   // 9
print(b)     // 8
```

These work naturally in `for` loops:

```mobius
for (var i = 0; i < 10; i++) {
    print(i)
}
```

### Logical

```mobius
true and false   // false
true or false    // true
not true         // false

// C-style alternatives
true && false
true || false
!true
```

### Bitwise

```mobius
var a = 0xFF & 0x0F   // AND  → 0x0F
var b = 0xF0 | 0x0F   // OR   → 0xFF
var c = 0xFF ^ 0x0F   // XOR  → 0xF0
var d = ~0             // NOT
var e = 1 << 4         // left shift  → 16
var f = 16 >> 2        // right shift → 4
```

### Comparison

```mobius
1 == 1     // true
1 != 2     // true
3 > 2      // true
2 < 3      // true
3 >= 3     // true
2 <= 3     // true
```

### String Concatenation

The `+` operator concatenates when either operand is a string:

```mobius
"Hello" + " " + "World"   // "Hello World"
"Count: " + 42            // "Count: 42"
```

### Method Calls (`:` syntax)

Mobius uses a dual-syntax convention inspired by Lua:

- **`.`** (dot) accesses fields, module namespaces, and table keys. No
  implicit argument is passed.
- **`:`** (colon) performs a method call, automatically passing the
  receiver object as `self` (the first argument to the function).

```mobius
// Dot syntax — field/module access
var name = person.name
var result = math.sin(3.14)

// Colon syntax — method call (self is implicit)
var arr = [3, 1, 2]
arr:sort()                  // equivalent to sort(arr)
arr:push(4)                 // equivalent to push(arr, 4)
print(arr:length())         // 4
```

The `:` syntax works on any value that has a type metatable. The built-in
standard library registers type metatables for arrays, tables, and channels,
giving them methods out of the box. You can also set type metatables from
C/C++ code — see the [Embedding Guide](embedding_guide.md#type-metatables).

Method calls can be chained:

```mobius
var evens = [1, 2, 3, 4, 5]:filter(func(x, i) { return x % 2 == 0; })
print(evens:length())    // 2
```

---

## Control Flow

### if / elif / else

```mobius
if (x > 0) {
    print("positive")
} elif (x == 0) {
    print("zero")
} else {
    print("negative")
}
```

### while

```mobius
var i = 0
while (i < 10) {
    print(i)
    i++
}
```

### for

C-style `for` loops:

```mobius
for (var i = 0; i < 10; i++) {
    print(i)
}
```

The initializer, condition, and increment are all optional:

```mobius
var j = 0
for (; j < 5;) {
    print(j)
    j++
}
```

### break and continue

```mobius
for (var i = 0; i < 100; i++) {
    if (i % 2 == 0) continue   // skip even numbers
    if (i > 10) break          // stop after 10
    print(i)
}
```

### switch

Mobius has a powerful `switch` statement that supports multiple kinds of case
patterns, far beyond simple value matching.

#### Value Patterns

```mobius
switch (color) {
    case "red":
        print("Stop")
        break
    case "green":
        print("Go")
        break
    default:
        print("Unknown")
}
```

#### Range Patterns

Match against inclusive ranges with `..` (inclusive of both endpoints):

```mobius
switch (score) {
    case 90..100:
        grade = "A"
        break
    case 80..89:
        grade = "B"
        break
    case 70..79:
        grade = "C"
        break
    default:
        grade = "F"
}
```

Ranges work with integers, floats, and characters:

```mobius
switch (letter) {
    case 'a'..'f':
        print("early alphabet")
        break
    case 'g'..'z':
        print("later alphabet")
        break
}
```

#### Comparison Patterns

Use relational operators in case expressions:

```mobius
switch (temperature) {
    case >= 100:
        print("boiling")
        break
    case <= 0:
        print("freezing")
        break
    case > 30:
        print("hot")
        break
    default:
        print("comfortable")
}
```

#### Type Patterns

Match by runtime type with `is`:

```mobius
switch (value) {
    case is string:
        print("it's a string")
        break
    case is int:
        print("it's an integer")
        break
    case is array:
        print("it's an array")
        break
    case is table:
        print("it's a table")
        break
    case is nil:
        print("it's nil")
        break
}
```

Supported names after `is`: `nil`, `bool` or `boolean`, `int` or `integer` (any
integer value), `float` (any floating-point value), `string`, `array`, `table`,
`function`. These describe runtime categories, not the narrow numeric annotation
keywords (`int64`, `uint64`, `float64` are not used here).

#### Enum Patterns

Match against enum members:

```mobius
enum Color { RED, GREEN, BLUE }

switch (my_color) {
    case Color.RED:
        print("red")
        break
    case Color.GREEN:
        print("green")
        break
    case Color.BLUE:
        print("blue")
        break
}
```

#### Multi-Pattern Cases

Match multiple patterns in a single case with commas:

```mobius
switch (day) {
    case "Saturday", "Sunday":
        print("weekend")
        break
    case "Monday", "Tuesday", "Wednesday", "Thursday", "Friday":
        print("weekday")
        break
}
```

#### Fall-Through

Cases fall through by default (like C). Use `break` to prevent fall-through:

```mobius
switch (x) {
    case 1:
        print("one")        // falls through to case 2
    case 2:
        print("one or two")
        break               // stops here
    case 3:
        print("three")
        break
}
```

---

## Functions

### Declaration

Functions are declared with the `func` keyword:

```mobius
func add(a, b) {
    return a + b
}

var result = add(10, 20)    // 30
```

### Parameter and Return Type Annotations

Function parameters and return values can optionally be annotated with types.
All annotations are optional — you can annotate all, some, or none of the
parameters, and the return type can be omitted.

```mobius
// Fully annotated
func add(a: int, b: int): int {
    return a + b
}

// Mixed — only some parameters annotated
func greet(name: string, times) {
    for (var i = 0; i < times; i++) {
        print("Hello,", name)
    }
}

// Return type only
func pi(): float {
    return 3.14159
}

// No annotations (inferred — same as before)
func double(n) {
    return n * 2
}
```

When a return type is declared, the compiler knows the function's return type
before compiling its body. This enables type-specialized opcodes for recursive
calls:

```mobius
func fibonacci(n: int): int {
    if (n <= 1) return n
    return fibonacci(n - 1) + fibonacci(n - 2)
}
```

Without the `: int` return annotation, the compiler cannot determine the
return type of `fibonacci` during compilation of its own body (since it
hasn't finished compiling yet). With the annotation, the recursive
`fibonacci(n - 1) + fibonacci(n - 2)` addition uses a fast integer-only
opcode instead of a generic one.

Parameter type annotations set the inferred type for that local variable,
which improves type specialization for operations on those parameters within
the function body. Unannotated parameters remain `unknown` until assigned.

Accepted type names: `int` (aliases `integer`, `int64`), `uint64`,
`float` (alias `float64`), `bool` (alias `boolean`), `string`, `array`,
`table`, `function`, `nil`, `userdata`, `enum`.

### Return Values

Functions return `nil` by default. Use `return` to return a value:

```mobius
func max(a, b) {
    if (a > b) return a
    return b
}
```

### First-Class Functions

Functions are values and can be assigned to variables, passed as arguments, or
returned from other functions:

```mobius
func apply(f, x) {
    return f(x)
}

func double(n) {
    return n * 2
}

print(apply(double, 5))    // 10
```

### Closures

Inner functions capture variables from their enclosing scope:

```mobius
func make_counter() {
    var count = 0
    func increment() {
        count = count + 1
        return count
    }
    return increment
}

var counter = make_counter()
print(counter())    // 1
print(counter())    // 2
print(counter())    // 3
```

Each call to the outer function creates an independent closure:

```mobius
var a = make_counter()
var b = make_counter()
a()    // 1
a()    // 2
b()    // 1 — independent from a
```

### Nested Closures

Closures can be nested arbitrarily deep:

```mobius
func outer(x) {
    func middle(y) {
        func inner(z) {
            return x + y + z
        }
        return inner
    }
    return middle
}

var f = outer(1)(2)(3)    // 6
```

### Recursion

Functions can call themselves. Adding type annotations lets the compiler emit
optimized opcodes for the recursive arithmetic:

```mobius
func fibonacci(n: int): int {
    if (n <= 1) return n
    return fibonacci(n - 1) + fibonacci(n - 2)
}

print(fibonacci(10))    // 55
```

---

## Arrays

### Array Literals

```mobius
var numbers = [1, 2, 3, 4, 5]
var mixed = [1, "hello", 3.14, true, nil]
var nested = [[1, 2], [3, 4], [5, 6]]
var empty = []
```

### Indexing

Arrays are zero-indexed:

```mobius
var arr = [10, 20, 30]
print(arr[0])     // 10
print(arr[2])     // 30
arr[1] = 99
print(arr[1])     // 99
```

Nested access:

```mobius
var matrix = [[1, 2], [3, 4]]
print(matrix[1][0])    // 3
```

### Array Methods

Array operations use the `:` method-call syntax. The only global array
function is `array_create(capacity [, fill_value])`.

| Method                    | Description                                         |
|---------------------------|-----------------------------------------------------|
| `arr:push(value)`         | Append a value to the end                           |
| `arr:pop()`               | Remove and return the last element                  |
| `arr:get(index)`          | Get element at index                                |
| `arr:set(index, value)`   | Set element at index                                |
| `arr:length()`            | Return the number of elements                       |
| `arr:slice(start, end)`   | Return a sub-array from start to end (exclusive)    |
| `arr:concat(other)`       | Return a new array combining arr and other          |
| `arr:reverse()`           | Reverse in-place and return                         |
| `arr:find(value)`         | Return the index of value, or -1 if not found       |
| `arr:sort([cmp])`         | Sort in-place (optional comparator function)        |
| `arr:map(fn)`             | Apply fn(element, index) and return new array       |
| `arr:filter(fn)`          | Return new array of elements where fn returns true  |
| `arr:reduce(fn, init)`    | Fold to a single value                              |
| `arr:foreach(fn)`         | Call fn(element, index) for each element            |
| `arr:any(fn)`             | True if fn returns true for any element             |
| `arr:all(fn)`             | True if fn returns true for every element           |
| `len(arr)`                | Global function; also works for arrays and strings  |

```mobius
var arr = array_create(10)
arr:push("first")
arr:push("second")
arr:push("third")

print(arr:length())            // 3
print(arr:find("second"))      // 1

var last = arr:pop()
print(last)                    // "third"

[1, 2, 3]:reverse()           // [3, 2, 1]

[1, 2]:concat([3, 4])         // [1, 2, 3, 4]

[1, 2, 3]:map(func(x, i) { return x * 10; })     // [10, 20, 30]
[1, 2, 3, 4]:filter(func(x, i) { return x > 2; })  // [3, 4]
[1, 2, 3]:reduce(func(acc, x) { return acc + x; }, 0)  // 6
```

---

## Tables

Tables are key-value containers, similar to Lua tables, JavaScript objects, or
Python dicts. They support string keys, integer keys, and computed keys.

### Creating Tables

```mobius
// String keys (shorthand)
var person = {
    name: "Alice",
    age: 30,
    city: "New York"
}

// Computed keys
var lookup = {
    [1] = "first",
    ["hello"] = "world",
    [2 + 3] = "five"
}

// Array-style (auto-indexed starting at 1)
var fruits = {"apple", "banana", "cherry"}
```

### Accessing Fields

```mobius
print(person.name)       // "Alice"
print(person["age"])     // 30
person.city = "Boston"
```

### Table Methods

Table operations use the `:` method-call syntax. `setmetatable`,
`getmetatable`, and `len` remain global functions.

| Method                 | Description                                |
|------------------------|--------------------------------------------|
| `tbl:remove(key)`      | Remove a key                               |
| `tbl:has_key(key)`     | Return `true` if the key exists            |
| `tbl:size()`           | Return the number of entries               |
| `tbl:pairs()`          | Return an array of `[key, value]` pairs    |

```mobius
var config = {}
config["debug"] = true
config["version"] = "1.0"

print(config:has_key("debug"))    // true
print(config:size())              // 2

var all = config:pairs()
for (var i = 0; i < len(all); i++) {
    var pair = all[i]
    print("Key:", pair[0], "Value:", pair[1])
}

config:remove("debug")
```

### Metatables

Tables can have a **metatable** that controls behavior when keys are missing
or when operators are applied. This is the foundation for OOP-style patterns.

#### \_\_index — Property Lookup Fallback

When you access a key that doesn't exist in a table, the runtime checks the
metatable's `__index` field. If `__index` is a table, the lookup continues
there.

```mobius
var defaults = {color: "blue", size: 10}
var meta = {["__index"] = defaults}

var obj = {color: "red"}
setmetatable(obj, meta)

print(obj.color)    // "red"   — found directly
print(obj.size)     // 10      — found via __index
```

This enables prototype-based inheritance:

```mobius
var Animal = {species: "Unknown", legs: 0}
var Mammal = {warm_blooded: true}
setmetatable(Mammal, {["__index"] = Animal})

var dog = {species: "Dog", legs: 4}
setmetatable(dog, {["__index"] = Mammal})

print(dog.species)       // "Dog"        — from dog
print(dog.warm_blooded)  // true         — from Mammal
print(dog.legs)          // 4            — from dog
```

#### Arithmetic Metamethods

Define `__add`, `__sub`, `__mul`, `__div` on a metatable to customize
operator behavior:

```mobius
var vec_meta = {
    ["__add"] = func(a, b) {
        return {
            x: a.x + b.x,
            y: a.y + b.y
        }
    }
}

var v1 = {x: 1, y: 2}
var v2 = {x: 3, y: 4}
setmetatable(v1, vec_meta)
setmetatable(v2, vec_meta)

var v3 = v1 + v2
print(v3.x, v3.y)    // 4  6
```

#### Metatable Functions

| Function                    | Description                        |
|-----------------------------|------------------------------------|
| `setmetatable(table, meta)` | Set the metatable for a table      |
| `getmetatable(table)`       | Return the current metatable       |

---

## Enums

Enums define a set of named integer constants.

### Basic Enums

Values auto-increment from 0:

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

### Explicit Values

```mobius
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500
}
```

Values after an explicit value auto-increment from that value:

```mobius
enum Direction : int64 {
    NORTH = -100,
    EAST,           // -99
    SOUTH = 50,
    WEST            // 51
}
```

### Typed Enums

Specify the underlying integer type after a colon (`int64` or `uint64`):

```mobius
enum Flags : uint64 {
    NONE = 0,
    READ = 1,
    WRITE = 2,
    EXECUTE = 4,
    ALL = 7
}

enum BigId : uint64 {
    FIRST = 1,
    SECOND
}
```

Allowed underlying types: **`int64`** and **`uint64`** only.

### Enums in Switch

```mobius
enum Season { SPRING, SUMMER, FALL, WINTER }

var s = Season.FALL

switch (s) {
    case Season.SPRING:
        print("flowers bloom")
        break
    case Season.SUMMER:
        print("sun shines")
        break
    case Season.FALL:
        print("leaves change")
        break
    case Season.WINTER:
        print("snow falls")
        break
}
```

---

## Modules and Imports

### Importing Modules

The `import` statement loads a plugin module and makes its functions available
under a namespace:

```mobius
import "math"

print(math.sin(3.14))
print(math.factorial(5))
```

### Import with Alias

```mobius
import "math" as m

print(m.cos(0))        // 1
print(m.sqrt(16))      // 4
```

### Loading Scripts

Use the `load()` function to execute another `.mob` file. Any globals defined
in the loaded file become available in the current scope:

```mobius
load("utils.mob")
// functions from utils.mob are now accessible
```

---

## Pragmas

Pragmas control interpreter behavior at the script level. They are processed
at parse time and affect the remainder of the file (or until changed).

```mobius
#pragma strict_types true       // enforce type annotations at runtime
#pragma strict_types false      // disable strict checking

#pragma type_warnings true      // warn on implicit type conversions
#pragma type_warnings false

#pragma override_behavior error // error on function name conflicts (default)
#pragma override_behavior warn  // warn but allow overrides
#pragma override_behavior quiet // silently allow overrides
```

### strict_types

When enabled, the interpreter enforces that values assigned to typed variables
match the declared type:

```mobius
#pragma strict_types true

var x: int64 = 42       // ok
var y: int64 = "hello"  // runtime error: type mismatch
```

### override_behavior

Controls what happens when a function or global is redefined:

```mobius
#pragma override_behavior warn

func greet() { print("hello") }
func greet() { print("hi") }     // warning printed, but allowed
```

---

## Concurrency

Mobius provides fiber-based concurrency through the `spawn`, `await`, `yield`, and `shared` keywords.

### Spawn and Await

`spawn` launches a function call on a separate fiber and returns a **future**. `await` blocks until the future resolves and returns its value.

```mobius
func compute(n) {
    var sum = 0
    for (var i = 0; i < n; i += 1) { sum += i }
    return sum
}

var f = spawn compute(1000)
// ... do other work ...
var result = await f  // blocks until compute() finishes
print(result)         // 499500
```

Multiple fibers can run in parallel:

```mobius
func double(x) { return x * 2 }

var a = spawn double(10)
var b = spawn double(20)
var c = spawn double(30)
print(await a, await b, await c)  // 20 40 60
```

Awaiting a future that has already resolved returns the cached result immediately. Futures are single-assignment: once resolved or rejected, they never change.

**Restrictions:**
- Only functions without captured upvalues can be spawned. Pass data as arguments instead.
- Native (C) functions cannot be spawned.

### Yield

The `yield` statement cooperatively yields the current fiber, allowing other fibers to run:

```mobius
for (var i = 0; i < 1000; i += 1) {
    // do work
    yield  // let other fibers make progress
}
```

### Shared Containers

By default, arrays and tables are **not** thread-safe. The `shared` keyword marks a container for concurrent access by adding mutex protection:

```mobius
var data = shared [0, 0, 0, 0]
```

`shared` propagates deeply — nested arrays and tables are also marked shared. All reads and writes to a shared container are automatically synchronized.

### Channels

Channels provide typed, bounded message-passing between fibers. They are
created via the `fiber` module and use the `:` method-call syntax:

```mobius
import "fiber"

var ch = fiber.channel(10)  // bounded channel with capacity 10

// Producer fiber
func producer(ch) {
    ch:send(42)
    ch:close()
}
spawn producer(ch)

// Consumer
var msg = ch:recv()
print(msg)    // 42
```

Channel methods: `ch:send(value)`, `ch:recv()`, `ch:try_send(value)`,
`ch:try_recv()`, `ch:close()`.

See the [Standard Library Reference](stdlib_reference.md#fiber--concurrency-functions) for the full channel API.

### Structured Concurrency

`fiber_all` waits for an array of futures to resolve and returns all results:

```mobius
func work(id) { return id * 10 }

var futures = [spawn work(1), spawn work(2), spawn work(3)]
var results = fiber_all(futures)  // [10, 20, 30]
```

`fiber_any` returns the result of the first future to resolve.

### Cancellation

Spawned fibers can be cancelled via `fiber_cancel(future)`. A cancelled fiber will throw a `CancellationError` at its next cancellation check point.

### Array Slices

`fiber_slice(array, start, length)` creates a lightweight view into an existing array. Reads and writes through the slice affect the underlying array, making slices ideal for data-parallel decomposition:

```mobius
var data = shared [1, 2, 3, 4, 5, 6, 7, 8]
var left  = fiber_slice(data, 0, 4)  // view of [1,2,3,4]
var right = fiber_slice(data, 4, 4)  // view of [5,6,7,8]
```

---

## Formal Grammar

The complete formal grammar for Mobius is in
[BNF.md](BNF.md).
