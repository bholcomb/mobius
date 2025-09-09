# C++ Class Binding Example

This example demonstrates how to bind C++ classes to the Mobius scripting language using the userdata type. It shows how to expose C++ objects and methods to Mobius scripts with proper memory management and type safety.

## Files

- `cpp_class_example.cpp` - C++ application demonstrating class binding to Mobius

## Features Demonstrated

- **Creating C++ objects accessible from Mobius**
- **Binding C++ methods as Mobius functions**
- **Automatic cleanup when objects go out of scope**
- **Type safety and error handling**
- **Userdata type system integration**

## Example Classes

### Vector3 Class
A simple 3D vector class with mathematical operations:

```cpp
class Vector3 {
private:
    double x_, y_, z_;

public:
    Vector3(double x = 0, double y = 0, double z = 0) : x_(x), y_(y), z_(z) {}
    
    // Getters and setters
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    void setZ(double z) { z_ = z; }
    
    // Vector operations
    double length() const {
        return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_);
    }
    
    Vector3 normalize() const {
        double len = length();
        if (len > 0) {
            return Vector3(x_ / len, y_ / len, z_ / len);
        }
        return Vector3();
    }
    
    double dot(const Vector3& other) const {
        return x_ * other.x_ + y_ * other.y_ + z_ * other.z_;
    }
    
    Vector3 cross(const Vector3& other) const {
        return Vector3(
            y_ * other.z_ - z_ * other.y_,
            z_ * other.x_ - x_ * other.z_,
            x_ * other.y_ - y_ * other.x_
        );
    }
};
```

### Calculator Class
A stateful calculator with memory:

```cpp
class Calculator {
private:
    double memory_;
    
public:
    Calculator() : memory_(0.0) {}
    
    double add(double a, double b) { return a + b; }
    double subtract(double a, double b) { return a - b; }
    double multiply(double a, double b) { return a * b; }
    double divide(double a, double b) {
        if (b == 0.0) throw std::runtime_error("Division by zero");
        return a / b;
    }
    
    void store(double value) { memory_ = value; }
    double recall() const { return memory_; }
    void clear() { memory_ = 0.0; }
};
```

## Binding Implementation

### 1. Creating Userdata Objects

```cpp
// Create Vector3 constructor function
EvalResult vector3_new(Value* args, size_t arg_count) {
    double x = (arg_count > 0) ? args[0].as.float_val : 0.0;
    double y = (arg_count > 1) ? args[1].as.float_val : 0.0;
    double z = (arg_count > 2) ? args[2].as.float_val : 0.0;
    
    Vector3* vec = new Vector3(x, y, z);
    
    // Create userdata wrapper
    UserdataValue* userdata = userdata_create(vec, "Vector3", vector3_destructor);
    
    return make_success(make_userdata_value(userdata));
}

// Destructor function
void vector3_destructor(void* ptr) {
    delete static_cast<Vector3*>(ptr);
}
```

### 2. Method Binding

```cpp
// Bind Vector3.length() method
EvalResult vector3_length(Value* args, size_t arg_count) {
    if (arg_count != 1 || args[0].type != VAL_USERDATA) {
        return make_error("vector3_length() expects a Vector3 object", 0, 0);
    }
    
    UserdataValue* userdata = args[0].as.userdata;
    if (strcmp(userdata->type_name, "Vector3") != 0) {
        return make_error("vector3_length() expects a Vector3 object", 0, 0);
    }
    
    Vector3* vec = static_cast<Vector3*>(userdata->data);
    double length = vec->length();
    
    return make_success(make_float_value(length));
}
```

### 3. Function Registration

```cpp
// Register constructor and methods
mobius_register_function(state, "Vector3", vector3_new, 
                        "Create a new Vector3(x, y, z)");
mobius_register_function(state, "vector3_length", vector3_length,
                        "Get length of Vector3");
mobius_register_function(state, "vector3_normalize", vector3_normalize,
                        "Normalize Vector3");
mobius_register_function(state, "vector3_dot", vector3_dot,
                        "Dot product of two Vector3s");
mobius_register_function(state, "vector3_cross", vector3_cross,
                        "Cross product of two Vector3s");
```

