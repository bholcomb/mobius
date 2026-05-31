# Control Flow

[← Documentation home](../index.md) · [Guide: Values and Types](values-and-types.md)

Conditions in `if`, `while`, and `for` are wrapped in parentheses. Blocks use
braces. A condition is "truthy" unless it is `false` or `nil`.

---

## if / elif / else

```mobius
if (x > 0) {
    print("positive")
} elif (x == 0) {
    print("zero")
} else {
    print("negative")
}
```

`elif` chains as far as you like; `else` is optional. The body of a branch may be
a single statement without braces, but braces are recommended.

## while

```mobius
var i = 0
while (i < 10) {
    print(i)
    i++
}
```

## for

C-style `for` with an initializer, condition, and increment — all optional:

```mobius
for (var i = 0; i < 10; i++) {
    print(i)
}

var j = 0
for (; j < 5;) {       // all three clauses optional
    print(j)
    j++
}
```

## for … in

`for … in` iterates over the contents of an array or table. The loop variable(s)
may be declared with `var` or reuse existing names.

**Arrays** — one variable binds each element; two variables bind index and
element:

```mobius
for (var x in [10, 20, 30]) {
    print(x)              // 10, 20, 30
}

for (var i, x in [10, 20, 30]) {
    print(i, x)           // 0 10, 1 20, 2 30
}
```

**Tables** — one variable binds each key; two variables bind key and value:

```mobius
for (var k in {a: 1, b: 2}) {
    print(k)              // a, b
}

for (var k, v in {a: 1, b: 2}) {
    print(k, v)           // a 1, b 2
}
```

## break and continue

```mobius
for (var i = 0; i < 100; i++) {
    if (i % 2 == 0) continue   // skip even numbers
    if (i > 10) break          // stop after 10
    print(i)
}
```

---

## switch

Mobius's `switch` is a pattern-matching construct, not just a constant lookup.
A single `switch` can mix value, range, comparison, type, and enum patterns.

> **Fall-through:** like C, cases fall through to the next case by default. Add
> `break` to stop. (`return` inside a case also stops, by leaving the function.)

### Value patterns

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

### Range patterns

`a..b` matches the **inclusive** range from `a` to `b`; `a...b` matches the
**exclusive** range (includes `a`, excludes `b`). Ranges work with integers,
floats, and characters:

```mobius
switch (score) {
    case 90..100:  grade = "A"; break    // inclusive: 90 through 100
    case 80...90:  grade = "B"; break    // exclusive: 80 through 89
    case 70..79:   grade = "C"; break
    default:       grade = "F"
}

switch (letter) {
    case 'a'..'f':  print("early alphabet"); break
    case 'g'..'z':  print("later alphabet"); break
}
```

### Comparison patterns

A case may use a relational operator:

```mobius
switch (temperature) {
    case >= 100:  print("boiling"); break
    case <= 0:    print("freezing"); break
    case > 30:    print("hot"); break
    default:      print("comfortable")
}
```

### Type patterns

Match by runtime type with `is`:

```mobius
switch (value) {
    case is string:  print("a string"); break
    case is int64:   print("an integer"); break
    case is array:   print("an array"); break
    case is table:   print("a table"); break
    case is nil:     print("nil"); break
}
```

Names accepted after `is`: `int64` (any integer up to `INT64_MAX`), `uint64`
(larger integers), `float64` (any floating-point value), `bool`/`boolean`,
`nil`, `string`, `array`, `table`, `function`. These are the same type names
used in annotations and reported by `typeof`.

### Enum patterns

```mobius
enum Season { SPRING, SUMMER, FALL, WINTER }

var s = Season.FALL
switch (s) {
    case Season.SPRING:  print("flowers bloom"); break
    case Season.SUMMER:  print("sun shines"); break
    case Season.FALL:    print("leaves change"); break
    case Season.WINTER:  print("snow falls"); break
}
```

### Multi-pattern cases

Match several patterns in one case with commas:

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

### Guards — `when`

Any case can add a `when <expression>` guard. The case matches only if both the
pattern matches **and** the guard is truthy:

```mobius
func classify(n) {
    switch (n) {
        case is int64 when n < 0:  return "negative int"
        case is int64:             return "non-negative int"
        default:                   return "other"
    }
}

print(classify(-5))   // "negative int"
print(classify(10))   // "non-negative int"
```

### Intentional fall-through

```mobius
switch (x) {
    case 1:
        print("one")          // no break — falls through
    case 2:
        print("one or two")
        break                 // stops here
    case 3:
        print("three")
        break
}
```

---

Next: [Functions](functions.md).
