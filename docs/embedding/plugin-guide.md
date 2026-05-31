# Plugin Guide

Plugins are shared libraries (`.so` on Linux, `.dll` on Windows) that extend
Mobius with native functions. This guide covers the plugin API, structure,
building, and the optional `.mob` companion-script pattern used by the bundled
modules.

**Header:** `<mobius/mobius_plugin.h>` — plugin structs, the stack API, and the
export macro. It includes `<mobius/mobius.h>` for you.

The stack API and value-handling rules are identical to those in the
[Embedding Guide](embedding-guide.md); this page focuses on packaging native
code as an importable module.

[← Documentation home](../index.md)

---

## How plugins work

1. The host adds one or more search directories with
   `mobius_add_plugin_directory(state, path)`.
2. When a script runs `import "my_plugin"`, the interpreter looks for a shared
   library exporting `mobius_plugin_info()`, loads it, reads its metadata and
   function table, and registers every function under a namespace matching the
   plugin name.
3. Scripts call functions with qualified names: `my_plugin.some_function(args)`.
4. On shutdown, each plugin's optional `cleanup_plugin()` hook runs before the
   library is unloaded.

---

## Plugin structure

A plugin must:

1. Include `<mobius/mobius_plugin.h>`.
2. Define one or more native functions.
3. Fill in a `MobiusPlugin` struct with metadata and a function table.
4. Export `mobius_plugin_info()` returning a pointer to that struct.

```c
#include <mobius/mobius_plugin.h>

int my_hello(MobiusState* state, int arg_count) {
    mobius_stack_pushString(state, "hello from plugin!");
    return 1;
}

static MobiusPluginFunction functions[] = {
    {"hello", my_hello, 0, MOBIUS_VAL_STRING, "Returns a greeting"},
};

static MobiusPlugin plugin = {
    .metadata = {
        .name        = "my_plugin",
        .version     = "1.0.0",
        .description = "A minimal example plugin",
        .author      = "Your Name",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license     = "MIT",
        .depends_on       = NULL,   // optional array of required module names
        .depends_on_count = 0,
    },
    .functions      = functions,
    .function_count = sizeof(functions) / sizeof(functions[0]),
    .init_plugin    = NULL,
    .cleanup_plugin = NULL,
    .post_init      = NULL,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &plugin;
}
```

> If you compile as C (not C++), drop the `extern "C"` wrapper.

---

## Writing plugin functions

