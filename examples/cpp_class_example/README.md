# C++ Class Binding Example

This example demonstrates how to bind C++ classes to Mobius using the userdata
type. It shows how to expose C++ objects and methods to scripts with proper
memory management and type safety.

## Files

- `cpp_class_example.cpp` - C++ application demonstrating class binding

## Headers Used

```cpp
#include <mobius/mobius.h>          // state lifecycle, execution
#include <mobius/mobius_plugin.h>   // stack API, native functions, userdata
```

## Example Classes

### Vector3

A 3D vector class with mathematical operations (length, normalize, add, dot
product).

### PlayerObj

A game player with name, position, health, and damage handling.

## Binding Pattern

### 1. Destructor

```cpp
void vector3_destructor(void* ptr) {
    if (ptr) delete static_cast<Vector3*>(ptr);
}
```

### 2. Constructor Function

```cpp
int vector3_new(MobiusState* state, int arg_count) {
    double x = 0, y = 0, z = 0;
    if (arg_count >= 1) x = mobius_stack_asFloat64(state, -arg_count);
    if (arg_count >= 2) y = mobius_stack_asFloat64(state, -arg_count + 1);
    if (arg_count >= 3) z = mobius_stack_asFloat64(state, -arg_count + 2);
    if (arg_count > 0) mobius_stack_pop(state, arg_count);

    Vector3* vec = new Vector3(x, y, z);
    mobius_stack_pushUserdata(state, vec, vector3_destructor,
                             "Vector3", sizeof(Vector3));
    return 1;
}
```

### 3. Method Binding

```cpp
int vector3_length_fn(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "vector3_length() expects 1 argument");

    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, -1, &type_name);
    if (!type_name || strcmp(type_name, "Vector3") != 0)
        return mobius_error(state, "vector3_length() expects a Vector3");

    double len = static_cast<Vector3*>(ptr)->length();
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, len);
    return 1;
}
```

### 4. Registration

```cpp
mobius_register_function(state, "Vector3", vector3_new);
mobius_register_function(state, "vector3_length", vector3_length_fn);
```

## Usage in Mobius Scripts

```mobius
var v1 = Vector3(1.0, 2.0, 3.0);
var v2 = Vector3(4.0, 5.0, 6.0);

print("v1 length:", vector3_length(v1));
print("v1 + v2 =", vector3_tostring(vector3_add(v1, v2)));
print("dot:", vector3_dot(v1, v2));

var player = Player("Hero");
print(player_tostring(player));
player_take_damage(player, 30);
print("Alive?", player_is_alive(player));
```

## Memory Management

Objects are automatically destroyed when the interpreter state is freed. The
destructor function registered with `mobius_stack_pushUserdata` is called for
each live userdata value.

## Building and Running

```bash
./buildy -r -n 3
./bin/cpp_class_example
```
