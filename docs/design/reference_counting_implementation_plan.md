# Reference Counting Implementation Plan

## Overview
This plan implements reference counting in two phases:
- **Phase 1**: String Reference Counting (10-50x performance improvement)
- **Phase 2**: AST Reference Counting (fixes load() function memory issues)

## PHASE 1: STRING REFERENCE COUNTING

### Priority: HIGH (Massive performance gains with contained risk)
### Estimated Timeline: 3-5 days
### Risk Level: MEDIUM

---

### Task 1: Design RefCountedString Structure
**File**: `src/mobius/value.h`
```c
typedef struct RefCountedString {
    char* data;           // Actual string data
    size_t length;        // String length (cached for performance)
    int ref_count;        // Reference count
    bool is_literal;      // True for string literals (never freed)
    bool is_mutable;      // Future: support for copy-on-write
} RefCountedString;
```

**Design Decisions:**
- Include length caching for O(1) length operations
- Support for string literals that are never freed
- Future-proof with mutability flag

---

### Task 2: Implement RefCountedString Structure
**Files**: `src/mobius/value.h`, `src/mobius/value.c`

**New Functions:**
```c
// Creation
RefCountedString* string_create(const char* data);
RefCountedString* string_create_literal(const char* data);  // Never freed

// Reference management  
RefCountedString* string_retain(RefCountedString* str);
void string_release(RefCountedString* str);

// Utilities
size_t string_length(RefCountedString* str);  // O(1) operation
const char* string_data(RefCountedString* str);
```

---

### Task 3: Update Value System
**Files**: `src/mobius/value.h`, `src/mobius/value.c`

**Changes to Value struct:**
```c
// OLD:
char* string;

// NEW:  
RefCountedString* string;
```

**Update functions:**
- `make_string_value()` - Create with ref count 1
- `copy_value()` - Use `string_retain()` instead of malloc/strcpy
- `free_value()` - Use `string_release()` instead of free

---

### Task 4: Update String Operations in Standard Library
**File**: `src/mobius/stdlib.c`

**Functions to update:**
- `builtin_concat()` - Create new ref-counted result
- `builtin_substr()` - Create new ref-counted substring  
- `builtin_upper()`, `builtin_lower()` - Create new ref-counted strings
- All string manipulation functions

**Pattern:**
```c
// OLD:
char* result = malloc(len);
strcpy(result, ...);
return make_string_value(result);

// NEW:
RefCountedString* result = string_create(...);
return make_string_value(result);
```

---

### Task 5: Update Scanner and Parser
**Files**: `src/mobius/scanner.c`, `src/mobius/parser.c`

**Scanner changes:**
- String literals created as `string_create_literal()` (never freed)
- Token string values use ref counting

**Parser changes:**
- String expression parsing uses ref counting
- Identifier to string conversion uses ref counting

---

### Task 6: Phase 1 Testing and Validation
**Tests to create/update:**
- String creation and copying performance test
- Memory leak detection (all ref counts should reach 0)
- Stress test with heavy string operations
- Existing string tests should still pass

**Performance benchmark:**
```mobius
// Test script: measure before/after performance
func string_intensive_test() {
    var base = "test string for performance measurement";
    var i = 0;
    while (i < 10000) {
        var copy1 = base;  // Should be instant with ref counting
        var copy2 = concat(copy1, " suffix");
        i = i + 1;
    }
}
```

---

## PHASE 2: AST REFERENCE COUNTING

### Priority: MEDIUM (Fixes load() function, higher complexity)
### Estimated Timeline: 5-8 days  
### Risk Level: HIGH

---

### Task 7: Design AST Reference Counting System
**Files**: `src/mobius/ast.h`

**Add to all AST structures:**
```c
// Add to Expr and Stmt structures
typedef struct Expr {
    ExprType type;
    int ref_count;        // NEW: Reference count
    // ... existing fields
} Expr;

typedef struct Stmt {
    StmtType type;
    int ref_count;        // NEW: Reference count  
    // ... existing fields
} Stmt;
```

---

### Task 8: Implement AST Lifecycle Management
**Files**: `src/mobius/ast.h`, `src/mobius/ast.c`

