# Type Annotations Design for Mobius

## Overview

This document outlines the design for adding optional type annotations to Mobius while maintaining its dynamic typing nature. The goal is to provide type hints for better performance, error checking, and developer experience without sacrificing flexibility.

## Syntax Design

### 1. Variable Type Annotations

```javascript
// Basic syntax: var name: type = value
var name: string = "John";
var age: int32 = 25;
var height: float = 5.9;

// Type inference when no annotation
var count = 10;        // Inferred as int64 (default integer type)
var pi = 3.14159;      // Inferred as float64

// Optional annotations for clarity
var data: table = {};
var callback: function = someFunction;
```

### 2. Numeric Type Suffixes

```javascript
// Integer types
var tiny = 127i8;          // int8 (-128 to 127)
var small = 32767i16;      // int16 (-32,768 to 32,767)
var medium = 2147483647i32; // int32 
var large = 9223372036854775807i64; // int64

// Unsigned integers
var byte = 255u8;          // uint8 (0 to 255)
var word = 65535u16;       // uint16 (0 to 65,535)
var dword = 4294967295u32; // uint32
var qword = 18446744073709551615u64; // uint64

// Float types (for future expansion)
var precise = 3.14159f64;  // float64 (double precision)
var fast = 3.14f32;        // float32 (single precision) - future
```

### 3. Function Type Annotations

```javascript
// Function with typed parameters and return type
func add(x: int32, y: int32): int32 {
    return x + y;
}

// Mixed parameter types
func processUser(name: string, age: int32, active: bool): table {
    return {["name"] = name, ["age"] = age, ["active"] = active};
}

// Optional parameters (future feature)
func greet(name: string, title?: string): string {
    if (title) {
        return title + " " + name;
    }
    return "Hello, " + name;
}
```

### 4. Table Type Annotations

```javascript
// Generic table
var data: table = {};

// Table with element type hint (future feature)
var numbers: table<int32> = {1, 2, 3, 4};
var strings: table<string> = {"a", "b", "c"};

// Mixed table (default behavior)
var mixed = {1, "hello", 3.14, true};
```

## Token Extensions

### New Tokens for Type System

```c
// Type annotation tokens
TOKEN_COLON,              // :
TOKEN_QUESTION,           // ? (for optional types)
TOKEN_LESS,               // < (for generics)
TOKEN_GREATER,            // > (for generics)

// Type name tokens
TOKEN_TYPE_STRING,        // string
TOKEN_TYPE_BOOL,          // bool
TOKEN_TYPE_INT8,          // int8
TOKEN_TYPE_INT16,         // int16
TOKEN_TYPE_INT32,         // int32
TOKEN_TYPE_INT64,         // int64
TOKEN_TYPE_UINT8,         // uint8
TOKEN_TYPE_UINT16,        // uint16
TOKEN_TYPE_UINT32,        // uint32
TOKEN_TYPE_UINT64,        // uint64
TOKEN_TYPE_FLOAT,         // float
TOKEN_TYPE_TABLE,         // table
TOKEN_TYPE_FUNCTION,      // function
TOKEN_TYPE_USERDATA,      // userdata

// Numeric suffix tokens
TOKEN_INT8_SUFFIX,        // i8
TOKEN_INT16_SUFFIX,       // i16
TOKEN_INT32_SUFFIX,       // i32
TOKEN_INT64_SUFFIX,       // i64
TOKEN_UINT8_SUFFIX,       // u8
TOKEN_UINT16_SUFFIX,      // u16
TOKEN_UINT32_SUFFIX,      // u32
TOKEN_UINT64_SUFFIX,      // u64
TOKEN_FLOAT32_SUFFIX,     // f32
TOKEN_FLOAT64_SUFFIX,     // f64
```

## AST Extensions

### Type Information Structure

```c
// Type information for AST nodes
typedef enum {
    TYPE_UNKNOWN,         // No type specified/inferred
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_UINT32,
    TYPE_UINT64,
    TYPE_FLOAT32,
    TYPE_FLOAT64,
    TYPE_TABLE,
    TYPE_FUNCTION,
    TYPE_USERDATA,
    TYPE_GENERIC,         // For generic types like table<T>
} MobiusType;

typedef struct {
    MobiusType base_type;
    MobiusType element_type;  // For generic types like table<int32>
    bool is_optional;         // For optional types (future)
} TypeInfo;

// Extended AST nodes with type information
typedef struct {
    Token name;
    Expr* initializer;
    TypeInfo type_hint;       // Optional type annotation
} VarStmt;

typedef struct {
    Token name;
    Token* params;
    TypeInfo* param_types;    // Array of parameter type hints
    size_t param_count;
    TypeInfo return_type;     // Return type hint
    Stmt** body;
    size_t body_count;
    struct Environment* closure;
} MobiusFunction;
```

