# Phase 1: String Reference Counting - Detailed Design

## RefCountedString Structure Design

### Core Structure
```c
typedef struct RefCountedString {
    char* data;           // Actual string data (null-terminated)
    size_t length;        // Cached string length for O(1) operations
    int ref_count;        // Reference count (atomic for thread safety)
    bool is_literal;      // True for string literals (never freed)
} RefCountedString;
```

### Design Rationale

#### 1. **Cached Length**
- Store `strlen()` result to avoid recalculating
- O(1) string length operations
- Critical for performance in string-heavy scripts

#### 2. **Reference Counting**
- Simple integer counter
- Increment on assignment/copying
- Decrement on variable destruction
- Free when count reaches 0

#### 3. **String Literal Support**
- `is_literal` flag for string constants in source code
- Literal strings point to static memory, never freed
- Prevents accidental freeing of string literals

### API Functions

#### Creation Functions
```c
// Create ref-counted string from C string (copies data)
RefCountedString* string_create(const char* data);

// Create ref-counted string for literals (no copy, never freed)
RefCountedString* string_create_literal(const char* data);

// Create empty string with given capacity
RefCountedString* string_create_with_capacity(size_t capacity);
```

#### Reference Management
```c
// Increment reference count, return same pointer
RefCountedString* string_retain(RefCountedString* str);

// Decrement reference count, free if reaches 0
void string_release(RefCountedString* str);
```

#### Utility Functions
```c
// O(1) length operation
size_t string_length(RefCountedString* str);

// Access to string data
const char* string_data(RefCountedString* str);

// Check if two ref-counted strings are equal
bool string_equals(RefCountedString* a, RefCountedString* b);
```

## Integration with Existing Value System

### Current Value Structure (BEFORE)
```c
typedef struct {
    ValueType type;
    union {
        // ... other types
        char* string;           // CURRENT: Direct char pointer
        // ... other types
    } as;
} Value;
```

### Updated Value Structure (AFTER)
```c
typedef struct {
    ValueType type;
    union {
        // ... other types
        RefCountedString* string;  // NEW: Reference counted string
        // ... other types
    } as;
} Value;
```

## Migration Strategy

### Phase 1A: Core Implementation
1. Implement RefCountedString structure
2. Implement string lifecycle functions
3. Add compile-time flag for fallback

### Phase 1B: Value System Integration
1. Update Value structure
2. Update make_string_value()
3. Update copy_value() and free_value()

### Phase 1C: Standard Library
1. Update string builtin functions
2. Update string concatenation
3. Update all string operations

### Phase 1D: Parser Integration
1. Update scanner for string literals
2. Update parser string handling
3. Update all string token processing

### Phase 1E: Testing and Validation
1. Run full test suite
2. Performance benchmarking
3. Memory leak detection

## Compile-Time Fallback System

```c
// In value.h
#ifdef MOBIUS_USE_REF_COUNTING
    typedef RefCountedString* MobiusStringType;
    #define MOBIUS_STRING_DATA(str) string_data(str)
    #define MOBIUS_STRING_LENGTH(str) string_length(str)
#else
    typedef char* MobiusStringType;
    #define MOBIUS_STRING_DATA(str) (str)
    #define MOBIUS_STRING_LENGTH(str) strlen(str)
#endif

// In Value struct
union {
    MobiusStringType string;  // Works with both systems
    // ... other fields
} as;
```

## Performance Expectations

### String Assignment Performance
```c
// BEFORE (Deep Copy):
Value copy = copy_value(original);  // malloc + strcpy

// AFTER (Reference Counting):
Value copy = copy_value(original);  // Just ref_count++
```

**Expected improvement: 10-50x faster**

### Memory Usage
```c
// BEFORE: 1000 copies = 1000 allocations
Value copies[1000];
for (int i = 0; i < 1000; i++) {
    copies[i] = copy_value(original);  // 1000 malloc calls
}

// AFTER: 1000 copies = 1 allocation + 1000 ref increments
Value copies[1000];
for (int i = 0; i < 1000; i++) {
    copies[i] = copy_value(original);  // ref_count = 1001
}
```

**Expected improvement: 99% memory reduction for copies**

## Thread Safety Considerations

### Current Design: Single-threaded
- Simple integer reference counting
- No atomic operations needed
- Matches current Mobius threading model

### Future Enhancement: Thread-safe
```c
#ifdef MOBIUS_THREAD_SAFE
    #include <stdatomic.h>
    typedef atomic_int refcount_t;
    #define REF_INCREMENT(rc) atomic_fetch_add(&(rc), 1)
    #define REF_DECREMENT(rc) atomic_fetch_sub(&(rc), 1)
#else
    typedef int refcount_t;
    #define REF_INCREMENT(rc) (++(rc))
    #define REF_DECREMENT(rc) (--(rc))
#endif
```

## Debugging and Development Support

### Debug Mode Features
```c
#ifdef MOBIUS_DEBUG_REF_COUNTING
    // Track all string allocations
    extern int total_strings_allocated;
    extern int total_strings_freed;
    extern int current_string_count;
    
    // Debug printing
    void string_debug_print_refs(RefCountedString* str);
    void string_debug_dump_all();
#endif
```

### Memory Leak Detection
```c
// At program exit, verify all strings are freed
void string_verify_no_leaks() {
    if (current_string_count > 0) {
        printf("WARNING: %d strings leaked!\n", current_string_count);
        string_debug_dump_all();
    }
}
```

## Risk Mitigation

### 1. Gradual Migration
- Implement behind compile-time flag
- Test each component individually
- Keep deep copy as fallback

### 2. Comprehensive Testing
- Unit tests for ref counting functions
- Integration tests for Value system
- Performance benchmarks
- Memory leak detection

### 3. Debugging Support
- Debug mode with extensive logging
- Reference count validation
- Memory allocation tracking

### 4. Rollback Plan
- Compile-time flag can disable ref counting
- No changes to external API
- Full backward compatibility maintained

## Success Criteria

### Functional Requirements
- [ ] All existing string tests pass
- [ ] No memory leaks detected
- [ ] Reference counting works correctly
- [ ] String literals handled properly

### Performance Requirements
- [ ] String assignment 10x+ faster
- [ ] Memory usage 50%+ reduction for string-heavy scripts
- [ ] No performance regression for non-string operations

### Quality Requirements
- [ ] Code coverage >95% for new functions
- [ ] Zero compiler warnings
- [ ] Valgrind clean run
- [ ] All existing functionality preserved

This design provides a solid foundation for implementing high-performance string reference counting while maintaining safety and backward compatibility.
