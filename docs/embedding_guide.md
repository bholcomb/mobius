# Embedding Mobius in C/C++ Applications

This guide explains how to integrate the Mobius scripting language into your
C or C++ application. Mobius provides a clean C API that handles state
management, script execution, value exchange, and native function
registration.

**Headers you need:**

- `<mobius/mobius.h>` — core embedding API (state lifecycle, execution, config)
- `<mobius/mobius_plugin.h>` — stack API, native functions, value types

Mobius variables are **type-locked**: a variable's type is inferred from its
first non-nil assignment and cannot change afterward. Scripts may optionally use
**`int64`**, **`uint64`**, and **`float64`** as numeric type annotations;
integers live as **`int64`** in the VM. The stack API still offers
**`pushInt8`**, **`asInt32`**, and other narrow helpers: they convert at the C
boundary for backward compatibility. Prefer **`pushInt64`** / **`pushUInt64`**
and **`asInt64`** / **`asUInt64`** (and **`Float64`**) in new embedding code.

---

## Table of Contents

1. [Minimal Example](#minimal-example)
2. [State Lifecycle](#state-lifecycle)
3. [Configuration](#configuration)
4. [Executing Scripts](#executing-scripts)
5. [The Stack](#the-stack)
6. [Exchanging Values with Scripts](#exchanging-values-with-scripts)
7. [Registering Native Functions](#registering-native-functions)
8. [Error Handling](#error-handling)
9. [Loading Plugins](#loading-plugins)
10. [Multiple Interpreter Instances](#multiple-interpreter-instances)
11. [Concurrency and Fibers](#concurrency-and-fibers)
12. [API Reference Summary](#api-reference-summary)

---

## Minimal Example

```c
#include <mobius/mobius.h>

int main(void) {
    // 1. Create an interpreter
    MobiusState* state = mobius_new_state(NULL);

    // 2. Load the standard library
    mobius_init_stdlib(state);

    // 3. Run a script
    mobius_exec_string(state, "print(\"Hello from Mobius!\")");

    // 4. Clean up
    mobius_free_state(state);
    return 0;
}
```

Link against `libmobius-core` when compiling:

```bash
g++ -o my_app my_app.cpp -lmobius-core -ldl
```

---

## State Lifecycle

Every Mobius program runs inside a `MobiusState`, an opaque handle that owns
the interpreter, its globals, its stack, and all allocated objects.

```c
// Create with default settings
MobiusState* state = mobius_new_state(NULL);

// Or with a custom configuration (see next section)
MobiusConfig config = mobius_default_config();
config.debug_mode = true;
MobiusState* state = mobius_new_state(&config);

// Load standard library (print, typeof, math, string, array, table functions)
int err = mobius_init_stdlib(state);
// err == MOBIUS_OK on success

// ... use the interpreter ...

// Destroy the interpreter and free all resources
mobius_free_state(state);
```

Always call `mobius_init_stdlib()` after creating the state — without it,
scripts won't have access to `print`, `typeof`, or any built-in functions.

---

## Configuration

`MobiusConfig` controls interpreter behavior. Get the defaults, adjust what
you need, and pass a pointer to `mobius_new_state()`.

```c
MobiusConfig config = mobius_default_config();
```

| Field                | Type                    | Default      | Description                                 |
|----------------------|-------------------------|--------------|---------------------------------------------|
| `initial_stack_size` | `size_t`                | (internal)   | Initial capacity of the value stack         |
| `max_stack_size`     | `size_t`                | (internal)   | Maximum stack depth before overflow error   |
| `max_call_depth`     | `size_t`                | (internal)   | Maximum function call nesting depth         |
| `strict_mode`        | `bool`                  | `false`      | Enforce type annotations at runtime         |
| `warn_on_conversion` | `bool`                  | `false`      | Warn on implicit type conversions           |
| `debug_mode`         | `bool`                  | `false`      | Enable extra debug output                   |
| `enable_hot_reload`  | `bool`                  | `false`      | Enable hot-reload support                   |
| `override_behavior`  | `MobiusOverrideBehavior`| `MOBIUS_OVERRIDE_ERROR` | How to handle global name conflicts |
| `fiber_stack_size`   | `size_t`                | `131072`     | Size (bytes) of each fiber's C stack        |
| `initial_fiber_pool_size` | `size_t`           | `16`         | Pre-allocated fibers in the pool            |
| `max_fiber_pool_size`| `size_t`                | `1024`       | Maximum number of pooled fibers             |
| `max_worker_threads` | `size_t`                | `4`          | Worker threads for fiber scheduling         |

### Override Behavior

| Value                  | Effect                                          |
|------------------------|-------------------------------------------------|
| `MOBIUS_OVERRIDE_ERROR`| Error on function/global name conflicts (default)|
| `MOBIUS_OVERRIDE_WARN` | Warn but allow the override                     |
| `MOBIUS_OVERRIDE_QUIET`| Silently allow overrides                        |

---

## Executing Scripts

### From a String

```c
int result = mobius_exec_string(state, "var x = 42\nprint(x)");
if (result != MOBIUS_OK) {
    // handle error
}
```

### From a File

```c
int result = mobius_exec_file(state, "scripts/init.mob");
if (result != MOBIUS_OK) {
    // handle error
}
```

Both functions return `MOBIUS_OK` (0) on success. On failure, the registered
error handler is invoked and an error code is returned.

### Error Codes

| Constant               | Value | Meaning                  |
|------------------------|-------|--------------------------|
| `MOBIUS_OK`            | 0     | Success                  |
| `MOBIUS_ERROR_SYNTAX`  | 1     | Parse / syntax error     |
| `MOBIUS_ERROR_RUNTIME` | 2     | Runtime error            |
| `MOBIUS_ERROR_TYPE`    | 3     | Type mismatch            |
| `MOBIUS_ERROR_ARGUMENT`| 4     | Wrong number of arguments|
| `MOBIUS_ERROR_MEMORY`  | 5     | Memory allocation failure|
| `MOBIUS_ERROR_FILE`    | 6     | File I/O error           |
| `MOBIUS_ERROR_PLUGIN`  | 7     | Plugin loading error     |

---

## The Stack

All data exchange between C and Mobius goes through a **value stack**, very
similar to Lua's stack model.

- **Positive indices** count from the bottom: 1 is the first element.
- **Negative indices** count from the top: -1 is the top element, -2 is one
  below the top, etc.
- **Push** functions add values to the top of the stack.
- **Pop** removes values from the top.

```
  Index:   1    2    3    ← positive (from bottom)
Stack:  [ 10 | "hi" | true ]
  Index:  -3   -2    -1   ← negative (from top)
```

### Stack Size

```c
int n = mobius_stack_size(state);
```

### Pushing Values

```c
mobius_stack_pushNil(state);
mobius_stack_pushBool(state, true);
mobius_stack_pushInt64(state, 42);
mobius_stack_pushUInt64(state, 1000000ULL);
mobius_stack_pushFloat64(state, 3.14);
mobius_stack_pushString(state, "hello");
```

**Recommended:** `pushInt64`, `pushUInt64`, `pushFloat64` (and matching `as` /
`get` functions). Narrow **`pushInt8`** … **`pushInt32`** and **`pushFloat32`**
remain available; they widen when pushing and the runtime stores integers as
**`int64`**.

### Reading Values

**Permissive getters** (`as` prefix) perform automatic type coercion:

```c
double   d = mobius_stack_asFloat64(state, -1);
int64_t  i = mobius_stack_asInt64(state, -1);
const char* s = mobius_stack_asString(state, -1);
bool     b = mobius_stack_asBool(state, -1);
```

**Strict getters** (`get` prefix) respect the strict-types pragma and error
on type mismatch:

```c
int64_t  i = mobius_stack_getInt64(state, -1);
const char* s = mobius_stack_getString(state, -1);
```

### Type Checking

```c
MobiusValueType type = mobius_stack_type(state, -1);

// Convenience predicates
if (mobius_stack_isNumber(state, -1))   { /* int or float */ }
if (mobius_stack_isInteger(state, -1))  { /* any int type */ }
if (mobius_stack_isFloat(state, -1))    { /* floating-point */ }
if (mobius_stack_isString(state, -1))   { /* string */ }
if (mobius_stack_isBool(state, -1))     { /* bool */ }
if (mobius_stack_isNil(state, -1))      { /* nil */ }
if (mobius_stack_isTable(state, -1))    { /* table */ }
if (mobius_stack_isArray(state, -1))    { /* array */ }
if (mobius_stack_isFunction(state, -1)) { /* function */ }
if (mobius_stack_isUserdata(state, -1)) { /* userdata */ }
```

### Stack Manipulation

```c
mobius_stack_pop(state, 2);      // pop 2 values from the top
mobius_stack_copy(state, -1);    // duplicate the top value
```

---

## Exchanging Values with Scripts

### Setting Globals from C

Push a value, then assign it to a named global:

```c
mobius_stack_pushString(state, "Mobius v1.0");
mobius_stack_setGlobal(state, "APP_VERSION");

mobius_stack_pushInt64(state, 8080);
mobius_stack_setGlobal(state, "PORT");
```

Now scripts can use these:

```mobius
print(APP_VERSION)    // "Mobius v1.0"
print(PORT)           // 8080
```

### Reading Globals from C

```c
mobius_stack_getGlobal(state, "result");
if (mobius_stack_isNumber(state, -1)) {
    double val = mobius_stack_asFloat64(state, -1);
    printf("result = %f\n", val);
}
mobius_stack_pop(state, 1);
```

### Working with Tables

```c
// Create a new table
mobius_stack_pushNewTable(state, 4);    // capacity hint

// Set fields: push value, then call setTableField
mobius_stack_pushString(state, "Alice");
mobius_stack_setTableField(state, -2, "name");

mobius_stack_pushInt64(state, 30);
mobius_stack_setTableField(state, -2, "age");

// Assign the table to a global
mobius_stack_setGlobal(state, "player");

// Read a field back
mobius_stack_getGlobal(state, "player");
mobius_stack_getTableField(state, -1, "name");
const char* name = mobius_stack_asString(state, -1);
printf("Player name: %s\n", name);
mobius_stack_pop(state, 2);
```

### Working with Arrays

```c
mobius_stack_pushNewArray(state, 8);

// Add elements: push value, then set by index
mobius_stack_pushString(state, "first");
mobius_stack_setArrayElement(state, -2, 0);

mobius_stack_pushString(state, "second");
mobius_stack_setArrayElement(state, -2, 1);

// Query length
size_t len = mobius_stack_getArrayLength(state, -1);

// Read an element
mobius_stack_getArrayElement(state, -1, 0);
const char* elem = mobius_stack_asString(state, -1);
mobius_stack_pop(state, 1);

// Assign to global
mobius_stack_setGlobal(state, "items");
```

### Userdata

Userdata lets you pass arbitrary C pointers into Mobius scripts. Attach a
destructor to handle cleanup when the value is garbage collected.

```c
typedef struct { float x, y, z; } Vec3;

void vec3_destroy(void* ptr) {
    free(ptr);
}

// Push userdata
Vec3* v = malloc(sizeof(Vec3));
v->x = 1.0f; v->y = 2.0f; v->z = 3.0f;
mobius_stack_pushUserdata(state, v, vec3_destroy, "Vec3", sizeof(Vec3));
mobius_stack_setGlobal(state, "position");

// Retrieve userdata
mobius_stack_getGlobal(state, "position");
const char* type_name = NULL;
Vec3* pos = (Vec3*)mobius_stack_getUserdata(state, -1, &type_name);
printf("Position: (%.1f, %.1f, %.1f) type=%s\n", pos->x, pos->y, pos->z, type_name);
mobius_stack_pop(state, 1);
```

---

## Registering Native Functions

Native C functions extend the scripting environment. Register them and they
become callable from any Mobius script.

### Function Signature

Every native function has this signature:

```c
int my_function(MobiusState* state, int arg_count);
```

- `arg_count` is the number of arguments the script passed.
- Arguments are on the stack (last argument at index -1).
- Pop/read arguments, push return values, and return the number of values
  pushed (>= 0).
- On error, call `mobius_error()` and return its result.

### Example: A Simple Function

```c
int native_greet(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "greet() expects 1 argument");
    }

    if (!mobius_stack_isString(state, -1)) {
        return mobius_error(state, "greet() expects a string");
    }

    const char* name = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    char buf[256];
    snprintf(buf, sizeof(buf), "Hello, %s!", name);
    mobius_stack_pushString(state, buf);
    return 1;    // one return value
}
```

### Registration

```c
mobius_register_function(state, "greet", native_greet);
```

Now scripts can call it:

```mobius
print(greet("World"))    // "Hello, World!"
```

### Complete Example

```c
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>

int native_square(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "square() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "square() expects a number");

    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, val * val);
    return 1;
}

int main(void) {
    MobiusState* state = mobius_new_state(NULL);
    mobius_init_stdlib(state);
    mobius_register_function(state, "square", native_square);

    mobius_exec_string(state, "print(\"9^2 =\", square(9))");

    mobius_free_state(state);
    return 0;
}
```

---

## Error Handling

### Default Behavior

By default, errors are printed to `stderr` with line/column information.

### Custom Error Handler

Install a callback to intercept errors:

```c
void my_error_handler(MobiusState* state, const MobiusError* error, void* userdata) {
    FILE* log = (FILE*)userdata;
    fprintf(log, "[ERROR %d] Line %d, Col %d: %s\n",
            error->code, error->line, error->column, error->message);
    if (error->suggestion) {
        fprintf(log, "  Suggestion: %s\n", error->suggestion);
    }
    if (error->function_name) {
        fprintf(log, "  In function: %s\n", error->function_name);
    }
}

// Install the handler
FILE* logfile = fopen("errors.log", "w");
mobius_set_error_handler(state, my_error_handler, logfile);
```

The `MobiusError` struct fields:

| Field           | Type          | Description                           |
|-----------------|---------------|---------------------------------------|
| `code`          | `int`         | Error code (MOBIUS_ERROR_*)           |
| `message`       | `const char*` | Human-readable error message          |
| `suggestion`    | `const char*` | Optional fix suggestion (may be NULL) |
| `line`          | `int`         | Source line number                    |
| `column`        | `int`         | Source column number                  |
| `function_name` | `const char*` | Enclosing function (may be NULL)      |

All pointers in `MobiusError` are valid **only for the duration of the
callback**. Copy them if you need to store them.

### Clearing Errors

After handling an error, clear the state so subsequent execution can proceed:

```c
mobius_clear_error(state);
```

### Errors from Native Functions

From inside a native function, report errors with `mobius_error()`:

```c
int my_func(MobiusState* state, int arg_count) {
    if (arg_count < 1)
        return mobius_error(state, "my_func requires at least 1 argument");
    // ...
}
```

`mobius_error()` invokes the error handler and returns a negative value. The
native function should return that value immediately.

---

## Loading Plugins

Plugins are shared libraries (`.so` on Linux, `.dll` on Windows) that export
additional functions. See the [Plugin Guide](plugin_guide.md) for how to
author them.

### Adding Plugin Directories

Plugin directories are per-state. Call `mobius_add_plugin_directory()` **after**
creating the state:

```c
MobiusState* state = mobius_new_state(NULL);
mobius_init_stdlib(state);

mobius_add_plugin_directory(state, "./plugins");
mobius_add_plugin_directory(state, "/usr/lib/mobius/modules");
```

The interpreter scans these directories for `.so`/`.dll` files that export
`mobius_plugin_info()`. Each `MobiusState` maintains its own list of plugin
directories, so multiple interpreter instances can have independent plugin
search paths.

### Querying Loaded Modules

```c
size_t count = mobius_get_module_count(state);
printf("Loaded %zu modules\n", count);
mobius_print_modules(state);    // prints to stdout
```

### Using Plugins from Scripts

Once a plugin directory is configured and the module is present, scripts
import it by name:

```mobius
import "math"
print(math.sin(3.14))
```

---

## Multiple Interpreter Instances

Each `MobiusState` is fully independent. You can run multiple interpreters
concurrently, each with its own globals, stack, and configuration:

```c
MobiusState* state_a = mobius_new_state(NULL);
MobiusState* state_b = mobius_new_state(NULL);

mobius_init_stdlib(state_a);
mobius_init_stdlib(state_b);

// Each state has its own globals
mobius_stack_pushInt64(state_a, 100);
mobius_stack_setGlobal(state_a, "value");

mobius_stack_pushInt64(state_b, 200);
mobius_stack_setGlobal(state_b, "value");

mobius_exec_string(state_a, "print(value)");    // prints 100
mobius_exec_string(state_b, "print(value)");    // prints 200

mobius_free_state(state_a);
mobius_free_state(state_b);
```

---

## Concurrency and Fibers

Mobius scripts can use `spawn`, `await`, `yield`, and `shared` to run fibers concurrently within a single `MobiusState`. Each state owns its own fiber pool and worker threads, configured via `MobiusConfig`.

### Threading Model

- Each `MobiusState` maintains a pool of worker threads (controlled by `max_worker_threads`).
- When a script executes `spawn`, a fiber is acquired from the pool and scheduled on a worker.
- The calling thread always participates as a worker for its own state.
- All reference counting is atomic, and shared containers use reader-writer locks.

### Configuration

Tune concurrency by adjusting `MobiusConfig` before creating a state:

```c
MobiusConfig config = mobius_default_config();
config.max_worker_threads = 8;
config.initial_fiber_pool_size = 32;
config.max_fiber_pool_size = 2048;
config.fiber_stack_size = 256 * 1024;  // 256KB per fiber

MobiusState* state = mobius_new_state(&config);
```

Configuration is immutable after `mobius_new_state()` returns.

### Metrics

Use the metrics API to observe runtime behavior:

```c
MobiusMetrics metrics = mobius_get_metrics(state);
printf("Fibers spawned: %lu\n", metrics.fibers_spawned);
printf("Jobs dispatched: %lu\n", metrics.jobs_dispatched);

mobius_reset_metrics(state);  // reset counters
```

### Thread Safety

- Multiple `MobiusState` instances can run in parallel with no shared mutable state.
- Within a single state, the fiber scheduler handles concurrency automatically.
- Native functions registered with the C API may be called from any worker thread — ensure they are thread-safe if they access shared C/C++ state.

---

## Type Metatables

Type metatables allow you to attach methods to all values of a given type.
When a script uses the `:` method-call syntax on a value, the VM looks up
the method name in the type's metatable. This is how the built-in array,
table, and channel methods work.

The standard library automatically registers type metatables for
`MOBIUS_VAL_ARRAY`, `MOBIUS_VAL_TABLE`, and `MOBIUS_VAL_CHANNEL` when you
call `mobius_init_stdlib()`. You can add your own methods to these
metatables or create metatables for other types (e.g., `MOBIUS_VAL_STRING`,
`MOBIUS_VAL_USERDATA`).

### Reading a Type Metatable

```c
// Push the current type metatable for arrays onto the stack
mobius_push_type_metatable(state, MOBIUS_VAL_ARRAY);

if (!mobius_stack_isNil(state, -1)) {
    // Inspect or modify the table using the normal table API
    mobius_stack_getTableField(state, -1, "push");
    // ... inspect the method ...
    mobius_stack_pop(state, 1);
}
mobius_stack_pop(state, 1);
```

If no metatable has been set for the type, `mobius_push_type_metatable`
pushes `nil`.

### Setting a Type Metatable

```c
// Create a new table to serve as the string type metatable
mobius_stack_pushNewTable(state, 4);

// Add a native method
mobius_register_function(state, "__temp_upper", native_string_upper);
mobius_stack_getGlobal(state, "__temp_upper");
mobius_stack_setTableField(state, -2, "upper");
mobius_remove_global(state, "__temp_upper");

// Pop the table and install it as the metatable for strings
mobius_set_type_metatable(state, MOBIUS_VAL_STRING);
```

After this, scripts can call:

```mobius
var s = "hello"
print(s:upper())    // "HELLO"
```

### Extending an Existing Type Metatable

To add methods to a type that already has a metatable (e.g., adding a
custom method to arrays):

```c
// Push the existing array metatable
mobius_push_type_metatable(state, MOBIUS_VAL_ARRAY);

// Add a new method to it
mobius_register_function(state, "__temp_sum", native_array_sum);
mobius_stack_getGlobal(state, "__temp_sum");
mobius_stack_setTableField(state, -2, "sum");
mobius_remove_global(state, "__temp_sum");

// Pop and re-install (or simply pop — changes are in-place since
// the table is a reference type)
mobius_stack_pop(state, 1);
```

### Writing Method-Style Native Functions

When a method is called via `:`, the receiver object (`self`) is passed as
the first argument in the native call stack. A typical method implementation
reads `self` first, then the user-provided arguments:

```c
int native_array_sum(MobiusState* state, int arg_count) {
    // self is at position 1 on the native stack (first argument)
    // For arrays, use the C API to iterate
    if (!mobius_stack_isArray(state, 1))
        return mobius_error(state, "sum() expects an array receiver");

    size_t len = mobius_stack_getArrayLength(state, 1);
    double total = 0;
    for (size_t i = 0; i < len; i++) {
        mobius_stack_getArrayElement(state, 1, i);
        total += mobius_stack_asFloat64(state, -1);
        mobius_stack_pop(state, 1);
    }

    mobius_stack_pushFloat64(state, total);
    return 1;
}
```

### API Functions

| Function                                           | Description                                   |
|----------------------------------------------------|-----------------------------------------------|
| `mobius_push_type_metatable(state, type)`           | Push the metatable for a value type (or nil)  |
| `mobius_set_type_metatable(state, type)`            | Pop top value and set as type's metatable     |

---

## API Reference Summary

### Lifecycle

| Function                   | Description                                      |
|----------------------------|--------------------------------------------------|
| `mobius_default_config()`  | Return a `MobiusConfig` with sensible defaults   |
| `mobius_new_state(config)` | Create a new interpreter (NULL for defaults)     |
| `mobius_init_stdlib(state)`| Load the standard library                        |
| `mobius_free_state(state)` | Destroy the interpreter and free all resources   |

### Execution

| Function                            | Description                    |
|-------------------------------------|--------------------------------|
| `mobius_exec_string(state, code)`   | Execute a string of Mobius code|
| `mobius_exec_file(state, filename)` | Execute a `.mob` file          |
| `mobius_start_repl(state)`          | Start an interactive REPL      |

### Error Handling

| Function                                         | Description                      |
|--------------------------------------------------|----------------------------------|
| `mobius_set_error_handler(state, handler, udata)` | Install a custom error handler  |
| `mobius_clear_error(state)`                      | Clear the last error             |
| `mobius_error(state, message)`                   | Report an error (from native fn) |

### Stack — Push

| Function                                      | Pushes           |
|-----------------------------------------------|------------------|
| `mobius_stack_pushNil(state)`                  | nil              |
| `mobius_stack_pushBool(state, val)`            | boolean          |
| `mobius_stack_pushInt64(state, val)` / `pushUInt64` | integer (preferred) |
| `mobius_stack_pushInt8` … `pushInt32`, `pushUInt8` … | backward-compat (widen to int64) |
| `mobius_stack_pushFloat64(state, val)` | float (preferred) |
| `mobius_stack_pushFloat32(state, val)` | backward-compat |
| `mobius_stack_pushString(state, str)`          | string           |
| `mobius_stack_pushNewTable(state, cap)`        | empty table      |
| `mobius_stack_pushNewArray(state, cap)`        | empty array      |
| `mobius_stack_pushUserdata(state, ptr, dtor, type, size)` | userdata |

### Stack — Read

| Function                                        | Returns          |
|-------------------------------------------------|------------------|
| `mobius_stack_asFloat64(state, idx)` (and other `as` variants) | auto-converting getter |
| `mobius_stack_getFloat64(state, idx)` (and other `get` variants) | strict getter |
| `mobius_stack_getUserdata(state, idx, &type_name)` | void pointer  |

### Stack — Inspect & Manipulate

| Function                              | Description                   |
|---------------------------------------|-------------------------------|
| `mobius_stack_size(state)`             | Number of values on the stack |
| `mobius_stack_type(state, idx)`        | Type enum of value at index   |
| `mobius_stack_is*(state, idx)`         | Type predicate (isNumber, etc)|
| `mobius_stack_pop(state, count)`       | Pop count values              |
| `mobius_stack_copy(state, idx)`        | Duplicate value at index      |

### Globals

| Function                                | Description                          |
|-----------------------------------------|--------------------------------------|
| `mobius_stack_getGlobal(state, name)`   | Push a global's value onto the stack |
| `mobius_stack_setGlobal(state, name)`   | Pop top value into a global          |

### Tables & Arrays (on stack)

| Function                                                | Description                      |
|---------------------------------------------------------|----------------------------------|
| `mobius_stack_setTableField(state, tbl_idx, key)`       | Pop top → table[key]             |
| `mobius_stack_getTableField(state, tbl_idx, key)`       | Push table[key]                  |
| `mobius_stack_setArrayElement(state, arr_idx, elem_idx)` | Pop top → array[elem_idx]       |
| `mobius_stack_getArrayElement(state, arr_idx, elem_idx)` | Push array[elem_idx]            |
| `mobius_stack_getArrayLength(state, arr_idx)`           | Return array length              |

### Native Functions

| Function                                            | Description                       |
|-----------------------------------------------------|-----------------------------------|
| `mobius_register_function(state, name, func)`       | Register a C function as a global |

### Type Metatables

| Function                                            | Description                                  |
|-----------------------------------------------------|----------------------------------------------|
| `mobius_push_type_metatable(state, type)`            | Push a type's metatable onto the stack (or nil) |
| `mobius_set_type_metatable(state, type)`             | Pop top and set as the metatable for a type  |

### Plugins

| Function                                       | Description                              |
|------------------------------------------------|------------------------------------------|
| `mobius_add_plugin_directory(state, path)`      | Add a directory to the plugin search path|
| `mobius_clear_plugin_directories(state)`        | Clear all plugin search paths            |
| `mobius_get_module_count(state)`                | Number of loaded modules                 |
| `mobius_print_modules(state)`                   | Print module summary to stdout           |

### Metrics

| Function                                | Description                              |
|-----------------------------------------|------------------------------------------|
| `mobius_get_metrics(state)`            | Return a `MobiusMetrics` snapshot        |
| `mobius_reset_metrics(state)`          | Reset all metric counters to zero        |