**New Functions:**
```c
// Expression reference management
Expr* expr_retain(Expr* expr);
void expr_release(Expr* expr);

// Statement reference management  
Stmt* stmt_retain(Stmt* stmt);
void stmt_release(Stmt* stmt);

// Bulk operations
void stmt_array_retain(Stmt** stmts, size_t count);
void stmt_array_release(Stmt** stmts, size_t count);
```

---

### Task 9: Update Parser AST Creation
**File**: `src/mobius/parser.c`

**Changes:**
- All `make_*_expr()` and `make_*_stmt()` functions initialize ref_count = 1
- Parser retains AST nodes when storing them in structures
- Parse result cleanup releases top-level AST nodes

---

### Task 10: Update Function Creation for AST References
**File**: `src/mobius/evaluator.c`

**Function creation changes:**
```c
// In eval_function_stmt():
// OLD: function->body = stmt->body;  // Dangerous pointer copy

// NEW: Retain references to AST body
function->body = stmt->body;
stmt_array_retain(function->body, function->body_count);
```

**Function cleanup:**
```c
// In free_value() for functions:
stmt_array_release(func->body, func->body_count);
```

---

### Task 11: Fix load() Function Memory Management  
**File**: `src/mobius/stdlib.c`

**Enable proper cleanup:**
```c
// In builtin_load():
// OLD: Disabled cleanup to prevent crashes
// free_parse_result(&parse_result);  // DISABLED

// NEW: Safe cleanup with ref counting
free_parse_result(&parse_result);  // Safe now!
free_file_result(&file_result);    // Safe now!
```

---

### Task 12: Update Evaluator AST Handling
**File**: `src/mobius/evaluator.c`

**Key changes:**
- `evaluate_stmt()` and `evaluate_expr()` don't need to retain (read-only)
- Statement execution doesn't change AST reference counts
- Only function creation and variable assignment affect AST refs

---

### Task 13: Phase 2 Testing and Integration
**Critical tests:**
- Load function with recursive calls (should not crash)
- Load function with functions that call functions from other loaded scripts
- Memory leak detection for AST nodes
- Stress test loading/unloading many scripts

**Integration test:**
```mobius
// Test script: should work without crashes
load("script_with_functions.mob");
var result = loaded_function("test");
load("another_script.mob");  // Should not interfere
```

---

## IMPLEMENTATION STRATEGY

### Phase 1 First (Recommended)
**Pros:**
- Immediate massive performance gains  
- Lower risk, well-contained changes
- Builds confidence for Phase 2
- Users see immediate benefits

### Development Approach:
1. **Feature branch**: `feature/string-ref-counting`
2. **Incremental commits**: One task per commit
3. **Continuous testing**: Run full test suite after each task
4. **Performance monitoring**: Measure improvements at each step

### Rollback Strategy:
- Keep deep copy implementation as fallback
- Compile-time flag to switch between approaches
- Extensive testing before final commit

### Risk Mitigation:
- **Memory leak detection**: Use Valgrind throughout development
- **Reference cycle detection**: Add debug assertions for ref count leaks
- **Comprehensive testing**: Test all string operations thoroughly

## SUCCESS METRICS

### Phase 1 Success Criteria:
- [ ] All existing tests pass
- [ ] String operations 10-50x faster  
- [ ] Memory usage reduced by 30-70% for string-heavy scripts
- [ ] Zero memory leaks in test suite
- [ ] Performance test shows dramatic improvement

### Phase 2 Success Criteria:
- [ ] load() function works with recursive calls
- [ ] Functions from loaded scripts work reliably
- [ ] No segfaults in integration tests
- [ ] Memory management is stable
- [ ] All original functionality preserved

## TIMELINE ESTIMATE

**Phase 1**: 3-5 days
- Day 1: Tasks 1-3 (Design and core implementation)
- Day 2: Tasks 4-5 (Update stdlib and parser)  
- Day 3: Task 6 (Testing and validation)

**Phase 2**: 5-8 days  
- Day 1-2: Tasks 7-9 (Design and AST implementation)
- Day 3-4: Tasks 10-11 (Function and load() updates)
- Day 5-6: Tasks 12-13 (Evaluator and testing)

**Total**: 8-13 days for complete implementation
