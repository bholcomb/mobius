# Mobius Table Implementation Plan

## Overview
Adding Lua-style tables to Mobius to enable dynamic data structures and object-oriented programming through metatables.

## Table Design Specifications

### Core Features
- **Pure hash table**: Single hash table for all key types (no array part)
- **Dynamic resizing**: Automatic growth and rehashing
- **Metatable support**: Full metamethod system for operator overloading
- **Mixed indexing**: `table[key]` and `table.key` syntax
- **Iteration support**: pairs() function for key-value iteration

### Syntax Examples
```javascript
// Table literals
var empty = {};
var config = {
    name: "Alice",
    age: 25,
    settings: {
        theme: "dark",
        lang: "en"
    }
};

// Access patterns
print(config.name);        // "Alice"
print(config["age"]);      // 25
print(config.settings.theme); // "dark"

// Assignment
config.city = "New York";
config["country"] = "USA";
config.settings.notifications = true;

// Metatables for OOP
var Point = {};
Point.__index = Point;

func Point.new(x, y) {
    var obj = {x: x, y: y};
    setmetatable(obj, Point);
    return obj;
}

func Point.distance(self, other) {
    var dx = self.x - other.x;
    var dy = self.y - other.y;
    return sqrt(dx*dx + dy*dy);
}

var p1 = Point.new(0, 0);
var p2 = Point.new(3, 4);
print(p1:distance(p2));  // 5
```

## Implementation Checklist

### Phase 1: Core Infrastructure (8 tasks)

#### 1. **Design Table Data Structure** 
- [ ] Define Table struct with array and hash parts
- [ ] Design Metatable struct with metamethod function pointers
- [ ] Plan memory layout and growth strategies
- [ ] Define table entry structures (key-value pairs)

#### 2. **Update Value System**
- [ ] Add `VAL_TABLE` to ValueType enum in `ast.h`
- [ ] Update all switch statements handling ValueType
- [ ] Add table pointer to Value union
- [ ] Update `value_type_name()` function

#### 3. **Token System Updates**
- [ ] Add table syntax tokens to TokenType enum
  - `TOKEN_LEFT_BRACE` (`{`)
  - `TOKEN_RIGHT_BRACE` (`}`)
  - `TOKEN_LEFT_BRACKET` (`[`)
  - `TOKEN_RIGHT_BRACKET` (`]`)
  - `TOKEN_COLON` (`:`) for key-value syntax
- [ ] Update `token_type_name()` function

#### 4. **Core Table Structure**
```c
typedef struct TableEntry {
    Value key;
    Value value;
    struct TableEntry* next;  // For collision chaining
    bool is_occupied;
} TableEntry;

typedef struct Table {
    // Hash table for all keys
    TableEntry* entries;
    size_t size;        // Number of key-value pairs
    size_t capacity;    // Size of entries array
    
    // Metatable for operator overloading
    struct Table* metatable;
    
    // Reference counting for memory management
    int ref_count;
} Table;
```

#### 5. **Metatable Structure**
```c
typedef struct Metatable {
    Table* base_table;
    
    // Core metamethods
    Value __index;      // table[key] when key not found
    Value __newindex;   // table[key] = value when key not found
    Value __tostring;   // tostring(table)
    Value __len;        // #table
    
    // Arithmetic metamethods
    Value __add;        // table1 + table2
    Value __sub;        // table1 - table2
    Value __mul;        // table1 * table2
    Value __div;        // table1 / table2
    Value __mod;        // table1 % table2
    Value __pow;        // table1 ^ table2
    Value __unm;        // -table
    
    // Comparison metamethods
    Value __eq;         // table1 == table2
    Value __lt;         // table1 < table2
    Value __le;         // table1 <= table2
    
    // Special metamethods
    Value __call;       // table(args...)
    Value __concat;     // table1 .. table2
} Metatable;
```

### Phase 2: Scanner and Parser (4 tasks)

#### 6. **Scanner Updates**
- [ ] Add table literal token recognition in `scan_token()`
- [ ] Handle `{`, `}`, `[`, `]`, `:` characters
- [ ] Update scanner tests

#### 7. **Parser - Table Literals**
- [ ] Add `parse_table_literal()` function
- [ ] Handle empty tables: `{}`
- [ ] Handle array-style: `{1, 2, 3}`
- [ ] Handle object-style: `{key: value, name: "test"}`
- [ ] Handle computed keys: `{[expr]: value}`
- [ ] Handle mixed syntax: `{1, 2, key: "value", [expr]: data}`

#### 8. **Parser - Table Indexing**
- [ ] Add table indexing expressions: `table[key]`
- [ ] Add dot notation: `table.key` (syntactic sugar for `table["key"]`)
- [ ] Handle assignment: `table[key] = value`
- [ ] Update expression precedence

#### 9. **AST Nodes**
- [ ] Add `EXPR_TABLE_LITERAL` expression type
- [ ] Add `EXPR_TABLE_INDEX` expression type  
- [ ] Add `EXPR_TABLE_DOT` expression type
- [ ] Add corresponding AST structures

### Phase 3: Core Table Operations (4 tasks)

#### 10. **Table Creation Functions**
```c
Table* create_table(size_t initial_capacity);
void free_table(Table* table);
Value make_table_value(Table* table);
Table* table_copy(Table* source);  // Shallow copy
```

