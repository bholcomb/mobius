# Advanced Switch Statement Design
## Flexible Pattern Matching for Mobius

## 🎯 **Design Goals**

Create a powerful switch statement that supports:
- **Value matching**: integers, floats, strings, booleans, nil
- **Expression matching**: `<= 3.14`, `> "hello"`, `!= nil`
- **Pattern matching**: array destructuring, object patterns
- **Type matching**: check value types
- **Range matching**: `1..10`, `'a'..'z'`
- **Guard clauses**: additional conditions

## 🏗️ **Syntax Design**

### **Basic Value Matching**
```javascript
switch (value) {
    case 42:
        print("The answer");
        break;
    case "hello":
        print("Greeting");
        break;
    case true:
        print("Boolean true");
        break;
    case nil:
        print("Nothing");
        break;
    default:
        print("Something else");
}
```

### **Expression Matching**
```javascript
switch (score) {
    case >= 90:
        print("A grade");
        break;
    case >= 80:
        print("B grade");
        break;
    case >= 70:
        print("C grade");
        break;
    case < 60:
        print("F grade");
        break;
    default:
        print("D grade");
}
```

### **Range Matching**
```javascript
switch (letter) {
    case 'a'..'z':
        print("Lowercase letter");
        break;
    case 'A'..'Z':
        print("Uppercase letter");
        break;
    case '0'..'9':
        print("Digit");
        break;
    default:
        print("Other character");
}

switch (number) {
    case 1..10:
        print("Single digit");
        break;
    case 11..100:
        print("Two digits");
        break;
    default:
        print("Other");
}
```

### **Type Matching**
```javascript
switch (value) {
    case type(int):
        print("Integer:", value);
        break;
    case typeof(string):
        print("String:", value);
        break;
    case type(array):
        print("Array with", length(value), "elements");
        break;
    case type(table):
        print("Table object");
        break;
    default:
        print("Unknown type");
}
```

### **Pattern Matching (Arrays)**
```javascript
switch (arr) {
    case []:
        print("Empty array");
        break;
    case [x]:
        print("Single element:", x);
        break;
    case [first, second]:
        print("Two elements:", first, second);
        break;
    case [head, ...tail]:
        print("Head:", head, "Tail:", tail);
        break;
    default:
        print("Other array pattern");
}
```

### **Pattern Matching (Tables/Objects)**
```javascript
switch (person) {
    case {name: "John"}:
        print("Found John");
        break;
    case {age: >= 18}:
        print("Adult");
        break;
    case {name: string, age: int}:
        print("Person with name and age");
        break;
    case {name}:  // Extract name
        print("Person named:", name);
        break;
    default:
        print("Unknown person format");
}
```

### **Guard Clauses**
```javascript
switch (value) {
    case x when x > 0 && x < 100:
        print("Positive number under 100");
        break;
    case s when typeof(s) == string && length(s) > 5:
        print("Long string:", s);
        break;
    case arr when typeof(arr) == array && length(arr) > 0:
        print("Non-empty array");
        break;
    default:
        print("No match");
}
```

### **Multiple Case Values**
```javascript
switch (day) {
    case "monday", "tuesday", "wednesday", "thursday", "friday":
        print("Weekday");
        break;
    case "saturday", "sunday":
        print("Weekend");
        break;
    default:
        print("Invalid day");
}

switch (grade) {
    case 'A', 'B':
        print("Good grade");
        break;
    case 'C', 'D':
        print("Average grade");
        break;
    case 'F':
        print("Failed");
        break;
}
```

## 🔧 **AST Structure Design**

