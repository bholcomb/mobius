# Functions

[← Documentation home](../index.md) · [Guide: Control Flow](control-flow.md)

Functions are declared with `func` and are first-class values: you can assign
them to variables, pass them as arguments, return them, and capture surrounding
variables in closures.

---

## Declaration

```mobius
func add(a, b) {
    return a + b
}

var result = add(10, 20)    // 30
```

Functions return `nil` by default. Use `return` to return a value:

```mobius
func max(a, b) {
    if (a > b) return a
    return b
}
```

## Type annotations

Parameters and the return value can be annotated. Annotations are optional and
independent — annotate all, some, or none:

```mobius
// fully annotated
func add(a: int64, b: int64): int64 {
    return a + b
}

// only some parameters
func greet(name: string, times) {
    for (var i = 0; i < times; i++) {
        print("Hello,", name)
    }
}

// return type only
func pi(): float64 {
    return 3.14159
}

// unannotated — inferred
func double(n) {
    return n * 2
}
```

Accepted type names: `int64`, `uint64`, `float64`, `bool`, `boolean`,
`string`, `array`, `table`, `buffer`, `function`, `nil`, `userdata`.

A declared **return type** is especially useful for recursion. Without it, the
compiler cannot know a function's return type while compiling its own body
(it hasn't finished compiling yet), so it falls back to generic opcodes. With
it, the recursive arithmetic uses fast type-specialized opcodes:

```mobius
func fibonacci(n: int64): int64 {
    if (n <= 1) return n
    return fibonacci(n - 1) + fibonacci(n - 2)
}

print(fibonacci(10))    // 55
```

Parameter annotations set the inferred type of each parameter, so
operations on them inside the body compile to type-specialized
instructions. Because the body trusts the annotation, calls to a named
function are checked against it at compile time — a provably wrong-typed
argument is a compile error:

```mobius
func add(a: int64, b: int64): int64 {
    return a + b
}
add("one", 2)
```

```text
Compile error [script.mob:4]: argument 1 of 'add' is string, but the parameter is annotated int64
```

`nil` arguments are always allowed, and arguments whose type the compiler
cannot prove (a table field, an untyped parameter passed along) compile
normally. Calls through function *values* — callbacks, functions stored in
tables — cannot be checked at compile time and are trusted as annotated.

## First-class functions

```mobius
func apply(f, x) {
    return f(x)
}

func double(n) { return n * 2 }

print(apply(double, 5))    // 10
```

Anonymous function expressions use the same `func` keyword without a name — they
are common as callbacks to array methods:

```mobius
var evens = [1, 2, 3, 4, 5]:filter(func(x, i) { return x % 2 == 0; })
```

## Closures

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
```

Each call to the outer function creates an independent closure:

```mobius
var a = make_counter()
var b = make_counter()
a()    // 1
a()    // 2
b()    // 1 — independent from a
```

Closures nest arbitrarily deep:

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

> **Concurrency note:** a closure passed to `spawn` may capture upvalues;
> captured values follow the same rules as arguments — non-shared data is copied
> (each spawn gets its own), while a captured `shared` variable is shared by
> reference. See [Concurrency](concurrency.md#value-semantics-across-spawn).

---

Next: [Error Handling](error-handling.md).
