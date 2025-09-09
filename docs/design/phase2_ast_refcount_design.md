# Phase 2: AST Reference Counting Design

## Problem Analysis

The remaining 2 failing tests (`test_load_function.mob` and `nested_loader.mob`) are failing due to AST memory management issues in the `load()` function:

### Root Cause
1. **load() function** loads a script and parses it into AST nodes
2. **Function definitions** in the loaded script create `MobiusFunction` objects that store **shallow pointers** to AST body statements
3. **load() completes** and calls `free_parse_result()`, freeing all AST nodes
4. **Function calls later** try to access the freed AST body, causing crashes

### Current Issue in Code
```c
// src/mobius/evaluator.c:1120-1124
// TODO: For now, still store pointers to body statements
// This is a bigger change that requires deep copying the entire AST
function->body = stmt->body;           // SHALLOW POINTER - PROBLEMATIC!
function->body_count = stmt->body_count;
```

## Solution: AST Reference Counting

### Core Design Principles
1. **Similar to String Reference Counting**: Each AST node gets a reference count
2. **Automatic Memory Management**: Nodes are freed when ref_count reaches 0
3. **Shared Ownership**: Multiple functions can reference the same AST nodes
4. **Efficient**: O(1) increment/decrement operations

### AST Node Structure Changes

```c
// Add reference counting to Expr and Stmt structures
typedef struct {
    ExprType type;
    int ref_count;          // NEW: Reference counter
    union {
        // ... existing expr data
    } as;
} Expr;

typedef struct {
    StmtType type;
    int ref_count;          // NEW: Reference counter  
    union {
        // ... existing stmt data
    } as;
} Stmt;
```

### Reference Counting Functions

```c
// AST Lifecycle Management
Expr* ast_retain_expr(Expr* expr);
void ast_release_expr(Expr* expr);
Stmt* ast_retain_stmt(Stmt* stmt);
void ast_release_stmt(Stmt* stmt);

// Deep retain/release for complex structures
void ast_retain_stmt_array(Stmt** stmts, size_t count);
void ast_release_stmt_array(Stmt** stmts, size_t count);
```

### Integration Points

1. **Parser Creation**: All AST nodes created with `ref_count = 1`
2. **Function Creation**: `function->body` retains references to statements
3. **Load Function**: Can safely free parse results after functions retain references
4. **Expression Evaluation**: Proper retain/release during evaluation
5. **Function Cleanup**: Release AST references when function is freed

### Memory Safety Benefits

1. **Eliminates Use-After-Free**: AST nodes stay alive as long as referenced
2. **Enables load() Function**: Scripts can define persistent functions
3. **Supports Recursion**: Functions can safely call themselves
4. **Clean Shutdown**: Proper reference counting prevents memory leaks

### Performance Characteristics

- **Creation**: O(1) - set ref_count = 1
- **Retain**: O(1) - increment ref_count  
- **Release**: O(1) - decrement, O(n) cleanup if ref_count = 0
- **Memory**: +4 bytes per AST node (int ref_count)
- **CPU**: Minimal overhead for increment/decrement operations

## Implementation Strategy

### Phase 2.1: Core AST Structure
- Add ref_count field to Expr and Stmt
- Implement basic retain/release functions

### Phase 2.2: Parser Integration  
- Update parser to create ref-counted AST nodes
- Ensure all nodes start with ref_count = 1

### Phase 2.3: Function Creation
- Update function creation to retain AST body references
- Remove TODO comment and implement proper retention

### Phase 2.4: Load Function Fix
- Enable proper cleanup in load() function
- Test that functions persist after script loading

### Phase 2.5: Evaluator Updates
- Update evaluator for proper retain/release
- Handle complex expression evaluation safely

### Phase 2.6: Testing & Integration
- Fix the 2 failing load() function tests
- Comprehensive testing of recursive functions
- Memory leak verification

## Expected Outcomes

✅ **All Tests Passing**: 36/36 tests (100% success rate)
✅ **Load Function Working**: Multi-file scripts fully supported  
✅ **Recursive Functions**: Safe recursive calls with proper memory management
✅ **Memory Efficiency**: No memory leaks, optimal reference counting
✅ **Production Ready**: Complete memory management system

This completes the hybrid memory management system:
- **Phase 1**: String Reference Counting (✅ Complete)
- **Phase 2**: AST Reference Counting (🚧 In Progress)
