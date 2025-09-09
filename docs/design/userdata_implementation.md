# Userdata Implementation in Mobius

## Overview

The userdata type has been successfully implemented in the Mobius scripting language to enable seamless C/C++ object integration. This feature allows embedding applications to expose C++ classes and objects to Mobius scripts with proper lifecycle management and type safety.

## Core Implementation

### 1. Value System Integration

- **Added `VAL_USERDATA` to ValueType enum**: New value type for opaque pointers
- **Extended Value union**: Added userdata struct with:
  - `void* ptr`: Opaque pointer to user data
  - `UserdataDestructor destructor`: Optional cleanup function
  - `const char* type_name`: Type identifier for runtime checks
  - `size_t size`: Size of the data (for debugging/GC)

### 2. Value Management Functions

- **`make_userdata_value()`**: Creates userdata values
- **Updated `copy_value()`**: Shared reference semantics for userdata
- **Updated `free_value()`**: Automatic destructor calls
- **Updated `print_value()`**: Human-readable userdata display
- **Updated `value_to_string()`**: String conversion support
- **Updated `values_equal()`**: Pointer and type-based equality

### 3. Table Integration

- **Hash function support**: Userdata can be used as table keys
- **Equality function**: Proper userdata comparison in tables
- **Reference semantics**: Tables can safely store userdata values

### 4. Embedding API

New functions added to `embedding.h` and `embedding.c`:

```c
// Creation
MobiusValue* mobius_create_userdata(MobiusState* state, void* ptr, 
                                   UserdataDestructor destructor, 
                                   const char* type_name, size_t size);

// Type checking
bool mobius_is_userdata(const MobiusValue* value);
bool mobius_is_userdata_type(const MobiusValue* value, const char* type_name);

// Value extraction
void* mobius_to_userdata(const MobiusValue* value);
const char* mobius_userdata_type(const MobiusValue* value);
size_t mobius_userdata_size(const MobiusValue* value);
```

## C++ Compatibility

### Fixed C++ Keyword Issues

The implementation required fixing several C++ keyword conflicts:

1. **`operator` keyword**: Changed all `operator` fields and parameters to `op`
   - `BinaryExpr.operator` → `BinaryExpr.op`
   - `UnaryExpr.operator` → `UnaryExpr.op`
   - Function parameters updated throughout codebase

2. **`class` keyword**: Changed `ClassStmt class` to `ClassStmt class_stmt`

These changes ensure the Mobius headers can be included in C++ code without conflicts.

## Example Usage

### C++ Class Binding

```cpp
// C++ class
class Vector3 {
    double x_, y_, z_;
public:
    Vector3(double x, double y, double z) : x_(x), y_(y), z_(z) {}
    double length() const { return sqrt(x_*x_ + y_*y_ + z_*z_); }
    // ... other methods
};

// Destructor function
void vector3_destructor(void* ptr) {
    delete static_cast<Vector3*>(ptr);
}

// Constructor binding
int vector3_new(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    double x = mobius_to_float(args[0]);
    double y = mobius_to_float(args[1]);
    double z = mobius_to_float(args[2]);
    
    Vector3* vec = new Vector3(x, y, z);
    *result = mobius_create_userdata(state, vec, vector3_destructor, "Vector3", sizeof(Vector3));
    return MOBIUS_OK;
}

// Method binding
int vector3_length(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    if (!mobius_is_userdata_type(args[0], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "Expected Vector3");
    }
    
    Vector3* vec = static_cast<Vector3*>(mobius_to_userdata(args[0]));
    *result = mobius_create_float(state, vec->length());
    return MOBIUS_OK;
}

// Registration
mobius_register_function(state, "Vector3", vector3_new, 3, "Create Vector3(x,y,z)");
mobius_register_function(state, "vector3_length", vector3_length, 1, "Get vector length");
```

### Mobius Script Usage

```javascript
var v1 = Vector3(1.0, 2.0, 3.0);
var v2 = Vector3(4.0, 5.0, 6.0);

print("v1 length:", vector3_length(v1));
var v3 = vector3_add(v1, v2);

// Objects are automatically cleaned up when they go out of scope
```

## Key Features

### 1. Automatic Memory Management

- **Destructor calls**: Userdata destructors are automatically called when values are freed
- **Reference semantics**: Copying userdata values shares the same underlying object
- **Scope-based cleanup**: Objects are destroyed when they go out of scope in Mobius

### 2. Type Safety

- **Runtime type checking**: `mobius_is_userdata_type()` ensures type safety
- **Type names**: Each userdata has an associated type identifier
- **Error handling**: Type mismatches produce clear error messages

### 3. Table Integration

- **Keys and values**: Userdata can be used as both table keys and values
- **Hash function**: Proper hashing based on pointer and type
- **Equality**: Userdata equality based on pointer identity and type

### 4. Embedding Integration

- **Full API**: Complete set of creation, access, and type checking functions
- **Error handling**: Proper error reporting for type mismatches
- **Memory management**: Integrated with Mobius memory management

## Building with C++

The C++ example can be built with:

```bash
make examples  # Builds all examples including cpp_class_example
```

Or manually:

```bash
g++ -Wall -Wextra -std=c++11 -g -O0 -Isrc -o bin/cpp_class_example \
    examples/cpp_class_example.cpp -Lbuild -lmobius -lm -ldl
```

## Limitations and Future Work

### Current Limitations

1. **Function registration**: The embedding API stores custom functions but doesn't yet integrate them with the evaluator
2. **Garbage collection**: No automatic GC yet, relies on scope-based cleanup
3. **Metamethods**: Userdata doesn't yet support Lua-style metamethods

### Future Enhancements

1. **Complete function registration**: Integrate custom functions with evaluator
2. **Userdata metamethods**: Add support for operator overloading
3. **Smart pointer support**: Better integration with C++ RAII patterns
4. **Class inheritance**: Support for C++ inheritance hierarchies

## Testing

The implementation includes:

- **Build verification**: All code compiles without warnings
- **C++ compatibility**: Headers work correctly in C++ code
- **Basic functionality**: Value creation, copying, and cleanup work
- **Example code**: Complete C++ class binding example

The userdata implementation provides a solid foundation for integrating C++ objects with Mobius scripts, enabling powerful scripting capabilities for embedded applications.
