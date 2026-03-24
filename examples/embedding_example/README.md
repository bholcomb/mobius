# Comprehensive Embedding Example

This example demonstrates how to embed the Mobius scripting language in a C++
application, similar to embedding Lua or Python.

## Files

- `embedding_example.cpp` - C++ application demonstrating embedding features

## Features Demonstrated

- **Creating and managing interpreter states**
- **Executing Mobius scripts from C**
- **Exchanging values between C and Mobius via the stack API**
- **Registering custom C functions**
- **Error handling**

## Headers Used

Only two public headers are needed:

```cpp
#include <mobius/mobius.h>          // state lifecycle, execution, config
#include <mobius/mobius_plugin.h>   // stack API, native functions, value types
```

## What This Example Does

1. **Basic Execution** — creates a state, loads stdlib, runs a script
2. **Value Exchange** — sets globals from C with `mobius_stack_push*` /
   `mobius_stack_setGlobal`, reads them back with `mobius_stack_getGlobal` /
   `mobius_stack_get*`
3. **Custom Functions** — registers `c_add`, `system_info`, and `double` via
   `mobius_register_function`
4. **Advanced Script** — runs Fibonacci and math from an embedded script
5. **Error Handling** — demonstrates syntax, runtime, and type errors
6. **File Execution** — writes a temporary `.mob` file and executes it

## Custom Function Pattern

Every native function uses the public stack API:

```cpp
int custom_add(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "custom_add requires exactly 2 arguments");

    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2))
        return mobius_error(state, "custom_add arguments must be numbers");

    double a = mobius_stack_asFloat64(state, -2);
    double b = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, a + b);
    return 1;
}
```

## Building and Running

```bash
# From the project root
./buildy -r -n 3

# Run
./bin/embedding_example
```

## API Functions Used

| Function | Purpose |
|----------|---------|
| `mobius_new_state(config)` | Create interpreter instance |
| `mobius_init_stdlib(state)` | Load standard library |
| `mobius_exec_string(state, code)` | Execute a script string |
| `mobius_exec_file(state, path)` | Execute a script file |
| `mobius_register_function(state, name, fn)` | Register a native C function |
| `mobius_stack_push*(state, val)` | Push values onto the stack |
| `mobius_stack_as*(state, idx)` | Read values from the stack |
| `mobius_stack_setGlobal(state, name)` | Pop top value into a global |
| `mobius_stack_getGlobal(state, name)` | Push a global onto the stack |
| `mobius_error(state, msg)` | Report an error from a native function |
| `mobius_clear_error(state)` | Clear the last error |
| `mobius_free_state(state)` | Destroy interpreter and free resources |
