# Mobius Language Reference

**Version:** 0.1.0  
**Date:** 2024  
**Authors:** Mobius Development Team

This document provides a complete reference for the Mobius scripting language, including syntax, built-in standard library, and available extensions.

## Table of Contents

1. [Language Overview](#language-overview)
2. [Syntax Reference](#syntax-reference)
3. [Data Types](#data-types)
4. [Operators](#operators)
5. [Control Flow](#control-flow)
6. [Functions](#functions)
7. [Standard Library](#standard-library)
8. [Math Extension Library](#math-extension-library)
9. [Text Processing Extension](#text-processing-extension)
10. [Error Handling](#error-handling)
11. [Examples](#examples)

---

## Language Overview

Mobius is a dynamically-typed, interpreted scripting language designed for embedding in C applications. It features:

- **Dynamic typing** with runtime type checking
- **First-class functions** with lexical scoping and closures
- **Multiple numeric types** (8/16/32/64-bit integers, 64-bit floats)
- **Comprehensive standard library** built-in
- **Extensible plugin system** for domain-specific functionality
- **C-like syntax** familiar to many developers

### Design Goals

- **Embeddability**: Easy to integrate into C applications
- **Simplicity**: Clean, readable syntax
- **Performance**: Efficient interpretation and memory usage
- **Extensibility**: Plugin system for custom functionality
- **Safety**: Comprehensive error handling and validation

---

## Syntax Reference

### Comments

```javascript
// Single-line comment
/* Multi-line comment */
```

### Variable Declarations

```javascript
var name = "Alice";           // String variable
var age = 25;                 // Integer variable
var height = 5.8;             // Float variable
var is_student = true;        // Boolean variable
var nothing = nil;            // Nil value
```

### Identifiers

- Must start with a letter or underscore
- Can contain letters, numbers, and underscores
- Case-sensitive
- Cannot be keywords

**Valid identifiers:**
```javascript
var myVariable = 10;
var _private = true;
var user2 = "bob";
var MAX_SIZE = 100;
```

### Keywords

Reserved words that cannot be used as identifiers:

```
and     class   else    false   func    for     if      nil
or      return  super   this    true    var     while   const
```

### String Literals

```javascript
var simple = "Hello, World!";
var with_quotes = "He said \"Hello\"";
var multiline = "Line 1\nLine 2\nLine 3";
```

**Escape sequences:**
- `\"` - Double quote
- `\\` - Backslash
- `\n` - Newline
- `\t` - Tab
- `\r` - Carriage return

### Numeric Literals

```javascript
// Integer literals
var decimal = 42;
var negative = -123;

// Float literals
var pi = 3.14159;
var scientific = 1.23e-4;
var large = 1.0e6;
```

### Boolean and Nil Literals

```javascript
var truth = true;
var falsehood = false;
var empty = nil;
```

---

## Data Types

### Integer Types

Mobius supports multiple integer types with explicit size control:

| Type | Size | Range | Example |
|------|------|-------|---------|
| `int8` | 8-bit signed | -128 to 127 | `var small = 100;` |
| `uint8` | 8-bit unsigned | 0 to 255 | `var byte_val = 255;` |
| `int16` | 16-bit signed | -32,768 to 32,767 | `var medium = 30000;` |
| `uint16` | 16-bit unsigned | 0 to 65,535 | `var port = 8080;` |
| `int32` | 32-bit signed | -2³¹ to 2³¹-1 | `var count = 1000000;` |
| `uint32` | 32-bit unsigned | 0 to 2³²-1 | `var large = 4000000000;` |
| `int64` | 64-bit signed | -2⁶³ to 2⁶³-1 | `var huge = 9223372036854775807;` |
| `uint64` | 64-bit unsigned | 0 to 2⁶⁴-1 | `var massive = 18446744073709551615;` |

### Float Type

- **64-bit double precision** floating-point numbers
- IEEE 754 standard representation
- Range: approximately ±1.7×10³⁰⁸

```javascript
var pi = 3.141592653589793;
var small = 1.23e-10;
var large = 9.87e20;
```

### String Type

- **UTF-8 encoded** text strings
- **Dynamically sized** with automatic memory management
- **Immutable** - operations create new strings

```javascript
var greeting = "Hello, World!";
var empty = "";
var unicode = "Hello, 世界! 🌍";
```

### Boolean Type

- Two values: `true` and `false`
- Used in conditional expressions and logical operations

```javascript
var is_valid = true;
var is_empty = false;
```

### Nil Type

- Represents the absence of a value
- Single value: `nil`
- Default value for uninitialized variables

```javascript
var uninitialized = nil;
```

### Function Type

- **First-class values** that can be stored in variables
- Support **closures** and **lexical scoping**
- Can be passed as arguments and returned from functions

```javascript
func add(a, b) {
    return a + b;
}

var operation = add;  // Store function in variable
var result = operation(5, 3);  // Call through variable
```

---

## Operators

### Arithmetic Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `+` | Addition | `5 + 3` | `8` |
| `-` | Subtraction | `5 - 3` | `2` |
| `*` | Multiplication | `5 * 3` | `15` |
| `/` | Division | `15 / 3` | `5` |
| `-` | Unary negation | `-5` | `-5` |

### Comparison Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `==` | Equal to | `5 == 5` | `true` |
| `!=` | Not equal to | `5 != 3` | `true` |
| `<` | Less than | `3 < 5` | `true` |
| `<=` | Less than or equal | `3 <= 5` | `true` |
| `>` | Greater than | `5 > 3` | `true` |
| `>=` | Greater than or equal | `5 >= 3` | `true` |

### Logical Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `and` | Logical AND | `true and false` | `false` |
| `or` | Logical OR | `true or false` | `true` |
| `!` | Logical NOT | `!true` | `false` |

### Operator Precedence

From highest to lowest precedence:

1. **Unary operators**: `!`, `-` (unary)
2. **Multiplication/Division**: `*`, `/`
3. **Addition/Subtraction**: `+`, `-`
4. **Comparison**: `<`, `<=`, `>`, `>=`
5. **Equality**: `==`, `!=`
6. **Logical AND**: `and`
7. **Logical OR**: `or`

---

## Control Flow

### Conditional Statements

#### If Statement

```javascript
if (condition) {
    // Execute if condition is true
}

if (age >= 18) {
    print("You are an adult");
}
```

#### If-Else Statement

```javascript
if (condition) {
    // Execute if condition is true
} else {
    // Execute if condition is false
}

if (score >= 60) {
    print("Pass");
} else {
    print("Fail");
}
```

#### If-Else-If Chain

```javascript
if (grade >= 90) {
    print("A");
} else if (grade >= 80) {
    print("B");
} else if (grade >= 70) {
    print("C");
} else {
    print("F");
}
```

### Loops

#### While Loop

```javascript
while (condition) {
    // Execute while condition is true
}

var i = 0;
while (i < 5) {
    print(i);
    i = i + 1;
}
```

#### For Loop

```javascript
for (initialization; condition; increment) {
    // Execute while condition is true
}

for (var i = 0; i < 5; i = i + 1) {
    print(i);
}
```

### Truthiness

Values are considered "truthy" or "falsy" in conditional contexts:

**Falsy values:**
- `false`
- `nil`
- `0` (integer zero)
- `0.0` (float zero)
- `""` (empty string)

**Truthy values:**
- `true`
- Any non-zero number
- Any non-empty string
- Any function

---

## Functions

### Function Declaration

```javascript
func function_name(parameter1, parameter2, ...) {
    // Function body
    return value;  // Optional return statement
}
```

### Basic Function Example

```javascript
func greet(name) {
    return "Hello, " + name + "!";
}

var message = greet("Alice");
print(message);  // Output: Hello, Alice!
```

### Functions with Multiple Parameters

```javascript
func calculate_area(width, height) {
    return width * height;
}

var area = calculate_area(10, 5);
print("Area:", area);  // Output: Area: 50
```

### Functions without Return Value

```javascript
func print_info(name, age) {
    print("Name:", name);
    print("Age:", age);
}

print_info("Bob", 25);
```

### Recursive Functions

```javascript
func factorial(n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

print("5! =", factorial(5));  // Output: 5! = 120
```

### Closures and Lexical Scoping

```javascript
func make_counter() {
    var count = 0;
    
    func increment() {
        count = count + 1;
        return count;
    }
    
    return increment;
}

var counter = make_counter();
print(counter());  // Output: 1
print(counter());  // Output: 2
print(counter());  // Output: 3
```

### First-Class Functions

```javascript
func add(a, b) { return a + b; }
func multiply(a, b) { return a * b; }

func apply_operation(operation, x, y) {
    return operation(x, y);
}

var sum = apply_operation(add, 5, 3);        // 8
var product = apply_operation(multiply, 5, 3); // 15
```

---

## Standard Library

The Mobius standard library provides essential functions that are always available without loading any plugins.

### Core Functions

#### `print(...)`
**Purpose:** Output values to the console  
**Parameters:** Any number of values  
**Returns:** `nil`  

```javascript
print("Hello, World!");
print("Name:", "Alice", "Age:", 25);
print();  // Print empty line
```

#### `typeof(value)`
**Purpose:** Get the type of a value  
**Parameters:** Any value  
**Returns:** String representing the type  

```javascript
print(typeof(42));          // "integer"
print(typeof(3.14));        // "float"
print(typeof("hello"));     // "string"
print(typeof(true));        // "boolean"
print(typeof(nil));         // "nil"
print(typeof(print));       // "function"
```

### Type Conversion Functions

#### `str(value)`
**Purpose:** Convert value to string  
**Parameters:** Any value  
**Returns:** String representation  

```javascript
var text = str(42);         // "42"
var float_text = str(3.14); // "3.14"
var bool_text = str(true);  // "true"
```

#### `int(value)`
**Purpose:** Convert value to integer  
**Parameters:** Number or string  
**Returns:** Integer value  

```javascript
var num = int("42");        // 42
var from_float = int(3.14); // 3
var from_bool = int(true);  // 1
```

#### `float(value)`
**Purpose:** Convert value to float  
**Parameters:** Number or string  
**Returns:** Float value  

```javascript
var decimal = float("3.14");  // 3.14
var from_int = float(42);     // 42.0
```

### Mathematical Functions

#### `abs(number)`
**Purpose:** Absolute value  
**Parameters:** Numeric value  
**Returns:** Absolute value  

```javascript
print(abs(-5));    // 5
print(abs(3.14));  // 3.14
print(abs(0));     // 0
```

#### `min(a, b, ...)`
**Purpose:** Find minimum value  
**Parameters:** Two or more numeric values  
**Returns:** Smallest value  

```javascript
print(min(5, 3));        // 3
print(min(1, 7, 3, 9));  // 1
```

#### `max(a, b, ...)`
**Purpose:** Find maximum value  
**Parameters:** Two or more numeric values  
**Returns:** Largest value  

```javascript
print(max(5, 3));        // 5
print(max(1, 7, 3, 9));  // 9
```

#### `pow(base, exponent)`
**Purpose:** Power function  
**Parameters:** Base and exponent (numeric)  
**Returns:** base^exponent  

```javascript
print(pow(2, 3));    // 8
print(pow(3, 2));    // 9
print(pow(2, 0.5));  // 1.414... (square root of 2)
```

#### `sqrt(number)`
**Purpose:** Square root  
**Parameters:** Non-negative numeric value  
**Returns:** Square root  

```javascript
print(sqrt(16));   // 4
print(sqrt(2));    // 1.414...
print(sqrt(0));    // 0
```

#### `floor(number)`
**Purpose:** Round down to nearest integer  
**Parameters:** Numeric value  
**Returns:** Floor value  

```javascript
print(floor(3.7));   // 3
print(floor(-2.3));  // -3
```

#### `ceil(number)`
**Purpose:** Round up to nearest integer  
**Parameters:** Numeric value  
**Returns:** Ceiling value  

```javascript
print(ceil(3.2));   // 4
print(ceil(-2.7));  // -2
```

#### `round(number)`
**Purpose:** Round to nearest integer  
**Parameters:** Numeric value  
**Returns:** Rounded value  

```javascript
print(round(3.7));   // 4
print(round(3.2));   // 3
print(round(-2.6));  // -3
```

### String Functions

#### `len(string)`
**Purpose:** Get string length  
**Parameters:** String value  
**Returns:** Length as integer  

```javascript
print(len("hello"));     // 5
print(len(""));          // 0
print(len("世界"));      // 2 (Unicode characters)
```

#### `substr(string, start, length)`
**Purpose:** Extract substring  
**Parameters:** String, start index, length  
**Returns:** Substring  

```javascript
var text = "Hello, World!";
print(substr(text, 0, 5));    // "Hello"
print(substr(text, 7, 5));    // "World"
```

#### `concat(string1, string2, ...)`
**Purpose:** Concatenate strings  
**Parameters:** Two or more strings  
**Returns:** Combined string  

```javascript
var full = concat("Hello", " ", "World", "!");
print(full);  // "Hello World!"
```

#### `upper(string)`
**Purpose:** Convert to uppercase  
**Parameters:** String value  
**Returns:** Uppercase string  

```javascript
print(upper("hello"));  // "HELLO"
print(upper("World"));  // "WORLD"
```

#### `lower(string)`
**Purpose:** Convert to lowercase  
**Parameters:** String value  
**Returns:** Lowercase string  

```javascript
print(lower("HELLO"));  // "hello"
print(lower("World"));  // "world"
```

#### `contains(string, substring)`
**Purpose:** Check if string contains substring  
**Parameters:** String to search, substring to find  
**Returns:** Boolean result  

```javascript
var text = "Hello, World!";
print(contains(text, "World"));   // true
print(contains(text, "world"));   // false (case-sensitive)
print(contains(text, "xyz"));     // false
```

### Utility Functions

#### `random()`
**Purpose:** Generate random number  
**Parameters:** None  
**Returns:** Random float between 0.0 and 1.0  

```javascript
var rand = random();
print("Random:", rand);  // Random: 0.742...

// Generate random integer between 1 and 10
var dice = int(random() * 10) + 1;
```

#### `time()`
**Purpose:** Get current Unix timestamp  
**Parameters:** None  
**Returns:** Current time as integer seconds since epoch  

```javascript
var now = time();
print("Current time:", now);  // Current time: 1640995200
```

#### `clock()`
**Purpose:** Get high-resolution time  
**Parameters:** None  
**Returns:** Time in milliseconds as float  

```javascript
var start = clock();
// ... some operations ...
var end = clock();
print("Elapsed:", end - start, "ms");
```

---

## Math Extension Library

The Math extension provides advanced mathematical functions beyond the standard library. It's automatically loaded if available.

### Trigonometric Functions

#### `sin(radians)`
**Purpose:** Sine function  
**Parameters:** Angle in radians  
**Returns:** Sine value  

```javascript
print(sin(0));          // 0
print(sin(pi() / 2));   // 1
print(sin(pi()));       // 0 (approximately)
```

#### `cos(radians)`
**Purpose:** Cosine function  
**Parameters:** Angle in radians  
**Returns:** Cosine value  

```javascript
print(cos(0));          // 1
print(cos(pi() / 2));   // 0 (approximately)
print(cos(pi()));       // -1
```

#### `tan(radians)`
**Purpose:** Tangent function  
**Parameters:** Angle in radians  
**Returns:** Tangent value  

```javascript
print(tan(0));          // 0
print(tan(pi() / 4));   // 1
```

#### `asin(value)`
**Purpose:** Arc sine function  
**Parameters:** Value between -1 and 1  
**Returns:** Angle in radians  

```javascript
print(asin(0));    // 0
print(asin(1));    // π/2 (approximately 1.57)
print(asin(-1));   // -π/2
```

#### `acos(value)`
**Purpose:** Arc cosine function  
**Parameters:** Value between -1 and 1  
**Returns:** Angle in radians  

```javascript
print(acos(1));    // 0
print(acos(0));    // π/2
print(acos(-1));   // π
```

#### `atan(value)`
**Purpose:** Arc tangent function  
**Parameters:** Any numeric value  
**Returns:** Angle in radians  

```javascript
print(atan(0));    // 0
print(atan(1));    // π/4 (approximately 0.785)
```

#### `atan2(y, x)`
**Purpose:** Two-argument arc tangent  
**Parameters:** Y and X coordinates  
**Returns:** Angle in radians  

```javascript
print(atan2(1, 1));     // π/4 (45 degrees)
print(atan2(1, 0));     // π/2 (90 degrees)
print(atan2(-1, -1));   // -3π/4 (-135 degrees)
```

### Logarithmic and Exponential Functions

#### `log(value)`
**Purpose:** Natural logarithm  
**Parameters:** Positive numeric value  
**Returns:** Natural log  

```javascript
print(log(e()));     // 1
print(log(1));       // 0
print(log(10));      // 2.302...
```

#### `log10(value)`
**Purpose:** Base-10 logarithm  
**Parameters:** Positive numeric value  
**Returns:** Log base 10  

```javascript
print(log10(10));     // 1
print(log10(100));    // 2
print(log10(1000));   // 3
```

#### `exp(value)`
**Purpose:** Exponential function (e^x)  
**Parameters:** Numeric value  
**Returns:** e raised to the power of value  

```javascript
print(exp(0));    // 1
print(exp(1));    // e (approximately 2.718)
print(exp(2));    // e² (approximately 7.389)
```

### Hyperbolic Functions

#### `sinh(value)`
**Purpose:** Hyperbolic sine  
**Parameters:** Numeric value  
**Returns:** Hyperbolic sine  

```javascript
print(sinh(0));    // 0
print(sinh(1));    // 1.175...
```

#### `cosh(value)`
**Purpose:** Hyperbolic cosine  
**Parameters:** Numeric value  
**Returns:** Hyperbolic cosine  

```javascript
print(cosh(0));    // 1
print(cosh(1));    // 1.543...
```

#### `tanh(value)`
**Purpose:** Hyperbolic tangent  
**Parameters:** Numeric value  
**Returns:** Hyperbolic tangent  

```javascript
print(tanh(0));    // 0
print(tanh(1));    // 0.761...
```

### Advanced Mathematical Functions

#### `factorial(n)`
**Purpose:** Factorial function  
**Parameters:** Non-negative integer (max 20)  
**Returns:** n! as float  

```javascript
print(factorial(0));    // 1
print(factorial(5));    // 120
print(factorial(10));   // 3628800
```

#### `gcd(a, b)`
**Purpose:** Greatest common divisor  
**Parameters:** Two integers  
**Returns:** GCD as integer  

```javascript
print(gcd(12, 8));     // 4
print(gcd(17, 13));    // 1
print(gcd(100, 25));   // 25
```

#### `lcm(a, b)`
**Purpose:** Least common multiple  
**Parameters:** Two integers  
**Returns:** LCM as integer  

```javascript
print(lcm(4, 6));      // 12
print(lcm(12, 8));     // 24
print(lcm(7, 3));      // 21
```

### Mathematical Constants

#### `pi()`
**Purpose:** Pi constant  
**Parameters:** None  
**Returns:** π (approximately 3.14159...)  

```javascript
var circumference = 2 * pi() * radius;
var area = pi() * pow(radius, 2);
```

#### `e()`
**Purpose:** Euler's number  
**Parameters:** None  
**Returns:** e (approximately 2.71828...)  

```javascript
var natural_exp = pow(e(), x);  // Same as exp(x)
var compound_interest = principal * pow(e(), rate * time);
```

---

## Text Processing Extension

The Text Processing extension provides advanced string manipulation functions.

### Text Analysis Functions

#### `word_count(text)`
**Purpose:** Count words in text  
**Parameters:** String  
**Returns:** Number of words as integer  

```javascript
print(word_count("hello world"));           // 2
print(word_count("The quick brown fox"));   // 4
print(word_count(""));                      // 0
```

#### `line_count(text)`
**Purpose:** Count lines in text  
**Parameters:** String  
**Returns:** Number of lines as integer  

```javascript
print(line_count("line1\nline2\nline3"));   // 3
print(line_count("single line"));           // 1
print(line_count(""));                      // 0
```

#### `char_count(text, character)`
**Purpose:** Count character occurrences  
**Parameters:** String, single character string  
**Returns:** Count as integer  

```javascript
print(char_count("hello", "l"));      // 2
print(char_count("programming", "m")); // 2
print(char_count("test", "x"));       // 0
```

### String Manipulation Functions

#### `reverse(text)`
**Purpose:** Reverse a string  
**Parameters:** String  
**Returns:** Reversed string  

```javascript
print(reverse("hello"));      // "olleh"
print(reverse("12345"));      // "54321"
print(reverse(""));           // ""
```

#### `title_case(text)`
**Purpose:** Convert to title case  
**Parameters:** String  
**Returns:** Title case string  

```javascript
print(title_case("hello world"));       // "Hello World"
print(title_case("the quick brown"));   // "The Quick Brown"
print(title_case("LOUD TEXT"));         // "Loud Text"
```

#### `trim(text)`
**Purpose:** Remove leading and trailing whitespace  
**Parameters:** String  
**Returns:** Trimmed string  

```javascript
print(trim("  hello  "));     // "hello"
print(trim("\t\ntext\n\t"));  // "text"
print(trim("no spaces"));     // "no spaces"
```

#### `replace_all(text, old_substring, new_substring)`
**Purpose:** Replace all occurrences of substring  
**Parameters:** Text, old substring, new substring  
**Returns:** Modified string  

```javascript
var text = "hello world hello";
print(replace_all(text, "hello", "hi"));  // "hi world hi"
print(replace_all(text, "l", "x"));       // "hexxo worxd hexxo"
```

### Text Formatting Functions

#### `pad_left(text, width, character)`
**Purpose:** Pad string to width on left  
**Parameters:** String, target width, padding character  
**Returns:** Padded string  

```javascript
print(pad_left("42", 5, "0"));      // "00042"
print(pad_left("hi", 8, " "));      // "      hi"
print(pad_left("test", 3, "x"));    // "test" (no padding needed)
```

#### `split(text, delimiter)`
**Purpose:** Split string by delimiter  
**Parameters:** String, delimiter string  
**Returns:** Parts joined with " | " (simplified)  

```javascript
print(split("a,b,c", ","));           // "a | b | c"
print(split("one two three", " "));   // "one | two | three"
print(split("no-delim", ","));        // "no-delim"
```

---

## Error Handling

Mobius provides comprehensive error reporting with detailed messages and suggestions.

### Error Categories

1. **Syntax Errors** - Parsing and compilation errors
2. **Runtime Errors** - Execution-time errors
3. **Type Errors** - Type mismatch errors
4. **Argument Errors** - Function argument errors
5. **Memory Errors** - Memory allocation failures

### Error Information

Each error includes:
- **Error code** and category
- **Descriptive message**
- **Line and column number**
- **Helpful suggestions**
- **Function context** (if applicable)

### Example Error Output

```
━━━ Type Error ━━━
  at line 5, column 12

  Cannot add string and integer

  💡 Suggestion: Use str() to convert the integer to string, or int() to convert string to integer

Function: calculate_total
```

### Common Error Patterns

#### Type Mismatch

```javascript
var result = "hello" + 42;  // Error: Cannot add string and integer
```

**Solution:**
```javascript
var result = "hello" + str(42);  // "hello42"
// or
var result = int("5") + 42;      // 47
```

#### Undefined Variables

```javascript
print(undefined_var);  // Error: Unknown variable 'undefined_var'
```

**Solution:**
```javascript
var undefined_var = "defined now";
print(undefined_var);  // Works
```

#### Function Call Errors

```javascript
print(sqrt(-1));  // Error: sqrt() requires non-negative argument
```

**Solution:**
```javascript
var value = abs(-1);
print(sqrt(value));  // Works
```

#### Division by Zero

```javascript
var result = 10 / 0;  // Error: Division by zero
```

**Solution:**
```javascript
var divisor = 5;
if (divisor != 0) {
    var result = 10 / divisor;
}
```

---

## Examples

### Complete Programs

#### Example 1: Calculator

```javascript
// Simple calculator with error handling
func safe_divide(a, b) {
    if (b == 0) {
        print("Error: Division by zero");
        return nil;
    }
    return a / b;
}

func calculator() {
    var a = 10;
    var b = 3;
    
    print("Calculator Demo");
    print("a =", a, ", b =", b);
    print("Addition:", a + b);
    print("Subtraction:", a - b);
    print("Multiplication:", a * b);
    print("Division:", safe_divide(a, b));
    print("Power:", pow(a, 2));
    print("Square root of a:", sqrt(a));
}

calculator();
```

#### Example 2: Text Processing

```javascript
// Text analysis program
func analyze_text(text) {
    print("Text Analysis");
    print("=============");
    print("Original text:", text);
    print("Length:", len(text));
    print("Words:", word_count(text));
    print("Lines:", line_count(text));
    print("Character 'e' count:", char_count(text, "e"));
    print("Uppercase:", upper(text));
    print("Lowercase:", lower(text));
    print("Title case:", title_case(text));
    print("Trimmed:", trim(text));
    print("Reversed:", reverse(trim(text)));
}

var sample_text = "  Hello, Beautiful World!  ";
analyze_text(sample_text);
```

#### Example 3: Mathematical Calculations

```javascript
// Mathematical calculations with math extension
func math_demo() {
    print("Mathematical Functions Demo");
    print("==========================");
    
    // Constants
    print("Pi:", pi());
    print("E:", e());
    
    // Trigonometry
    var angle = pi() / 4;  // 45 degrees in radians
    print("sin(π/4):", sin(angle));
    print("cos(π/4):", cos(angle));
    print("tan(π/4):", tan(angle));
    
    // Logarithms
    print("log(e):", log(e()));
    print("log10(100):", log10(100));
    print("exp(1):", exp(1));
    
    // Advanced functions
    print("5! =", factorial(5));
    print("gcd(12, 8) =", gcd(12, 8));
    print("lcm(4, 6) =", lcm(4, 6));
}

math_demo();
```

#### Example 4: Loop and Conditionals

```javascript
// Fibonacci sequence with loops and conditionals
func fibonacci_sequence(n) {
    print("Fibonacci sequence (first", n, "numbers):");
    
    if (n <= 0) {
        print("Invalid input");
        return;
    }
    
    if (n >= 1) print("F(0) = 0");
    if (n >= 2) print("F(1) = 1");
    
    var a = 0;
    var b = 1;
    
    for (var i = 2; i < n; i = i + 1) {
        var next = a + b;
        print("F(" + str(i) + ") =", next);
        a = b;
        b = next;
    }
}

fibonacci_sequence(10);
```

#### Example 5: String Processing Pipeline

```javascript
// Complex string processing pipeline
func process_user_input(input) {
    print("Processing:", input);
    
    // Clean the input
    var cleaned = trim(input);
    if (len(cleaned) == 0) {
        print("Empty input after cleaning");
        return;
    }
    
    // Analyze
    var words = word_count(cleaned);
    var chars = len(cleaned);
    
    print("Statistics:");
    print("  Words:", words);
    print("  Characters:", chars);
    print("  Average word length:", float(chars) / float(words));
    
    // Transform
    var title = title_case(cleaned);
    var reversed = reverse(cleaned);
    
    print("Transformations:");
    print("  Title case:", title);
    print("  Reversed:", reversed);
    
    // Check for specific content
    if (contains(upper(cleaned), "HELLO")) {
        print("Contains greeting!");
    }
}

process_user_input("  hello beautiful world  ");
```

### Best Practices

1. **Always validate inputs** to functions
2. **Use meaningful variable names**
3. **Handle edge cases** (empty strings, zero values, etc.)
4. **Leverage the type system** with explicit conversions
5. **Use built-in functions** for common operations
6. **Structure code with functions** for reusability
7. **Add comments** for complex logic

### Performance Tips

1. **Minimize string concatenation** in loops
2. **Cache function results** when possible
3. **Use appropriate data types** for your use case
4. **Avoid deep recursion** for large datasets
5. **Leverage built-in functions** (they're optimized)

---

## Conclusion

Mobius provides a powerful yet simple scripting environment suitable for embedding in C applications. With its comprehensive standard library, optional math extension, and clean syntax, it offers the flexibility needed for various scripting tasks while maintaining ease of use.

For more information on embedding Mobius in your applications, see the [Embedding Guide](embedding_guide.md) and [Examples Guide](examples_guide.md).

---

**Document Version:** 1.0  
**Language Version:** Mobius 0.1.0  
**Last Updated:** December 2024