#### 11. **Table Access Operations**
```c
Value table_get(Table* table, Value key);
bool table_set(Table* table, Value key, Value value);
bool table_has_key(Table* table, Value key);
bool table_remove(Table* table, Value key);
size_t table_size(Table* table);  // Number of key-value pairs
```

#### 12. **Hash Table Implementation**
- [ ] Implement hash function for different key types
- [ ] Implement collision resolution (chaining)
- [ ] Implement dynamic resizing and rehashing
- [ ] Optimize for common cases (string keys, integer keys)

#### 13. **Hash Optimization**
- [ ] Optimize hash function for string and number keys
- [ ] Implement efficient collision resolution
- [ ] Dynamic load factor management and resizing
- [ ] Cache-friendly entry layout

### Phase 4: Metatable System (3 tasks)

#### 14. **Metatable Core Functions**
```c
Table* create_metatable();
void set_metatable(Table* table, Table* metatable);
Table* get_metatable(Table* table);
Value call_metamethod(Table* table, const char* method_name, Value* args, size_t arg_count);
```

#### 15. **Metamethod Implementation**
- [ ] `__index` - Handle missing key lookup
- [ ] `__newindex` - Handle assignment to missing key
- [ ] `__tostring` - Custom string representation
- [ ] `__len` - Custom length operator

#### 16. **Operator Overloading**
- [ ] Integrate metamethods with binary operators in evaluator
- [ ] Handle arithmetic: `+`, `-`, `*`, `/`, `%`, `^`
- [ ] Handle comparison: `==`, `<`, `<=`
- [ ] Handle unary minus: `-table`

### Phase 5: Built-in Functions (2 tasks)

#### 17. **Table Library Functions**
```c
// Core table manipulation
EvalResult builtin_table_insert(Value* args, size_t arg_count);  // Insert at key
EvalResult builtin_table_remove(Value* args, size_t arg_count);  // Remove by key
EvalResult builtin_table_has_key(Value* args, size_t arg_count); // Check if key exists
EvalResult builtin_table_size(Value* args, size_t arg_count);    // Get table size

// Metatable functions
EvalResult builtin_setmetatable(Value* args, size_t arg_count);
EvalResult builtin_getmetatable(Value* args, size_t arg_count);
```

#### 18. **Iteration Functions**
```c
// Iterator function (only one needed)
EvalResult builtin_pairs(Value* args, size_t arg_count);    // All key-value pairs
EvalResult builtin_next(Value* args, size_t arg_count);     // Next key-value pair
```

### Phase 6: Integration and Memory Management (3 tasks)

#### 19. **Value System Integration**
- [ ] Update `copy_value()` to handle tables (reference counting)
- [ ] Update `free_value()` to handle table cleanup
- [ ] Update `values_equal()` for table comparison
- [ ] Update `print_value()` and `value_to_string()` for tables

#### 20. **Memory Management**
- [ ] Implement reference counting for tables
- [ ] Handle circular references in metatables
- [ ] Integrate with existing memory cleanup systems
- [ ] Add memory leak detection for tables

#### 21. **Performance Optimization**
- [ ] Optimize hash function for string and number keys
- [ ] Implement table caching for commonly accessed keys
- [ ] Optimize hash table load factor and growth strategy
- [ ] Profile and optimize common operations

### Phase 7: Testing and Documentation (3 tasks)

#### 22. **Comprehensive Testing**
- [ ] Unit tests for table creation and basic operations
- [ ] Tests for metatable functionality
- [ ] Performance tests for large tables
- [ ] Memory leak tests
- [ ] Integration tests with existing language features

#### 23. **Example Programs**
- [ ] Basic table usage examples
- [ ] Object-oriented programming examples
- [ ] Data structure implementations (stacks, queues, sets)
- [ ] Real-world use cases

#### 24. **Documentation Updates**
- [ ] Update language reference with table syntax
- [ ] Document all metamethods and their behavior
- [ ] Add table programming guide
- [ ] Update embedding API documentation

## Technical Considerations

### Memory Management Strategy
- **Reference Counting**: Tables use reference counting for automatic cleanup
- **Circular Reference Detection**: Special handling for metatable cycles
- **Weak References**: Consider weak table support for advanced use cases

### Performance Targets
- **Hash Table**: O(1) average case for get/set operations
- **Dynamic Resizing**: Amortized O(1) insertion and deletion
- **Metatable Lookup**: Minimal overhead when metamethods not used

### Compatibility
- **Lua-like Semantics**: Follow Lua table behavior where possible
- **Mobius Integration**: Integrate cleanly with existing type system
- **C API**: Expose table operations through embedding API

## Implementation Order

1. **Start with Phase 1**: Core infrastructure and data structures
2. **Phase 2**: Scanner and parser support for syntax
3. **Phase 3**: Basic table operations without metatables
4. **Phase 4**: Add metatable system incrementally
5. **Phase 5**: Built-in functions and iteration
6. **Phase 6**: Polish integration and optimization
7. **Phase 7**: Testing and documentation

## Estimated Effort
- **Total Tasks**: 22 major implementation tasks
- **Estimated Time**: 2-3 weeks for full implementation
- **Testing Phase**: Additional 1 week for comprehensive testing
- **Documentation**: Additional 3-5 days for complete documentation

This implementation will transform Mobius into a much more powerful scripting language with modern data structure capabilities and object-oriented programming support.