## Scanner Extensions

### Numeric Suffix Recognition

```c
// In scanner.c - recognize numeric suffixes
static Token scan_number_with_suffix(Scanner* scanner) {
    // Scan base number first
    Token base_token = scan_number(scanner);
    
    // Check for type suffix
    if (peek(scanner) == 'i' || peek(scanner) == 'u' || peek(scanner) == 'f') {
        char suffix[4] = {0};
        int suffix_len = 0;
        
        // Read suffix characters
        while (isalpha(peek(scanner)) && suffix_len < 3) {
            suffix[suffix_len++] = advance(scanner);
        }
        
        // Determine suffix type
        TokenType suffix_type = TOKEN_ERROR;
        if (strcmp(suffix, "i8") == 0) suffix_type = TOKEN_INT8_SUFFIX;
        else if (strcmp(suffix, "i16") == 0) suffix_type = TOKEN_INT16_SUFFIX;
        else if (strcmp(suffix, "i32") == 0) suffix_type = TOKEN_INT32_SUFFIX;
        else if (strcmp(suffix, "i64") == 0) suffix_type = TOKEN_INT64_SUFFIX;
        else if (strcmp(suffix, "u8") == 0) suffix_type = TOKEN_UINT8_SUFFIX;
        else if (strcmp(suffix, "u16") == 0) suffix_type = TOKEN_UINT16_SUFFIX;
        else if (strcmp(suffix, "u32") == 0) suffix_type = TOKEN_UINT32_SUFFIX;
        else if (strcmp(suffix, "u64") == 0) suffix_type = TOKEN_UINT64_SUFFIX;
        else if (strcmp(suffix, "f32") == 0) suffix_type = TOKEN_FLOAT32_SUFFIX;
        else if (strcmp(suffix, "f64") == 0) suffix_type = TOKEN_FLOAT64_SUFFIX;
        
        if (suffix_type != TOKEN_ERROR) {
            // Create typed numeric token
            return make_typed_numeric_token(base_token, suffix_type);
        }
    }
    
    return base_token;
}
```

## Parser Extensions

### Type Annotation Parsing

```c
// Parse optional type annotation
TypeInfo parse_type_annotation(Parser* parser) {
    TypeInfo type_info = {TYPE_UNKNOWN, TYPE_UNKNOWN, false};
    
    if (parser_match(parser, TOKEN_COLON)) {
        // Parse type name
        if (parser_match(parser, TOKEN_TYPE_STRING)) {
            type_info.base_type = TYPE_STRING;
        } else if (parser_match(parser, TOKEN_TYPE_INT32)) {
            type_info.base_type = TYPE_INT32;
        } else if (parser_match(parser, TOKEN_TYPE_INT64)) {
            type_info.base_type = TYPE_INT64;
        }
        // ... handle other types
        
        // Check for generic syntax: table<int32>
        if (type_info.base_type == TYPE_TABLE && parser_match(parser, TOKEN_LESS)) {
            type_info.element_type = parse_simple_type(parser);
            consume(parser, TOKEN_GREATER, "Expect '>' after generic type parameter");
        }
    }
    
    return type_info;
}

// Enhanced variable declaration parsing
Stmt* parse_var_declaration(Parser* parser) {
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name");
    
    // Parse optional type annotation
    TypeInfo type_hint = parse_type_annotation(parser);
    
    Expr* initializer = NULL;
    if (parser_match(parser, TOKEN_EQUAL)) {
        initializer = parse_expression(parser);
    }
    
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after variable declaration");
    return make_var_stmt(name, initializer, type_hint);
}
```

## Runtime Type Checking

### Type Validation and Conversion