### **Switch Statement AST**
```c
// Switch statement AST node
typedef struct {
    Expr* discriminant;         // The value being switched on
    SwitchCase** cases;         // Array of case clauses
    size_t case_count;
    Stmt** default_body;        // Default case body (optional)
    size_t default_body_count;
} SwitchStmt;

// Individual case clause
typedef struct {
    CasePattern** patterns;     // Array of patterns to match
    size_t pattern_count;
    Expr* guard;               // Optional guard expression (when clause)
    Stmt** body;               // Case body statements
    size_t body_count;
    bool has_break;            // Whether case has explicit break
} SwitchCase;

// Pattern types for case matching
typedef enum {
    PATTERN_VALUE,             // Literal value: case 42:
    PATTERN_EXPRESSION,        // Expression: case >= 10:
    PATTERN_RANGE,             // Range: case 1..10:
    PATTERN_TYPE,              // Type: case typeof(string):
    PATTERN_ARRAY,             // Array destructuring: case [x, y]:
    PATTERN_TABLE,             // Table destructuring: case {name, age}:
    PATTERN_WILDCARD,          // Wildcard: case _:
} PatternType;

// Pattern matching structure
typedef struct {
    PatternType type;
    union {
        Value literal;          // For PATTERN_VALUE
        struct {
            TokenType operator; // >=, <=, ==, !=, etc.
            Expr* expression;   // Right-hand side
        } expr_pattern;
        struct {
            Expr* start;        // Start of range
            Expr* end;          // End of range
            bool inclusive;     // Whether end is inclusive
        } range_pattern;
        struct {
            ValueType value_type; // Type to match
        } type_pattern;
        struct {
            ArrayPattern* elements;
            size_t element_count;
            bool has_rest;      // Has ...rest pattern
            char* rest_name;    // Name for rest elements
        } array_pattern;
        struct {
            TablePattern* fields;
            size_t field_count;
            bool is_exhaustive; // Whether all fields must match
        } table_pattern;
    } as;
} CasePattern;

// Array pattern element
typedef struct {
    char* name;                // Variable name to bind (NULL for literals)
    CasePattern* pattern;      // Nested pattern (NULL for simple binding)
    bool is_rest;             // Whether this is a ...rest element
} ArrayPattern;

// Table pattern field
typedef struct {
    char* key;                // Field key
    char* bind_name;          // Variable name to bind (NULL to use key)
    CasePattern* pattern;     // Nested pattern (NULL for simple binding)
    bool is_optional;         // Whether field is optional
} TablePattern;
```

## 🔍 **Lexer and Parser Extensions**

### **New Tokens**
```c
// Add to TokenType enum
TOKEN_SWITCH,           // switch
TOKEN_CASE,             // case
TOKEN_DEFAULT,          // default
TOKEN_WHEN,             // when (for guards)
TOKEN_DOT_DOT,          // .. (range operator)
TOKEN_DOT_DOT_DOT,      // ... (rest operator)
TOKEN_TYPE,             // type (for type matching)
```

### **Parser Functions**
```c
// Parser function declarations
Stmt* parse_switch_statement(Parser* parser);
SwitchCase* parse_switch_case(Parser* parser);
CasePattern* parse_case_pattern(Parser* parser);
CasePattern* parse_value_pattern(Parser* parser);
CasePattern* parse_expression_pattern(Parser* parser);
CasePattern* parse_range_pattern(Parser* parser);
CasePattern* parse_type_pattern(Parser* parser);
CasePattern* parse_array_pattern(Parser* parser);
CasePattern* parse_table_pattern(Parser* parser);
Expr* parse_guard_expression(Parser* parser);
```

### **Parsing Algorithm**
```c
// Parse switch statement
Stmt* parse_switch_statement(Parser* parser) {
    consume(parser, TOKEN_SWITCH, "Expect 'switch'");
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'switch'");
    
    Expr* discriminant = parse_expression(parser);
    
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after switch expression");
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before switch body");
    
    SwitchCase** cases = NULL;
    size_t case_count = 0;
    Stmt** default_body = NULL;
    size_t default_body_count = 0;
    
    while (!check(parser, TOKEN_RIGHT_BRACE) && !is_at_end(parser)) {
        if (match(parser, TOKEN_CASE)) {
            // Parse case clause
            SwitchCase* case_clause = parse_switch_case(parser);
            cases = realloc(cases, sizeof(SwitchCase*) * (case_count + 1));
            cases[case_count++] = case_clause;
        } else if (match(parser, TOKEN_DEFAULT)) {
            // Parse default clause
            consume(parser, TOKEN_COLON, "Expect ':' after 'default'");
            default_body = parse_block_body(parser, &default_body_count);
        } else {
            error(parser, "Expect 'case' or 'default' in switch body");
        }
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after switch body");
    
    return make_switch_stmt(discriminant, cases, case_count, 
                           default_body, default_body_count);
}
```

