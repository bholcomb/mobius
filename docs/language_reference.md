# Mobius Language Reference

**Version:** 0.1.0  
**Last Updated:** January 2025

Welcome to the complete Mobius scripting language reference! This document covers all language features, syntax, built-in functions, and programming concepts available in Mobius.

## Table of Contents

1. [Language Overview](#language-overview)
2. [Lexical Structure](#lexical-structure)
3. [Data Types](#data-types)
4. [Variables and Constants](#variables-and-constants)
5. [Operators](#operators)
6. [Control Flow](#control-flow)
7. [Functions](#functions)
8. [Enumerations](#enumerations)
9. [Arrays](#arrays)
10. [Tables (Hash Maps)](#tables-hash-maps)
11. [Type System](#type-system)
12. [Built-in Functions](#built-in-functions)
13. [Modules and Imports](#modules-and-imports)
14. [Error Handling](#error-handling)
15. [Advanced Features](#advanced-features)
16. [Language Examples](#language-examples)

---

## Language Overview

Mobius is a dynamic, multi-paradigm scripting language designed for embedding in C applications. It combines:

- **Dynamic typing** with optional type annotations
- **Multiple numeric types** for precision control
- **Reference counting** for predictable memory management
- **First-class functions** with lexical scoping
- **Tables and arrays** for data structures
- **Enumerations** for type-safe constants
- **C-like syntax** for familiarity

### Key Features

✅ **Embeddable**: Clean C API for integration  
✅ **Fast**: Bytecode VM with optimization hints  
✅ **Safe**: Comprehensive error handling  
✅ **Extensible**: Plugin system for domain-specific functionality  
✅ **Precise**: Multiple integer and float types  
✅ **Modern**: Enums, pattern matching, and advanced control flow  

---

## Lexical Structure

### Comments
```javascript
// Single-line comment

/* Multi-line comment
   spans multiple lines */
```

### Identifiers
```javascript
variable_name
functionName
MyClass
_private
```

Rules:
- Start with letter or underscore
- Contain letters, digits, underscores
- Case-sensitive
- Cannot be keywords

### Keywords
**Control Flow:**
```
if else elif while for break continue return switch case default
```

**Declarations:**
```
var func enum import
```

**Literals:**
```
true false nil
```

**Operators:**
```
and or not in is
```

**Type Annotations:**
```
int8 int16 int32 int64 uint8 uint16 uint32 uint64 float32 float64
```

**Reserved (Future Use):**
```
class this super static let const do when with yield try catch finally throw match
```

### Numeric Literals
```javascript
// Integers (default: int64)
42          // Decimal
0xFF        // Hexadecimal
0o755       // Octal
0b1010      // Binary

// Typed integers
42i8        // int8
100u16      // uint16
1000000i64  // int64

// Floating point
3.14        // double (float64)
2.5f        // float32
1.23e-4     // Scientific notation
```

### String Literals
```javascript
"Hello, world!"
"String with \"escaped\" quotes"
"String with\nnewlines"
'Single quotes also work'
```

### Character Literals
```javascript
'A'
'\n'
'\''
```

---

## Data Types

### Primitive Types

#### Nil
```javascript
var nothing = nil;
print(typeof(nothing));  // "nil"
```

#### Boolean
```javascript
var isTrue = true;
var isFalse = false;
print(typeof(isTrue));   // "bool"
```

#### Integers
```javascript
// Signed integers
var tiny: int8 = 127;       // -128 to 127
var small: int16 = 32767;   // -32,768 to 32,767  
var medium: int32 = 2000000; // ~2.1 billion range
var large: int64 = 9223372036854775807; // Very large range

// Unsigned integers  
var byte: uint8 = 255;      // 0 to 255
var word: uint16 = 65535;   // 0 to 65,535
var dword: uint32 = 4000000; // ~4.3 billion
var qword: uint64 = 18446744073709551615; // Very large positive

print(typeof(tiny));        // "integer"
```

#### Floating Point
```javascript
var single: float32 = 3.14f;  // 32-bit float
var double: float64 = 2.718;  // 64-bit double (default)

print(typeof(single));        // "float32"  
print(typeof(double));        // "float"
```

#### String
```javascript
var greeting = "Hello, Mobius!";
var name = 'World';
print(typeof(greeting));      // "string"
print(len(greeting));         // 14
```

#### Character
```javascript
var letter = 'A';
var newline = '\n';
print(typeof(letter));        // "char"
```

### Composite Types

#### Arrays
```javascript
var numbers = [1, 2, 3, 4, 5];
var mixed = [42, "hello", true, nil];
var empty = [];

print(typeof(numbers));       // "array"
print(array_length(numbers)); // 5
```

#### Tables (Hash Maps)
```javascript
var person = {
    name: "Alice",
    age: 30,
    city: "New York"
};

var computed = {
    [1 + 1]: "two",
    ["key"]: "value"
};

print(typeof(person));        // "table"
print(person.name);           // "Alice"
print(person["age"]);         // 30
```

#### Functions
```javascript
func greet(name) {
    return "Hello, " + name + "!";
}

var lambda = func(x) { return x * 2; };

print(typeof(greet));         // "function"
```

#### Enumerations
```javascript
enum Color {
    RED = 1,
    GREEN = 2, 
    BLUE = 3
}

enum Status: uint8 {
    PENDING = 0,
    RUNNING = 1,
    COMPLETE = 2
}

print(typeof(Color.RED));     // "enum"
print(Color.RED);             // Color.RED
```

---

## Variables and Constants

### Variable Declaration
```javascript
// Uninitialized variable (defaults to nil)
var x;

// Initialized variable
var y = 42;

// Multiple variables
var a = 1, b = 2, c = 3;

// Type annotations
var count: int32 = 100;
var price: float32 = 19.99f;
```

### Variable Assignment
```javascript
x = 10;
y = x + 5;
z = "Hello";

// Compound assignment
x += 5;    // x = x + 5
y -= 2;    // y = y - 2
z *= 3;    // z = z * 3
w /= 4;    // w = w / 4
```

### Scoping Rules
```javascript
var global = "I'm global";

func example() {
    var local = "I'm local";
    global = "Modified global";
    
    if (true) {
        var block_local = "Block scope";
        print(local);      // Accessible
        print(global);     // Accessible
    }
    
    // print(block_local); // Error: undefined
}
```

---

## Operators

### Arithmetic Operators
```javascript
var a = 10, b = 3;

print(a + b);    // 13 (addition)
print(a - b);    // 7  (subtraction)
print(a * b);    // 30 (multiplication)
print(a / b);    // 3.333... (division)
print(a % b);    // 1  (modulo)

print(-a);       // -10 (unary minus)
print(+a);       // 10  (unary plus)
```

### Comparison Operators
```javascript
var x = 5, y = 10;

print(x == y);   // false (equal)
print(x != y);   // true  (not equal)
print(x < y);    // true  (less than)
print(x <= y);   // true  (less or equal)
print(x > y);    // false (greater than)
print(x >= y);   // false (greater or equal)
```

### Logical Operators
```javascript
var a = true, b = false;

print(a && b);   // false (logical AND)
print(a || b);   // true  (logical OR)
print(!a);       // false (logical NOT)

// Short-circuit evaluation
print(false && expensive_function()); // expensive_function not called
print(true || expensive_function());  // expensive_function not called
```

### Bitwise Operators
```javascript
var x = 12, y = 10;  // 1100 and 1010 in binary

print(x & y);    // 8  (1000) - bitwise AND
print(x | y);    // 14 (1110) - bitwise OR  
print(x ^ y);    // 6  (0110) - bitwise XOR
print(~x);       // -13      - bitwise NOT
print(x << 1);   // 24 (11000) - left shift
print(x >> 1);   // 6  (110)   - right shift
```

### Assignment Operators
```javascript
var x = 10;

x += 5;   // x = x + 5  (15)
x -= 3;   // x = x - 3  (12)
x *= 2;   // x = x * 2  (24)
x /= 4;   // x = x / 4  (6)
```

### Increment/Decrement
```javascript
var i = 5;

i++;      // Post-increment (returns 5, then i becomes 6)
++i;      // Pre-increment (i becomes 7, returns 7)
i--;      // Post-decrement (returns 7, then i becomes 6)
--i;      // Pre-decrement (i becomes 5, returns 5)
```

### Operator Precedence
**Highest to Lowest:**
1. `()` `[]` `.` - Function calls, indexing, member access
2. `++` `--` `!` `~` `+` `-` - Unary operators
3. `*` `/` `%` - Multiplication, division, modulo
4. `+` `-` - Addition, subtraction
5. `<<` `>>` - Bitwise shifts
6. `<` `<=` `>` `>=` - Relational comparison
7. `==` `!=` - Equality comparison
8. `&` - Bitwise AND
9. `^` - Bitwise XOR
10. `|` - Bitwise OR
11. `&&` - Logical AND
12. `||` - Logical OR
13. `=` `+=` `-=` `*=` `/=` - Assignment

---

## Control Flow

### Conditional Statements

#### If-Else
```javascript
if (condition) {
    // executed if condition is true
} else if (other_condition) {
    // executed if other_condition is true
} else {
    // executed if all conditions are false
}

// Single-line if (no braces needed)
if (x > 0) print("Positive");

// Ternary-like using logical operators
var result = condition && "true_value" || "false_value";
```

#### Switch Statements
```javascript
switch (value) {
    case 1:
        print("One");
        break;
    case 2:
    case 3:
        print("Two or Three");
        break;
    default:
        print("Other");
}

// Switch with enums
enum Color { RED, GREEN, BLUE }
switch (color) {
    case Color.RED:
        print("Red color");
        break;
    case Color.GREEN:
        print("Green color");  
        break;
    default:
        print("Unknown color");
}

// Switch with ranges (if implemented)
switch (score) {
    case 90..100:
        print("A grade");
        break;
    case 80..89:
        print("B grade");
        break;
    default:
        print("Below B");
}
```

### Loops

#### While Loop
```javascript
var i = 0;
while (i < 10) {
    print(i);
    i++;
}

// Infinite loop
while (true) {
    if (should_break()) break;
    // do work
}
```

#### For Loop
```javascript
// C-style for loop
for (var i = 0; i < 10; i++) {
    print(i);
}

// For loop without initialization
var j = 0;
for (; j < 5; j++) {
    print(j);
}

// Infinite for loop
for (;;) {
    if (should_exit()) break;
    // do work
}
```

#### Loop Control
```javascript
for (var i = 0; i < 10; i++) {
    if (i == 3) continue;  // Skip iteration
    if (i == 7) break;     // Exit loop
    print(i);  // Prints: 0, 1, 2, 4, 5, 6
}

// Works in while loops too
var count = 0;
while (true) {
    count++;
    if (count % 2 == 0) continue;
    if (count > 10) break;
    print(count);  // Prints odd numbers 1, 3, 5, 7, 9
}
```

---

## Functions

### Function Declaration
```javascript
// Basic function
func greet(name) {
    return "Hello, " + name + "!";
}

// Function with multiple parameters
func add(a, b) {
    return a + b;
}

// Function with no parameters
func getCurrentTime() {
    return time();
}

// Function with no return value
func logMessage(message) {
    print("[LOG] " + message);
}
```

### Function Calls
```javascript
var greeting = greet("World");
var sum = add(5, 3);
getCurrentTime();
logMessage("System started");
```

### Function Expressions (Lambdas)
```javascript
// Anonymous function assigned to variable
var multiply = func(x, y) {
    return x * y;
};

// Function as argument
func apply_operation(a, b, operation) {
    return operation(a, b);
}

var result = apply_operation(10, 5, multiply);
var result2 = apply_operation(10, 5, func(x, y) { return x - y; });
```

### Closures
```javascript
func makeCounter() {
    var count = 0;
    return func() {
        count++;
        return count;
    };
}

var counter1 = makeCounter();
var counter2 = makeCounter();

print(counter1());  // 1
print(counter1());  // 2
print(counter2());  // 1 (independent closure)
```

### Recursion
```javascript
func factorial(n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

func fibonacci(n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

print(factorial(5));  // 120
print(fibonacci(8));  // 21
```

---

## Enumerations

### Basic Enums
```javascript
// Simple enum with automatic values
enum Direction {
    NORTH,    // 0
    SOUTH,    // 1
    EAST,     // 2
    WEST      // 3
}

// Enum with explicit values
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    SERVER_ERROR = 500
}

print(Direction.NORTH);     // Direction.NORTH
print(HttpStatus.OK);       // HttpStatus.OK
```

### Typed Enums
```javascript
// Enum with specific underlying type
enum Priority: uint8 {
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3,
    CRITICAL = 4
}

enum FileSize: uint64 {
    SMALL = 1024,
    MEDIUM = 1048576,     // 1MB
    LARGE = 1073741824    // 1GB
}
```

### Using Enums
```javascript
enum State { IDLE, RUNNING, STOPPED }

var current_state = State.IDLE;

// In switch statements
switch (current_state) {
    case State.IDLE:
        print("System is idle");
        break;
    case State.RUNNING:
        print("System is running");
        break;
    case State.STOPPED:
        print("System is stopped");
        break;
}

// Comparison
if (current_state == State.RUNNING) {
    print("Processing...");
}

// Type checking
print(typeof(State.IDLE));  // "enum"
```

---

## Arrays

### Array Creation
```javascript
// Array literals
var numbers = [1, 2, 3, 4, 5];
var mixed = [42, "hello", true, nil];
var empty = [];

// Using array_create
var dynamic = array_create();
var sized = array_create(10);  // Pre-allocate space
```

### Array Access
```javascript
var fruits = ["apple", "banana", "cherry"];

print(fruits[0]);           // "apple" (0-based indexing)
print(fruits[1]);           // "banana"
print(fruits[2]);           // "cherry"

// Negative indexing (from end)
print(fruits[-1]);          // "cherry"
print(fruits[-2]);          // "banana"

// Out of bounds returns nil
print(fruits[10]);          // nil
```

### Array Modification
```javascript
var arr = [1, 2, 3];

// Direct assignment
arr[0] = 10;               // [10, 2, 3]
arr[3] = 4;                // [10, 2, 3, 4] (extends array)

// Using array functions
array_push(arr, 5);        // [10, 2, 3, 4, 5]
var last = array_pop(arr); // last = 5, arr = [10, 2, 3, 4]

array_set(arr, 1, 20);     // [10, 20, 3, 4]
var val = array_get(arr, 1); // val = 20
```

### Array Operations
```javascript
var arr1 = [1, 2, 3];
var arr2 = [4, 5, 6];

// Length
print(array_length(arr1)); // 3

// Concatenation
var combined = array_concat(arr1, arr2); // [1, 2, 3, 4, 5, 6]

// Slicing
var subset = array_slice(arr1, 1, 2);    // [2, 3] (from index 1, length 2)

// Reverse
array_reverse(arr1);       // [3, 2, 1] (modifies original)

// Find
var index = array_find(arr2, 5);         // 1 (index of first occurrence)
```

### Array iteration
```javascript
var numbers = [1, 2, 3, 4, 5];

// Manual iteration
for (var i = 0; i < array_length(numbers); i++) {
    print(numbers[i]);
}

// Using array_get for safety
for (var i = 0; i < array_length(numbers); i++) {
    var value = array_get(numbers, i);
    if (value != nil) {
        print(value);
    }
}
```

---

## Tables (Hash Maps)

### Table Creation
```javascript
// Table literals
var person = {
    name: "Alice",
    age: 30,
    city: "New York"
};

// Empty table
var empty = {};

// Computed keys
var config = {
    ["debug_" + "mode"]: true,
    [1 + 1]: "two"
};
```

### Table Access
```javascript
var user = { name: "Bob", age: 25 };

// Dot notation
print(user.name);          // "Bob"
user.age = 26;

// Bracket notation
print(user["name"]);       // "Bob" 
user["location"] = "Paris";

// Dynamic keys
var key = "age";
print(user[key]);          // 26
```

### Table Operations
```javascript
var data = { x: 10, y: 20 };

// Insert/update
table_insert(data, "z", 30);     // {x: 10, y: 20, z: 30}
data.w = 40;                     // {x: 10, y: 20, z: 30, w: 40}

// Remove
table_remove(data, "y");         // {x: 10, z: 30, w: 40}

// Check existence
if (table_has_key(data, "x")) {
    print("x exists");
}

// Size
print(table_size(data));         // 3
```

### Table Iteration
```javascript
var inventory = {
    apples: 10,
    bananas: 5,
    oranges: 8
};

// Using pairs (returns key-value pairs)
var pairs_table = pairs(inventory);
for (var i = 0; i < array_length(pairs_table); i++) {
    var pair = pairs_table[i];
    print("Key: " + pair.key + ", Value: " + pair.value);
}
```

### Metatables
```javascript
var mt = {
    __add: func(a, b) {
        return a.value + b.value;
    }
};

var obj1 = { value: 10 };
var obj2 = { value: 20 };

setmetatable(obj1, mt);
setmetatable(obj2, mt);

// Now obj1 + obj2 calls mt.__add
var result = obj1 + obj2;  // 30
```

---

## Type System

### Type Annotations
```javascript
// Variable type annotations
var count: int32 = 100;
var price: float32 = 19.99f;
var name: string = "Product";

// Function parameter and return type annotations
func calculate(x: float64, y: float64): float64 {
    return x * y + 1.0;
}

// Array and table type hints
var numbers: array = [1, 2, 3];
var config: table = { debug: true };
```

### Type Configuration
```javascript
// Enable strict type checking
set_strict_types(true);

// Enable type conversion warnings
set_type_warnings(true);

// Get current type configuration
var config = get_type_config();
print("Strict mode:", config.strict_mode);
print("Warnings:", config.warn_on_conversion);
```

### Type Checking Functions
```javascript
// Runtime type checking
print(typeof(42));           // "integer"
print(typeof("hello"));      // "string"
print(typeof(true));         // "bool"
print(typeof(nil));          // "nil"
print(typeof([1, 2, 3]));    // "array"
print(typeof({x: 1}));       // "table"

// Type conversion
var num = int("123");        // Convert string to integer
var text = str(456);         // Convert integer to string
var decimal = float("3.14"); // Convert string to float
```

---

## Built-in Functions

### Core Functions (5)

#### `print(...args)`
Print values to stdout with spaces between arguments.
```javascript
print("Hello");              // Hello
print("Count:", 42);         // Count: 42
print(1, 2, 3);             // 1 2 3
```

#### `typeof(value)`
Return the type of a value as a string.
```javascript
print(typeof(42));          // "integer"
print(typeof("text"));      // "string"
print(typeof(true));        // "bool"
```

#### `str(value)`
Convert any value to a string representation.
```javascript
print(str(42));             // "42"
print(str(true));           // "true"
print(str(nil));            // "nil"
```

#### `int(value)`
Convert a value to an integer.
```javascript
print(int("123"));          // 123
print(int(3.14));           // 3
print(int(true));           // 1
```

#### `float(value)`
Convert a value to a floating-point number.
```javascript
print(float("3.14"));       // 3.14
print(float(42));           // 42.0
print(float(true));         // 1.0
```

### Math Functions (8)

#### `abs(x)`
Return the absolute value of x.
```javascript
print(abs(-5));             // 5
print(abs(3.14));           // 3.14
```

#### `min(...args)`
Return the minimum value from arguments.
```javascript
print(min(1, 2, 3));        // 1
print(min(-5, 0, 10));      // -5
```

#### `max(...args)`
Return the maximum value from arguments.
```javascript
print(max(1, 2, 3));        // 3
print(max(-5, 0, 10));      // 10
```

#### `pow(base, exponent)`
Return base raised to the power of exponent.
```javascript
print(pow(2, 3));           // 8
print(pow(4, 0.5));         // 2.0 (square root)
```

#### `sqrt(x)`
Return the square root of x.
```javascript
print(sqrt(16));            // 4.0
print(sqrt(2));             // 1.414...
```

#### `floor(x)`
Return the largest integer less than or equal to x.
```javascript
print(floor(3.7));          // 3.0
print(floor(-2.3));         // -3.0
```

#### `ceil(x)`
Return the smallest integer greater than or equal to x.
```javascript
print(ceil(3.2));           // 4.0
print(ceil(-2.7));          // -2.0
```

#### `round(x)`
Return x rounded to the nearest integer.
```javascript
print(round(3.4));          // 3.0
print(round(3.6));          // 4.0
```

### String Functions (6)

#### `len(string)`
Return the length of a string.
```javascript
print(len("hello"));        // 5
print(len(""));             // 0
```

#### `substr(string, start, [length])`
Extract a substring starting at start with optional length.
```javascript
print(substr("hello", 1, 3));     // "ell"
print(substr("world", 2));        // "rld"
```

#### `concat(...strings)`
Concatenate multiple strings.
```javascript
print(concat("Hello", " ", "World"));  // "Hello World"
print(concat("a", "b", "c"));          // "abc"
```

#### `upper(string)`
Convert string to uppercase.
```javascript
print(upper("hello"));      // "HELLO"
```

#### `lower(string)`
Convert string to lowercase.
```javascript
print(lower("WORLD"));      // "world"
```

#### `contains(string, substring)`
Check if string contains substring.
```javascript
print(contains("hello world", "world"));  // true
print(contains("test", "xyz"));           // false
```

### Array Functions (10)

#### `array_create([size])`
Create a new empty array with optional initial size.
```javascript
var arr1 = array_create();     // Empty array
var arr2 = array_create(10);   // Pre-allocated for 10 elements
```

#### `array_push(array, value)`
Add a value to the end of the array.
```javascript
var arr = [1, 2];
array_push(arr, 3);         // arr is now [1, 2, 3]
```

#### `array_pop(array)`
Remove and return the last element from the array.
```javascript
var arr = [1, 2, 3];
var last = array_pop(arr);  // last = 3, arr = [1, 2]
```

#### `array_get(array, index)`
Get element at index (safe, returns nil if out of bounds).
```javascript
var arr = [10, 20, 30];
print(array_get(arr, 1));   // 20
print(array_get(arr, 10));  // nil
```

#### `array_set(array, index, value)`
Set element at index (extends array if needed).
```javascript
var arr = [1, 2];
array_set(arr, 2, 3);       // arr is now [1, 2, 3]
```

#### `array_length(array)`
Return the number of elements in the array.
```javascript
var arr = [1, 2, 3, 4];
print(array_length(arr));   // 4
```

#### `array_slice(array, start, length)`
Extract a portion of the array.
```javascript
var arr = [1, 2, 3, 4, 5];
var sub = array_slice(arr, 1, 3);  // [2, 3, 4]
```

#### `array_concat(array1, array2)`
Concatenate two arrays into a new array.
```javascript
var arr1 = [1, 2];
var arr2 = [3, 4];
var combined = array_concat(arr1, arr2);  // [1, 2, 3, 4]
```

#### `array_reverse(array)`
Reverse the elements in the array (modifies original).
```javascript
var arr = [1, 2, 3];
array_reverse(arr);          // arr is now [3, 2, 1]
```

#### `array_find(array, value)`
Find the index of the first occurrence of value.
```javascript
var arr = [10, 20, 30, 20];
print(array_find(arr, 20)); // 1 (first occurrence)
print(array_find(arr, 99)); // -1 (not found)
```

### Table Functions (7)

#### `table_insert(table, key, value)`
Insert or update a key-value pair in the table.
```javascript
var t = {};
table_insert(t, "name", "Alice");
print(t.name);               // "Alice"
```

#### `table_remove(table, key)`
Remove a key-value pair from the table.
```javascript
var t = {x: 1, y: 2};
table_remove(t, "x");        // t is now {y: 2}
```

#### `table_has_key(table, key)`
Check if a table contains a specific key.
```javascript
var t = {name: "Bob"};
print(table_has_key(t, "name"));  // true
print(table_has_key(t, "age"));   // false
```

#### `table_size(table)`
Return the number of key-value pairs in the table.
```javascript
var t = {a: 1, b: 2, c: 3};
print(table_size(t));       // 3
```

#### `setmetatable(table, metatable)`
Set a metatable for the table.
```javascript
var mt = { __add: func(a, b) { return a.x + b.x; } };
var obj = {x: 5};
setmetatable(obj, mt);
```

#### `getmetatable(table)`
Get the metatable of a table.
```javascript
var mt = getmetatable(obj);
if (mt != nil) {
    print("Table has metatable");
}
```

#### `pairs(table)`
Return an array of key-value pairs for iteration.
```javascript
var t = {name: "Alice", age: 30};
var pairs_array = pairs(t);
// pairs_array contains objects with .key and .value properties
```

### Utility Functions (3)

#### `random([min, max])`
Generate random numbers.
```javascript
print(random());             // Random float 0.0 to 1.0
print(random(10));           // Random integer 0 to 9
print(random(5, 15));        // Random integer 5 to 14
```

#### `time()`
Return current Unix timestamp.
```javascript
var now = time();
print("Current time:", now);
```

#### `clock()`
Return program execution time in seconds.
```javascript
var start = clock();
// ... do work ...
var elapsed = clock() - start;
print("Execution time:", elapsed, "seconds");
```

### File I/O Functions (1)

#### `load(filename)`
Load and execute a Mobius script file.
```javascript
load("config.mob");          // Execute config.mob
load("utils.mob");           // Load utility functions
```

### Type System Functions (3)

#### `set_strict_types(enabled)`
Enable or disable strict type checking.
```javascript
set_strict_types(true);      // Enable strict mode
set_strict_types(false);     // Disable strict mode
```

#### `set_type_warnings(enabled)`
Enable or disable type conversion warnings.
```javascript
set_type_warnings(true);     // Show warnings
set_type_warnings(false);    // Hide warnings
```

#### `get_type_config()`
Return current type system configuration.
```javascript
var config = get_type_config();
print("Strict:", config.strict_mode);
print("Warnings:", config.warn_on_conversion);
```

---

## Modules and Imports

### Loading Scripts
```javascript
// Load and execute another script
load("utils.mob");

// Functions and variables from utils.mob are now available
// (assuming they were defined globally)
```

### Script Organization
**main.mob:**
```javascript
load("math_utils.mob");
load("string_utils.mob");

var result = advanced_calculation(10, 20);
var formatted = format_number(result);
print(formatted);
```

**math_utils.mob:**
```javascript
func advanced_calculation(a, b) {
    return pow(a, 2) + sqrt(b);
}

func factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
```

**string_utils.mob:**
```javascript
func format_number(num) {
    return "Result: " + str(num);
}

func repeat_string(str, count) {
    var result = "";
    for (var i = 0; i < count; i++) {
        result = result + str;
    }
    return result;
}
```

---

## Error Handling

### Runtime Errors
```javascript
// Division by zero
var x = 5 / 0;               // Runtime error

// Undefined variable
print(undefined_var);        // Runtime error

// Array out of bounds (returns nil, no error)
var arr = [1, 2, 3];
print(arr[10]);              // nil (safe)

// Type errors in strict mode
set_strict_types(true);
var num: int32 = "string";   // Type error in strict mode
```

### Error Messages
When errors occur, Mobius provides detailed information:
- Error type and message
- Line and column numbers
- Suggestions for fixes
- Stack trace for function calls

Example error output:
```
━━━ Division Error ━━━
  at line 5, column 11

  Division by zero

  💡 Suggestion: Check that the divisor is not zero before performing division
```

---

## Advanced Features

### Optional Semicolons
Semicolons are optional in most cases but required when multiple statements are on the same line:

```javascript
// Optional semicolons
var x = 10
var y = 20
print(x + y)

// Required for multiple statements per line
var a = 1; var b = 2; print(a + b)

// C-style for loops always need semicolons
for (var i = 0; i < 10; i++) {
    print(i)
}
```

### Type Annotations
Optional type hints for better documentation and error checking:

```javascript
// Variable annotations
var count: int32 = 0;
var name: string = "Mobius";

// Function annotations
func calculate(x: float64, y: float64): float64 {
    return x * y + 1.0;
}

// Enum with type
enum Status: uint8 {
    INACTIVE = 0,
    ACTIVE = 1
}
```

### Metaprogramming
Limited metaprogramming through metatables:

```javascript
// Custom operators via metatables
var Vector = {
    __add: func(a, b) {
        return {x: a.x + b.x, y: a.y + b.y};
    },
    __sub: func(a, b) {
        return {x: a.x - b.x, y: a.y - b.y};
    }
};

var v1 = {x: 1, y: 2};
var v2 = {x: 3, y: 4};
setmetatable(v1, Vector);
setmetatable(v2, Vector);

var v3 = v1 + v2;  // {x: 4, y: 6}
```

---

## Language Examples

### Calculator Program
```javascript
func calculator() {
    print("Simple Calculator");
    print("Enter expressions like: 5 + 3");
    
    var history = [];
    
    while (true) {
        print("Enter calculation (or 'quit' to exit):");
        // In a real implementation, you'd read input here
        
        // Example calculations
        var operations = [
            {a: 10, op: "+", b: 5},
            {a: 20, op: "*", b: 3},
            {a: 15, op: "/", b: 3}
        ];
        
        for (var i = 0; i < array_length(operations); i++) {
            var calc = operations[i];
            var result;
            
            switch (calc.op) {
                case "+":
                    result = calc.a + calc.b;
                    break;
                case "-":
                    result = calc.a - calc.b;
                    break;
                case "*":
                    result = calc.a * calc.b;
                    break;
                case "/":
                    if (calc.b != 0) {
                        result = calc.a / calc.b;
                    } else {
                        print("Error: Division by zero");
                        continue;
                    }
                    break;
                default:
                    print("Unknown operator:", calc.op);
                    continue;
            }
            
            var expr = str(calc.a) + " " + calc.op + " " + str(calc.b) + " = " + str(result);
            print(expr);
            array_push(history, expr);
        }
        
        break; // End demo
    }
    
    print("Calculation history:");
    for (var i = 0; i < array_length(history); i++) {
        print((i + 1) + ". " + history[i]);
    }
}

calculator();
```

### Data Processing Example
```javascript
// Employee management system
enum Department: uint8 {
    ENGINEERING = 1,
    MARKETING = 2,
    SALES = 3,
    HR = 4
}

func create_employee(name, dept, salary) {
    return {
        name: name,
        department: dept,
        salary: salary,
        hire_date: time()
    };
}

func main() {
    var employees = [];
    
    // Add employees
    array_push(employees, create_employee("Alice", Department.ENGINEERING, 75000));
    array_push(employees, create_employee("Bob", Department.MARKETING, 55000));
    array_push(employees, create_employee("Carol", Department.ENGINEERING, 80000));
    array_push(employees, create_employee("Dave", Department.SALES, 60000));
    
    print("Company Employees:");
    print("==================");
    
    var total_salary = 0;
    var eng_count = 0;
    
    for (var i = 0; i < array_length(employees); i++) {
        var emp = employees[i];
        var dept_name;
        
        switch (emp.department) {
            case Department.ENGINEERING:
                dept_name = "Engineering";
                eng_count++;
                break;
            case Department.MARKETING:
                dept_name = "Marketing";
                break;
            case Department.SALES:
                dept_name = "Sales";
                break;
            case Department.HR:
                dept_name = "HR";
                break;
            default:
                dept_name = "Unknown";
        }
        
        print(emp.name + " - " + dept_name + " - $" + str(emp.salary));
        total_salary += emp.salary;
    }
    
    print("\nSummary:");
    print("Total employees: " + str(array_length(employees)));
    print("Engineering employees: " + str(eng_count));
    print("Total salary cost: $" + str(total_salary));
    print("Average salary: $" + str(total_salary / array_length(employees)));
}

main();
```

### Game Logic Example
```javascript
enum GameState { MENU, PLAYING, PAUSED, GAME_OVER }

var game = {
    state: GameState.MENU,
    score: 0,
    level: 1,
    player: {
        x: 0,
        y: 0,
        health: 100
    }
};

func update_game() {
    switch (game.state) {
        case GameState.MENU:
            print("Press any key to start...");
            game.state = GameState.PLAYING;
            break;
            
        case GameState.PLAYING:
            // Game logic
            game.player.x += 1;
            game.score += 10;
            
            if (game.score > 1000) {
                game.level++;
                game.score = 0;
                print("Level up! Now at level " + str(game.level));
            }
            
            if (game.player.health <= 0) {
                game.state = GameState.GAME_OVER;
            }
            break;
            
        case GameState.PAUSED:
            print("Game paused. Press resume to continue.");
            break;
            
        case GameState.GAME_OVER:
            print("Game Over! Final score: " + str(game.score));
            print("Final level: " + str(game.level));
            break;
    }
}

// Simulate game loop
for (var frame = 0; frame < 10; frame++) {
    print("Frame " + str(frame + 1) + ":");
    update_game();
    print("Player position: (" + str(game.player.x) + ", " + str(game.player.y) + ")");
    print("Score: " + str(game.score) + ", Level: " + str(game.level));
    print("---");
}
```

---

## Conclusion

This reference covers all currently implemented features in Mobius 0.1.0. The language provides a solid foundation for scripting with:

- **Rich type system** supporting multiple integer and float types
- **Modern language features** like enums and optional semicolons  
- **Comprehensive built-in library** with 36+ functions
- **Flexible data structures** (arrays and tables)
- **C-like syntax** for ease of adoption
- **Embeddable design** for integration into applications

For embedding Mobius in your C applications, see the [Embedding Guide](embedding_guide.md).

For practical examples, explore the [Examples Directory](../examples/).

For implementation details, see the [Design Documents](design/).

**Happy scripting with Mobius!** 🚀