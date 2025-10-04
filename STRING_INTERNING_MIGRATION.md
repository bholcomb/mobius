# String Interning Migration Checklist

## Overview
Migrating from `RefCountedString` to `MobiusString` with interning pool for better performance.

## Key Changes
- All strings are now interned (deduplicated in per-state hash table)
- Precomputed hash values stored in `MobiusString` for fast comparison and table lookup
- `make_string_value_from_cstr()` now requires `MobiusState*` parameter

---

## ✅ Already Completed

- [x] Created `src/mobius/internal/string_intern.h` and `string_intern.c`
- [x] Added `StringInternPool* string_pool` to `MobiusState`
- [x] Initialize/free string pool in `mobius_new_state()` and `mobius_free_state()`
- [x] Updated `src/mobius/data/value.h` - Changed `RefCountedString*` → `MobiusString*`
- [x] Updated `src/mobius/data/value.c` - `make_string_value_from_cstr()` signature
- [x] Updated `src/mobius/data/table.c` - Use precomputed hash for strings
- [x] Updated `src/mobius/data/table.h` - Metamethod functions take `MobiusString*`
- [x] Updated `src/mobius/eval/eval_arithmatic.c` - Optimized string concatenation

---

## 📝 TODO: Core Files to Update

### A. src/mobius/data/value.c
- [ ] Line 60: Update `make_string_value(MobiusString*)` parameter type
- [ ] Line 459: Update `string_retain()` call to use new API

### B. src/mobius/eval/eval_import.c (4 locations)
```c
// Pattern: Get state from environment
MobiusState* state = env->current_context->state;
```
- [ ] Line 102: `make_string_value_from_cstr(path[i])` → add `state` parameter
- [ ] Line 117: `make_string_value_from_cstr(path[i])` → add `state` parameter
- [ ] Line 145: `make_string_value_from_cstr(func_name)` → add `state` parameter
- [ ] Line 355: `make_string_value_from_cstr(func_name)` → add `state` parameter

### C. src/mobius/state/stack.c (3 locations)
```c
// state is already available as function parameter
```
- [ ] Line 470: `make_string_value_from_cstr(str)` → add `state` parameter
- [ ] Line 581: `make_string_value_from_cstr(key)` → add `state` parameter
- [ ] Line 600: `make_string_value_from_cstr(key)` → add `state` parameter

### D. src/mobius/library/types.c (2 locations)
```c
// state is passed as MobiusState* parameter to library functions
```
- [ ] Line 28: `make_string_value_from_cstr("strict_mode")` → add `state` parameter
- [ ] Line 33: `make_string_value_from_cstr("warn_on_conversion")` → add `state` parameter

### E. src/mobius/eval/eval_expression.c (2 locations)
```c
// Get state from environment
MobiusState* state = env->current_context->state;
```
- [ ] Line 120: `make_string_value_from_cstr(func_name)` → add `state` parameter
- [ ] Line 197: `make_string_value_from_cstr(func_name)` → add `state` parameter

### F. src/mobius/library/string.c (5 locations)
```c
// state is passed as MobiusState* parameter to library functions
// These use old string_create() - change to string_create(state, ...)
```
- [ ] Line 59: `string_create(upper_str)` → `string_create(state, upper_str)`
- [ ] Line 94: `string_create(lower_str)` → `string_create(state, lower_str)`
- [ ] Line 138: `make_string_value_from_cstr("")` → add `state` parameter
- [ ] Line 155: `string_create(substr_data)` → `string_create(state, substr_data)`
- [ ] Line 202: `string_create(result_data)` → `string_create(state, result_data)`

### G. src/mobius/eval/eval_table.c (1 location)
```c
// Get state from environment
MobiusState* state = env->current_context->state;
```
- [ ] Line 157: `make_string_value_from_cstr(key_str)` → add `state` parameter

### H. src/mobius/state/environment.c (3 locations)
```c
// Get state from environment
MobiusState* state = env->current_context->state;
```
- [ ] Line 38: `make_string_value_from_cstr(name)` → add `state` parameter
- [ ] Line 53: `make_string_value_from_cstr(name)` → add `state` parameter
- [ ] Line 74: `make_string_value_from_cstr(name)` → add `state` parameter

### I. src/mobius/frontend/parser.c (4 locations)
```c
// CHALLENGE: Parser may not have direct access to state
// May need to add MobiusState* to Parser struct or pass through parsing context
```
- [ ] Line 223: `make_string_value_from_cstr(token.literal.string)` → add `state` parameter
- [ ] Line 226: `make_string_value_from_cstr("")` → add `state` parameter
- [ ] Line 321: `make_string_value_from_cstr(key_str)` → add `state` parameter
- [ ] Line 1300: `string_create(token.literal.string)` → `string_create(state, ...)`

---

## 🗑️ Files to Delete After Migration

- [ ] `src/mobius/internal/refString.h` - Old API header
- [ ] `src/mobius/internal/refString.c` - Old implementation
- [ ] Update `Makefile` to remove refString.c from build

---

## 📦 Examples (Lower Priority)

### examples/game_engine/game_engine.c (1 location)
- [ ] Update to use new string API

### examples/text_processing_plugin/text_processing_plugin.c (8 locations)
- [ ] Update all string creation calls

### examples/embedding_example/embedding_example.c (2 locations)
- [ ] Update string creation calls

### examples/performance_tests/refcount_implementation_sample.c
- [ ] Review if still needed or delete

---

## 🧪 Testing After Migration

- [ ] Compile without errors: `make clean && make`
- [ ] Run basic tests: `./bin/mobius tests/basic/test_pragma.mob`
- [ ] Run string tests: `./bin/mobius tests/basic/test_string_operations.mob` (if exists)
- [ ] Run integration tests: `./bin/mobius tests/integration/test_clean_architecture.mob`
- [ ] Run stack API test: `./bin/stack_api_test`
- [ ] Verify no memory leaks: `valgrind ./bin/mobius <test_file>`

---

## 📝 Code Patterns Reference

### Pattern 1: Function has Environment parameter
```c
// OLD:
Value key = make_string_value_from_cstr("mykey");

// NEW:
MobiusState* state = env->current_context->state;
Value key = make_string_value_from_cstr(state, "mykey");
```

### Pattern 2: Function has MobiusState parameter
```c
// OLD:
Value key = make_string_value_from_cstr("mykey");

// NEW:
Value key = make_string_value_from_cstr(state, "mykey");
```

### Pattern 3: Old string_create() calls
```c
// OLD:
RefCountedString* str = string_create(data);

// NEW:
MobiusString* str = string_create(state, data);
```

### Pattern 4: Efficient string concatenation (see eval_arithmatic.c)
```c
// Use string_data() and string_length() from MobiusString
// Avoid strlen() calls - use precomputed lengths!
const char* data = string_data(my_string);
size_t len = string_length(my_string);
```

---

## Performance Benefits

✅ **Interning**: Identical strings share memory (single copy)
✅ **Fast comparison**: Pointer equality for interned strings
✅ **Precomputed hash**: O(1) hash lookup in tables
✅ **Cached length**: No strlen() calls needed
✅ **Lua-style hashing**: Fast for short strings, efficient sampling for long strings

---

## Notes

- All strings are immutable - modifications create new strings
- Interned strings live until MobiusState is destroyed
- Reference counting prepared for future GC implementation
- String comparison is layered: pointer → hash → length → sample → full content