## Usage in Mobius Scripts

### Creating and Using Vector3 Objects

```javascript
// Create vectors
var v1 = Vector3(1.0, 2.0, 3.0);
var v2 = Vector3(4.0, 5.0, 6.0);

// Get properties
print("v1 length:", vector3_length(v1));
print("v2 length:", vector3_length(v2));

// Vector operations
var normalized = vector3_normalize(v1);
print("v1 normalized length:", vector3_length(normalized));

var dot_product = vector3_dot(v1, v2);
print("v1 · v2 =", dot_product);

var cross_product = vector3_cross(v1, v2);
print("v1 × v2 length:", vector3_length(cross_product));
```

### Using Calculator Objects

```javascript
// Create calculator
var calc = Calculator();

// Basic operations
print("5 + 3 =", calculator_add(calc, 5, 3));
print("10 - 4 =", calculator_subtract(calc, 10, 4));
print("6 * 7 =", calculator_multiply(calc, 6, 7));
print("15 / 3 =", calculator_divide(calc, 15, 3));

// Memory operations
calculator_store(calc, 42);
print("Stored value:", calculator_recall(calc));

calculator_clear(calc);
print("After clear:", calculator_recall(calc));
```

## Memory Management

### Automatic Cleanup

```cpp
// Objects are automatically destroyed when:
// 1. The userdata value is garbage collected
// 2. The interpreter state is freed
// 3. The script scope ends

// The destructor function is called automatically:
void vector3_destructor(void* ptr) {
    delete static_cast<Vector3*>(ptr);
    std::cout << "Vector3 object destroyed" << std::endl;
}
```

### Reference Counting

```cpp
// Userdata uses reference counting for memory management
// Objects are kept alive as long as references exist
var v1 = Vector3(1, 2, 3);
var v2 = v1;  // v2 references the same object
// Object destroyed only when both v1 and v2 go out of scope
```

## Building and Running

### Prerequisites
- G++ with C++11 support
- Mobius library (built with `make`)

### Building
```bash
make examples
```

### Running
```bash
./bin/cpp_class_example
```

## Expected Output

```
🚀 C++ Class Binding Example
============================

Creating Vector3 objects...
v1 = (1, 2, 3)
v2 = (4, 5, 6)

Vector operations:
v1 length: 3.741657387
v2 length: 8.774964387

Normalized v1 length: 1

Dot product v1 · v2: 32
Cross product v1 × v2 length: 3.741657387

Calculator operations:
5 + 3 = 8
10 - 4 = 6  
6 * 7 = 42
15 / 3 = 5

Memory operations:
Stored: 42
Recalled: 42
After clear: 0

✅ C++ binding example completed!
```

## Best Practices

### 1. Type Safety

```cpp
// Always validate userdata type
if (strcmp(userdata->type_name, "Vector3") != 0) {
    return make_error("Expected Vector3 object", 0, 0);
}
```

### 2. Exception Handling

```cpp
// Wrap C++ exceptions for Mobius
try {
    double result = calc->divide(a, b);
    return make_success(make_float_value(result));
} catch (const std::exception& e) {
    return make_error(e.what(), 0, 0);
}
```

### 3. Resource Management

```cpp
// Provide proper destructor
void my_class_destructor(void* ptr) {
    delete static_cast<MyClass*>(ptr);
}

// Register with userdata creation
UserdataValue* userdata = userdata_create(obj, "MyClass", my_class_destructor);
```

## Advanced Patterns

### 1. Inheritance Simulation

```cpp
// Base class methods work on derived objects
if (is_derived_from(userdata->type_name, "Shape")) {
    // Common operations for all shapes
}
```

### 2. Method Chaining

```cpp
// Enable fluent interfaces
var result = vector.normalize().scale(2.0).translate(1, 0, 0);
```

### 3. Operator Overloading

```cpp
// Implement operators as functions
mobius_register_function(state, "vector3_add", vector3_add_op, "v1 + v2");
// Usage: var result = vector3_add(v1, v2);
```

This example demonstrates the complete integration of C++ object-oriented programming with Mobius scripting, enabling powerful hybrid applications that leverage both C++ performance and Mobius flexibility!