Same signature and rules as native functions in the
[Embedding Guide](embedding-guide.md#registering-native-functions): read
arguments from the stack (last at `-1`), pop what you consume, push results, and
return the count pushed — or return `mobius_error()` on failure.

```c
int my_add(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "add() expects 2 arguments");
    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2))
        return mobius_error(state, "add() expects numbers");

    double b = mobius_stack_asFloat64(state, -1);
    double a = mobius_stack_asFloat64(state, -2);
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, a + b);
    return 1;
}
```

---

## The plugin structs

### MobiusPluginFunction

```c
typedef struct {
    const char*     name;         // function name within the module namespace
    MobiusCFunction function;     // function pointer
    size_t          arg_count;    // expected argument count (SIZE_MAX = variadic)
    MobiusValueType return_type;  // MOBIUS_VAL_UNKNOWN if it depends on inputs
    const char*     description;  // help text (may be NULL)
} MobiusPluginFunction;
```

`return_type` lets the compiler emit type-specialized opcodes at call sites; use
`MOBIUS_VAL_UNKNOWN` when the result type depends on the arguments.

### MobiusPluginMetadata

```c
typedef struct {
    const char* name;             // import namespace — scripts call name.func()
    const char* version;
    const char* description;
    const char* author;
    size_t      api_version;      // must be MOBIUS_PLUGIN_API_VERSION
    const char* license;          // may be NULL
    const char** depends_on;      // optional array of required module names
    size_t       depends_on_count;
} MobiusPluginMetadata;
```

### MobiusPlugin

```c
typedef struct {
    MobiusPluginMetadata  metadata;
    MobiusPluginFunction* functions;
    size_t                function_count;

    int  (*init_plugin)(MobiusState* state);   // optional, may be NULL
    void (*cleanup_plugin)(void);              // optional, may be NULL
    int  (*post_init)(MobiusState* state);     // optional, may be NULL
} MobiusPlugin;
```

---

## Lifecycle hooks

### init_plugin

Called once when the plugin first loads, before functions are registered.
Receives the state so it can do setup. Return `0` for success; non-zero aborts
loading.

```c
int my_init(MobiusState* state) {
    (void)state;
    srand((unsigned)time(NULL));
    return 0;
}
```

### post_init

Called **after** the functions are registered into the module table. The module
table is on the stack at **index 0**, so this is where you add constants, enums,
or type metatables to the module:

```c
int my_post_init(MobiusState* state) {
    mobius_stack_pushInt64(state, 42);
    mobius_stack_setTableField(state, 0, "ANSWER");   // my_plugin.ANSWER == 42
    return 0;
}
```

### cleanup_plugin

Called at interpreter shutdown — release anything acquired in `init_plugin`.

```c
void my_cleanup(void) { /* close connections, free buffers, ... */ }
```

---

## The `.mob` companion pattern

Several bundled modules pair their native library with a `.mob` script that
wraps low-level native functions in a friendlier API. The native functions are
named with a leading `__` (treated as internal) and registered as **userdata
type metatable** methods or hidden helpers in `post_init`; the companion script
exposes clean names.

This is how the `compression`, `http`, `socket`, `websocket`, `web`, and
`sqlite` modules present object-style APIs (e.g. `db:query(...)`,
`sock:recv(...)`) over plain C functions. The packaging layout that ties a
native library to its `.mob` entry script is described in
[Packaging](packaging.md).

If your module only needs plain functions, you don't need a companion script —
the function table alone is enough.

---

## Building a plugin

Compile as a shared library with position-independent code and the entry symbol
visible. The output filename (without extension) should match the plugin's
`metadata.name`.

```bash
# GCC / Clang
g++ -shared -fPIC -o my_plugin.so my_plugin.cpp -I/path/to/mobius/include

# MSVC
cl /LD /I C:\path\to\mobius\include my_plugin.cpp /Fe:my_plugin.dll
```

With Buildy, add a `shared_library` module to your `buildy.yaml` that includes
`../../include` (see the bundled modules under `src/modules/` for examples).

---

## Loading and using

```c
mobius_add_plugin_directory(state, "./modules");
mobius_exec_string(state,
    "import \"text_tools\"\n"
    "print(text_tools.word_count(\"hello world\"))");
```

```mobius
import "text_tools"
print(text_tools.word_count("The quick brown fox"))   // 4

import "text_tools" as tt
print(tt.to_upper("hello"))                            // "HELLO"
```

---

## Value types

`MobiusValueType` (from `mobius_plugin.h`) classifies stack values:

```c
typedef enum {
    MOBIUS_VAL_UNKNOWN = -1,      // return type depends on inputs / undetermined
    /* Non-refcounted (inline) types */
    MOBIUS_VAL_NIL,
    MOBIUS_VAL_BOOL,
    MOBIUS_VAL_INT64,
    MOBIUS_VAL_UINT64,
    MOBIUS_VAL_FLOAT64,
    MOBIUS_VAL_CHAR,
    MOBIUS_VAL_NATIVE_FUNCTION,
    MOBIUS_VAL_STRING,
    /* Refcounted (heap-allocated) types */
    MOBIUS_VAL_ARRAY,
    MOBIUS_VAL_FUNCTION,
    MOBIUS_VAL_TABLE,
    MOBIUS_VAL_USERDATA,
    MOBIUS_VAL_ENUM,
    MOBIUS_VAL_FUTURE,
    MOBIUS_VAL_ARRAY_SLICE,
    MOBIUS_VAL_CHANNEL,
    MOBIUS_VAL_SHARED_CELL,
    MOBIUS_VAL_BUFFER
} MobiusValueType;
```

These values match the interpreter's internal enum numerically.

---

## Best practices

1. **Validate arguments** — check `arg_count` and types, with clear messages.
2. **Pop what you consume** before pushing results, to keep the stack clean.
3. **Use `SIZE_MAX`** as `arg_count` for variadic functions.
4. **Keep names unique** — the plugin name is the script namespace.
5. **Match `api_version`** to `MOBIUS_PLUGIN_API_VERSION`.
6. **Free temporary allocations** after pushing a result (the push copies it).
7. **Use lifecycle hooks** for resource setup/teardown and for adding constants
   in `post_init`.
8. **Be thread-safe** — plugin functions may run on any worker thread.

---

Next: [Packaging](packaging.md).