```c
// Type checking and conversion utilities
typedef struct {
    bool success;
    Value converted_value;
    char* error_message;
} TypeConversionResult;

TypeConversionResult validate_and_convert_type(Value value, TypeInfo expected_type) {
    TypeConversionResult result = {false, make_nil_value(), NULL};
    
    switch (expected_type.base_type) {
        case TYPE_INT32:
            if (value.type == VAL_INTEGER) {
                // Check if value fits in int32 range
                if (value.as.integer.value.i64 >= INT32_MIN && 
                    value.as.integer.value.i64 <= INT32_MAX) {
                    result.success = true;
                    result.converted_value = make_integer_value(NUM_INT32, value.as.integer.value.i64);
                } else {
                    result.error_message = strdup("Value out of range for int32");
                }
            } else if (value.type == VAL_FLOAT64) {
                // Convert float to int32 with range checking
                if (value.as.float64_val >= INT32_MIN && value.as.float64_val <= INT32_MAX) {
                    result.success = true;
                    result.converted_value = make_integer_value(NUM_INT32, (int64_t)value.as.float64_val);
                } else {
                    result.error_message = strdup("Float value out of range for int32");
                }
            } else {
                result.error_message = strdup("Cannot convert to int32");
            }
            break;
            
        case TYPE_STRING:
            if (value.type == VAL_STRING) {
                result.success = true;
                result.converted_value = copy_value(value);
            } else {
                // Convert other types to string
                char* str_repr = value_to_string(value);
                if (str_repr) {
                    result.success = true;
                    result.converted_value = make_string_value(str_repr);
                }
            }
            break;
            
        // ... handle other types
    }
    
    return result;
}
```

## Evaluator Integration

### Type-Aware Variable Assignment

```c
// Enhanced variable assignment with type checking
EvalResult eval_var_stmt_with_types(VarStmt* stmt, Environment* env) {
    Value initial_value = make_nil_value();
    
    if (stmt->initializer) {
        EvalResult init_result = evaluate_expr(stmt->initializer, env);
        if (is_error(init_result)) return init_result;
        initial_value = init_result.value;
    }
    
    // Validate type if annotation provided
    if (stmt->type_hint.base_type != TYPE_UNKNOWN) {
        TypeConversionResult conversion = validate_and_convert_type(initial_value, stmt->type_hint);
        if (!conversion.success) {
            return make_error_detailed_with_source(
                conversion.error_message ? conversion.error_message : "Type validation failed",
                "Check that the value matches the declared type",
                ERROR_TYPE,
                stmt->name.line,
                stmt->name.column,
                NULL
            );
        }
        initial_value = conversion.converted_value;
        free(conversion.error_message);
    }
    
    define_variable(env, stmt->name.lexeme, initial_value);
    return make_success(make_nil_value());
}
```

## Benefits

### 1. Performance Optimizations
- **Numeric Operations**: Knowing exact integer types enables optimized arithmetic
- **Memory Layout**: Type information allows better memory allocation strategies
- **Function Calls**: Typed parameters enable faster dispatch

### 2. Better Error Messages
```
Type Error at line 15, column 8:
  Cannot assign string "hello" to variable 'count: int32'
  
  Expected: int32
  Got: string
  
  Suggestion: Use int32("hello") to convert, or change variable type to string
```

### 3. IDE Support
- **Auto-completion**: Type hints enable better IntelliSense
- **Static Analysis**: Optional type checking during development
- **Refactoring**: Type information improves rename/refactor safety

### 4. Gradual Typing
- **Backwards Compatible**: Existing code works without changes
- **Opt-in**: Developers can add types where beneficial
- **Flexible**: Mix typed and untyped code in same program

## Implementation Phases

### Phase 1: Basic Type Annotations
1. Extend scanner for type tokens and numeric suffixes
2. Update parser for type annotation syntax
3. Modify AST nodes to store type information
4. Basic runtime type validation

### Phase 2: Function Type Checking
1. Function parameter and return type annotations
2. Type checking at function call sites
3. Better error messages for type mismatches

### Phase 3: Advanced Features
1. Generic types (table<T>)
2. Optional types (T?)
3. Union types (string | int32)
4. Type inference improvements

### Phase 4: Optimization
1. Type-specific optimizations in evaluator
2. Compile-time type checking mode
3. Performance improvements based on type information

This design maintains Mobius's dynamic nature while providing the benefits of optional static typing, making it suitable for both rapid prototyping and performance-critical applications.