## ⚙️ **Evaluation Strategy**

### **Pattern Matching Engine**
```c
// Pattern matching result
typedef struct {
    bool matches;              // Whether pattern matched
    Environment* bindings;     // Variable bindings from pattern
} PatternMatchResult;

// Main pattern matching function
PatternMatchResult match_pattern(CasePattern* pattern, Value value, Environment* env) {
    PatternMatchResult result = {false, NULL};
    
    switch (pattern->type) {
        case PATTERN_VALUE:
            result.matches = values_equal(pattern->as.literal, value);
            break;
            
        case PATTERN_EXPRESSION:
            result.matches = evaluate_expression_pattern(pattern, value, env);
            break;
            
        case PATTERN_RANGE:
            result.matches = value_in_range(value, pattern->as.range_pattern);
            break;
            
        case PATTERN_TYPE:
            result.matches = (value.type == pattern->as.type_pattern.value_type);
            break;
            
        case PATTERN_ARRAY:
            result = match_array_pattern(pattern, value, env);
            break;
            
        case PATTERN_TABLE:
            result = match_table_pattern(pattern, value, env);
            break;
            
        case PATTERN_WILDCARD:
            result.matches = true;  // Always matches
            break;
    }
    
    return result;
}

// Expression pattern matching
bool evaluate_expression_pattern(CasePattern* pattern, Value value, Environment* env) {
    TokenType op = pattern->as.expr_pattern.operator;
    EvalResult rhs_result = evaluate_expr(pattern->as.expr_pattern.expression, env);
    
    if (rhs_result.has_error) return false;
    
    switch (op) {
        case TOKEN_EQUAL_EQUAL:
            return values_equal(value, rhs_result.value);
        case TOKEN_BANG_EQUAL:
            return !values_equal(value, rhs_result.value);
        case TOKEN_GREATER:
            return compare_values(value, rhs_result.value) > 0;
        case TOKEN_GREATER_EQUAL:
            return compare_values(value, rhs_result.value) >= 0;
        case TOKEN_LESS:
            return compare_values(value, rhs_result.value) < 0;
        case TOKEN_LESS_EQUAL:
            return compare_values(value, rhs_result.value) <= 0;
        default:
            return false;
    }
}

// Array pattern matching with destructuring
PatternMatchResult match_array_pattern(CasePattern* pattern, Value value, Environment* env) {
    PatternMatchResult result = {false, create_environment(env)};
    
    if (value.type != VAL_ARRAY) return result;
    
    ArrayValue* array = value.as.array;
    ArrayPattern* array_pattern = &pattern->as.array_pattern;
    
    // Check if array length matches pattern requirements
    size_t required_elements = array_pattern->element_count;
    if (array_pattern->has_rest) required_elements--;
    
    if (array->length < required_elements) return result;
    
    // Match each pattern element
    for (size_t i = 0; i < array_pattern->element_count; i++) {
        ArrayPattern* elem_pattern = &array_pattern->elements[i];
        
        if (elem_pattern->is_rest) {
            // Handle rest pattern ...tail
            ArrayValue* rest_array = array_create(array->length - i);
            for (size_t j = i; j < array->length; j++) {
                array_push(rest_array, array_get(array, j));
            }
            define_variable(result.bindings, elem_pattern->name, 
                          make_array_value(rest_array));
            break;
        } else {
            // Regular element pattern
            Value element = array_get(array, i);
            if (elem_pattern->pattern) {
                // Nested pattern matching
                PatternMatchResult elem_result = match_pattern(elem_pattern->pattern, element, env);
                if (!elem_result.matches) return result;
                merge_bindings(result.bindings, elem_result.bindings);
            } else if (elem_pattern->name) {
                // Simple binding
                define_variable(result.bindings, elem_pattern->name, element);
            }
        }
    }
    
    result.matches = true;
    return result;
}
```

