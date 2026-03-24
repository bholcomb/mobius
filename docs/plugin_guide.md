# Writing Plugins for Mobius

Plugins are shared libraries (`.so` on Linux, `.dll` on Windows) that extend
Mobius with additional native functions. This guide walks through the plugin
API, the structure of a plugin, and how to build and load one.

The Mobius language only exposes **`int64`**, **`uint64`**, and **`float64`** as
numeric type annotations; integers are stored as 64-bit values in the VM. The C
stack API still includes **`pushInt8`**, **`pushInt32`**, **`asInt32`**, and
other narrow-width helpers for backward compatibility: they widen when pushing
and narrow when reading at the C boundary, while the runtime keeps integers as
**`int64`**. Prefer **`mobius_stack_pushInt64`** / **`mobius_stack_pushUInt64`**
and **`mobius_stack_asInt64`** / **`mobius_stack_asUInt64`** in new code.

**Header you need:**

- `<mobius/mobius_plugin.h>` — plugin structs, stack API, export macros

---

## Table of Contents

1. [How Plugins Work](#how-plugins-work)
2. [Plugin Structure](#plugin-structure)
3. [Writing Plugin Functions](#writing-plugin-functions)
4. [Metadata and Registration](#metadata-and-registration)
5. [Lifecycle Hooks](#lifecycle-hooks)
6. [Building a Plugin](#building-a-plugin)
7. [Loading and Using Plugins](#loading-and-using-plugins)
8. [Complete Example: Text Processing Plugin](#complete-example-text-processing-plugin)
9. [Best Practices](#best-practices)
10. [API Quick Reference](#api-quick-reference)

---

## How Plugins Work

1. The host application calls `mobius_add_plugin_directory()` with one or
   more directories before creating the interpreter state.
2. The interpreter scans those directories for shared libraries that export
   a `mobius_plugin_info()` function.
3. When a script runs `import "my_plugin"`, the interpreter loads the
   corresponding `.so`/`.dll`, calls `mobius_plugin_info()`, reads the
   metadata and function table, and registers every function under a
   namespace matching the plugin name.
4. Scripts call plugin functions using qualified names:
   `my_plugin.some_function(args)`.
5. On interpreter shutdown, each plugin's optional `cleanup_plugin()` hook
   is called before the library is unloaded.

---

## Plugin Structure

Every plugin must:

1. Include `<mobius/mobius_plugin.h>`.
2. Define one or more native C functions.
3. Fill out a `MobiusPlugin` struct with metadata and a function table.
4. Export a `mobius_plugin_info()` function that returns a pointer to that
   struct.

Here is the minimal skeleton:

```c
#include <mobius/mobius_plugin.h>

// --- Native functions ---

int my_hello(MobiusState* state, int arg_count) {
    mobius_stack_pushString(state, "hello from plugin!");
    return 1;
}

// --- Function table ---

static MobiusPluginFunction functions[] = {
    {"hello", my_hello, 0, "Returns a greeting"},
};

// --- Plugin descriptor ---

static MobiusPlugin plugin = {
    .metadata = {
        .name        = "my_plugin",
        .version     = "1.0.0",
        .description = "A minimal example plugin",
        .author      = "Your Name",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license     = "MIT",
    },
    .functions      = functions,
    .function_count = sizeof(functions) / sizeof(functions[0]),
    .init_plugin    = NULL,
    .cleanup_plugin = NULL,
};

// --- Entry point ---

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &plugin;
}
```

> **Note:** If writing in C (not C++), omit the `extern "C"` wrapper — it is
> only needed when compiling as C++ to prevent name mangling.

---

## Writing Plugin Functions

### Signature

```c
int my_function(MobiusState* state, int arg_count);
```

- `state` — the interpreter instance.
- `arg_count` — number of arguments the script passed.
- Return value: the number of values pushed onto the stack as return values
  (>= 0), **or** the result of `mobius_error()` on failure (always negative).

### Reading Arguments

Arguments are on the stack. The last argument is at index `-1`, second-to-last
at `-2`, and so on.

```c
int my_add(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "add() expects 2 arguments");

    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2))
        return mobius_error(state, "add() expects numeric arguments");

    double b = mobius_stack_asFloat64(state, -1);
    double a = mobius_stack_asFloat64(state, -2);

    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, a + b);
    return 1;
}
```

### Argument Validation Pattern

Most plugin functions follow this pattern:

```c
int my_func(MobiusState* state, int arg_count) {
    // 1. Check argument count
    if (arg_count != EXPECTED_COUNT)
        return mobius_error(state, "my_func() expects N arguments");

    // 2. Check argument types
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "my_func() expects a string argument");

    // 3. Read arguments
    const char* str = mobius_stack_asString(state, -1);

    // 4. Pop consumed arguments
    mobius_stack_pop(state, arg_count);

    // 5. Compute result and push it
    mobius_stack_pushString(state, result);

    // 6. Return number of values pushed
    return 1;
}
```

### Returning Multiple Values

Push multiple values and return the count:

```c
int get_position(MobiusState* state, int arg_count) {
    mobius_stack_pushFloat64(state, 10.5);   // x
    mobius_stack_pushFloat64(state, 20.3);   // y
    return 2;
}
```

### Returning Nothing

```c
int log_message(MobiusState* state, int arg_count) {
    const char* msg = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    printf("[LOG] %s\n", msg);
    return 0;    // no return value
}
```

### Stack Type Checkers

| Function                          | Checks for            |
|-----------------------------------|-----------------------|
| `mobius_stack_isNumber(state, i)`  | Any numeric type      |
| `mobius_stack_isInteger(state, i)` | Any integer width     |
| `mobius_stack_isFloat(state, i)`   | Floating-point values |
| `mobius_stack_isString(state, i)`  | String                |
| `mobius_stack_isBool(state, i)`    | Boolean               |
| `mobius_stack_isNil(state, i)`     | Nil                   |
| `mobius_stack_isTable(state, i)`   | Table                 |
| `mobius_stack_isArray(state, i)`   | Array                 |
| `mobius_stack_isFunction(state, i)`| Function              |
| `mobius_stack_isUserdata(state, i)`| Userdata              |

### Stack Value Getters

**Permissive** (`as` prefix) — auto-convert where possible:

```c
int64_t     mobius_stack_asInt64(state, idx);
double      mobius_stack_asFloat64(state, idx);
const char* mobius_stack_asString(state, idx);
bool        mobius_stack_asBool(state, idx);
```

**Strict** (`get` prefix) — respect strict_types pragma:

```c
int64_t     mobius_stack_getInt64(state, idx);
const char* mobius_stack_getString(state, idx);
```

Primary numeric getters/setters: **`Int64`**, **`UInt64`**, **`Float64`**. The
same headers also declare **`Int8`** … **`Int32`**, **`UInt8`** … **`UInt32`**,
and **`Float32`** for compatibility; those convert at the boundary while
integer values remain **`int64`** in the interpreter.

---

## Metadata and Registration

### MobiusPluginMetadata

```c
typedef struct {
    const char* name;         // Plugin name (used as import namespace)
    const char* version;      // Semantic version string
    const char* description;  // Short description
    const char* author;       // Author name
    size_t      api_version;  // Must be MOBIUS_PLUGIN_API_VERSION
    const char* license;      // License (may be NULL)
} MobiusPluginMetadata;
```

The `name` field is critical — it determines the namespace scripts use to call
your functions. If `name` is `"math"`, scripts call `math.sin()`.

### MobiusPluginFunction

```c
typedef struct {
    const char*     name;         // Function name
    MobiusCFunction function;     // Function pointer
    size_t          arg_count;    // Expected argument count (SIZE_MAX = variadic)
    const char*     description;  // Help text (may be NULL)
} MobiusPluginFunction;
```

Set `arg_count` to `SIZE_MAX` for variadic functions that accept any number of
arguments.

### MobiusPlugin

```c
typedef struct {
    MobiusPluginMetadata  metadata;
    MobiusPluginFunction* functions;
    size_t                function_count;
    int  (*init_plugin)(void);       // Optional (may be NULL)
    void (*cleanup_plugin)(void);    // Optional (may be NULL)
} MobiusPlugin;
```

---

## Lifecycle Hooks

### init_plugin

Called once when the plugin is first loaded. Return 0 for success, non-zero
for failure (which prevents the plugin from loading).

```c
int my_init(void) {
    // Allocate resources, open connections, seed RNG, etc.
    srand(time(NULL));
    return 0;
}
```

### cleanup_plugin

Called when the interpreter shuts down. Release any resources acquired during
init.

```c
void my_cleanup(void) {
    // Close connections, free buffers, etc.
}
```

---

## Building a Plugin

Plugins must be compiled as shared libraries with position-independent code and
the entry point symbol visible.

### GCC / Clang (Linux / macOS)

```bash
g++ -shared -fPIC -o my_plugin.so my_plugin.cpp \
    -I/path/to/mobius/include
```

### MSVC (Windows)

```bash
cl /LD /I C:\path\to\mobius\include my_plugin.cpp /Fe:my_plugin.dll
```

### With Buildy (Mobius build system)

Add a module in your `buildy.yaml`:

```yaml
modules:
  - name: my_plugin
    type: shared_library
    sources:
      - my_plugin.cpp
    include_dirs:
      - ../../include
```

### Output Naming

The shared library filename (minus extension) should match the plugin's
`metadata.name`. If your plugin is named `"text_processing"`, the file should
be `text_processing.so` (or `text_processing.dll`).

---

## Loading and Using Plugins

### From the Embedding Application

```c
// Register plugin directories before creating the state
mobius_add_plugin_directory("./plugins");
mobius_add_plugin_directory("./bin/modules");

MobiusState* state = mobius_new_state(NULL);
mobius_init_stdlib(state);

// Plugins are now discoverable
mobius_exec_string(state, "import \"text_processing\"\n"
                          "print(text_processing.word_count(\"hello world\"))");
```

### From Scripts

```mobius
import "text_processing"

var count = text_processing.word_count("The quick brown fox")
print(count)    // 4

var reversed = text_processing.reverse("hello")
print(reversed)    // "olleh"
```

With an alias:

```mobius
import "text_processing" as tp

print(tp.trim("  hello  "))    // "hello"
```

---

## Complete Example: Text Processing Plugin

This example demonstrates a full plugin with multiple functions, lifecycle
hooks, and help text.

```cpp
#include <mobius/mobius_plugin.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// --- Functions ---

int text_word_count(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "word_count() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "word_count() expects a string");

    const char* text = mobius_stack_asString(state, -1);
    int words = 0, in_word = 0;
    while (*text) {
        if (isspace(*text)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
        text++;
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, words);
    return 1;
}

int text_upper(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "to_upper() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "to_upper() expects a string");

    const char* input = mobius_stack_asString(state, -1);
    size_t len = strlen(input);
    char* result = (char*)malloc(len + 1);
    for (size_t i = 0; i <= len; i++)
        result[i] = toupper((unsigned char)input[i]);

    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, result);
    free(result);
    return 1;
}

// --- Registration ---

static MobiusPluginFunction functions[] = {
    {"word_count", text_word_count, 1, "Count words in a string"},
    {"to_upper",   text_upper,     1, "Convert string to uppercase"},
};

static MobiusPlugin plugin = {
    .metadata = {
        .name        = "text_tools",
        .version     = "1.0.0",
        .description = "Text processing utilities",
        .author      = "Example Author",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license     = "MIT",
    },
    .functions      = functions,
    .function_count = sizeof(functions) / sizeof(functions[0]),
    .init_plugin    = NULL,
    .cleanup_plugin = NULL,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &plugin;
}
```

Build it:

```bash
g++ -shared -fPIC -o text_tools.so text_tools.cpp -I/path/to/mobius/include
```

Use it in a script:

```mobius
import "text_tools"

print(text_tools.word_count("Mobius is great"))   // 3
print(text_tools.to_upper("hello"))               // "HELLO"
```

---

## Best Practices

1. **Always validate arguments.** Check `arg_count` and types before reading
   values. Return clear error messages.

2. **Pop what you consume.** After reading arguments from the stack, pop them
   before pushing return values. This keeps the stack clean.

3. **Use `SIZE_MAX` for variadic functions.** If your function accepts a
   variable number of arguments, set `arg_count` to `SIZE_MAX` in the
   function table and handle the count dynamically in the implementation.

4. **Keep plugin names unique.** The plugin name is used as the script
   namespace. Collisions with other plugins or built-in modules will cause
   errors.

5. **Match the API version.** Always set `api_version` to
   `MOBIUS_PLUGIN_API_VERSION`. This allows the interpreter to detect
   incompatible plugins.

6. **Free temporary allocations.** If you allocate memory for a result string,
   push it onto the stack (which copies it), then free your buffer.

7. **Use lifecycle hooks for resources.** Open files, connections, or
   allocations in `init_plugin()` and release them in `cleanup_plugin()`.

8. **Test with both backends.** Mobius has a bytecode VM (default) and a
   tree-walk interpreter (`--tree-walk`). Test your plugin with both.

---

## API Quick Reference

### Entry Point

```c
MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void);
```

### Error Reporting

```c
int mobius_error(MobiusState* state, const char* message);
```

### Stack Push

```c
void mobius_stack_pushNil(state);
void mobius_stack_pushBool(state, value);
void mobius_stack_pushInt64(state, value);    // primary; Int8…Int32, UInt* also exist
void mobius_stack_pushUInt64(state, value);
void mobius_stack_pushFloat64(state, value);  // Float32 also exists
void mobius_stack_pushString(state, str);
void mobius_stack_pushNewTable(state, capacity);
void mobius_stack_pushNewArray(state, capacity);
void mobius_stack_pushUserdata(state, ptr, destructor, type_name, size);
```

### Stack Read

```c
double      mobius_stack_asFloat64(state, idx);   // permissive
int64_t     mobius_stack_asInt64(state, idx);      // permissive
const char* mobius_stack_asString(state, idx);     // permissive
bool        mobius_stack_asBool(state, idx);       // permissive

int64_t     mobius_stack_getInt64(state, idx);     // strict (narrow get* still declared)
const char* mobius_stack_getString(state, idx);    // strict
```

### Stack Manipulation

```c
int             mobius_stack_size(state);
MobiusValueType mobius_stack_type(state, idx);
void            mobius_stack_pop(state, count);
void            mobius_stack_copy(state, idx);
```

### Type Predicates

```c
bool mobius_stack_isNumber(state, idx);
bool mobius_stack_isInteger(state, idx);
bool mobius_stack_isFloat(state, idx);
bool mobius_stack_isString(state, idx);
bool mobius_stack_isBool(state, idx);
bool mobius_stack_isNil(state, idx);
bool mobius_stack_isTable(state, idx);
bool mobius_stack_isArray(state, idx);
bool mobius_stack_isFunction(state, idx);
bool mobius_stack_isUserdata(state, idx);
```

### Value Types Enum

The `MobiusValueType` enum includes tags for multiple numeric widths so the C
API can distinguish how a value was pushed; integer **payloads** in the VM are
still wide **`int64`** storage.

```c
typedef enum {
    MOBIUS_VAL_NIL,
    MOBIUS_VAL_BOOL,
    MOBIUS_VAL_INT8, MOBIUS_VAL_INT16, MOBIUS_VAL_INT32, MOBIUS_VAL_INT64,
    MOBIUS_VAL_UINT8, MOBIUS_VAL_UINT16, MOBIUS_VAL_UINT32, MOBIUS_VAL_UINT64,
    MOBIUS_VAL_FLOAT32, MOBIUS_VAL_FLOAT64,
    MOBIUS_VAL_STRING, MOBIUS_VAL_CHAR,
    MOBIUS_VAL_ARRAY, MOBIUS_VAL_FUNCTION, MOBIUS_VAL_NATIVE_FUNCTION,
    MOBIUS_VAL_TABLE, MOBIUS_VAL_USERDATA, MOBIUS_VAL_ENUM,
} MobiusValueType;
```