### **Switch Statement Evaluation**
```c
// Evaluate switch statement
EvalResult eval_switch_stmt(SwitchStmt* stmt, Environment* env) {
    EvalResult discriminant_result = evaluate_expr(stmt->discriminant, env);
    if (discriminant_result.has_error) return discriminant_result;
    
    Value switch_value = discriminant_result.value;
    
    // Try each case in order
    for (size_t i = 0; i < stmt->case_count; i++) {
        SwitchCase* case_clause = stmt->cases[i];
        
        // Check if any pattern in this case matches
        bool case_matches = false;
        Environment* case_env = create_environment(env);
        
        for (size_t j = 0; j < case_clause->pattern_count; j++) {
            PatternMatchResult match_result = match_pattern(
                case_clause->patterns[j], switch_value, env);
            
            if (match_result.matches) {
                // Check guard clause if present
                if (case_clause->guard) {
                    EvalResult guard_result = evaluate_expr(case_clause->guard, case_env);
                    if (guard_result.has_error || !is_truthy(guard_result.value)) {
                        continue;  // Guard failed, try next pattern
                    }
                }
                
                // Pattern and guard matched
                case_matches = true;
                if (match_result.bindings) {
                    merge_bindings(case_env, match_result.bindings);
                }
                break;
            }
        }
        
        if (case_matches) {
            // Execute case body
            EvalResult case_result = execute_block(case_clause->body, 
                                                 case_clause->body_count, case_env);
            
            // Handle break and fall-through
            if (case_result.has_error || case_result.has_returned || 
                case_clause->has_break) {
                return case_result;
            }
            
            // Fall through to next case if no break
        }
    }
    
    // No case matched, try default
    if (stmt->default_body) {
        return execute_block(stmt->default_body, stmt->default_body_count, env);
    }
    
    // No match and no default
    return make_nil_result();
}
```

## 🧪 **Testing Strategy**

### **Test Cases**
```javascript
// Basic value matching
func test_basic_matching() {
    switch (42) {
        case 42: return "correct";
        default: return "wrong";
    }
}

// Expression matching
func test_expression_matching() {
    switch (85) {
        case >= 90: return "A";
        case >= 80: return "B";
        case >= 70: return "C";
        default: return "F";
    }
}

// Range matching
func test_range_matching() {
    switch (5) {
        case 1..10: return "single digit";
        case 11..100: return "double digit";
        default: return "other";
    }
}

// Array destructuring
func test_array_destructuring() {
    switch ([1, 2, 3]) {
        case []: return "empty";
        case [x]: return "single: " + x;
        case [x, y]: return "pair: " + x + ", " + y;
        case [x, y, z]: return "triple: " + x + ", " + y + ", " + z;
        default: return "many";
    }
}

// Guard clauses
func test_guard_clauses() {
    switch (15) {
        case x when x > 10 && x < 20: return "teen";
        case x when x >= 20: return "adult";
        default: return "child";
    }
}
```

## 📋 **Implementation Phases**

### **Phase 1: Basic Switch**
- [ ] Add switch/case/default tokens
- [ ] Basic AST structures for switch statements
- [ ] Parser for simple value matching
- [ ] Evaluator for basic cases and default

### **Phase 2: Expression Matching**
- [ ] Comparison operators in case patterns
- [ ] Expression evaluation in patterns
- [ ] Range syntax and matching

### **Phase 3: Type Matching**
- [ ] Type pattern syntax
- [ ] Type checking in patterns
- [ ] Integration with existing type system

### **Phase 4: Pattern Matching**
- [ ] Array destructuring patterns
- [ ] Table destructuring patterns
- [ ] Variable binding in patterns

### **Phase 5: Advanced Features**
- [ ] Guard clauses (when expressions)
- [ ] Multiple patterns per case
- [ ] Fall-through behavior
- [ ] Break statement handling

### **Phase 6: Optimization**
- [ ] Jump tables for simple value switches
- [ ] Pattern compilation optimization
- [ ] Guard clause optimization

This switch statement design provides powerful pattern matching capabilities while maintaining clean, readable syntax!

