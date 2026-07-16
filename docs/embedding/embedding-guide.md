# Embedding Guide

This guide explains how to embed the Mobius interpreter in a C or C++
application: managing interpreter state, executing scripts, exchanging values
through the stack, registering native functions, and handling errors.

**Headers:**

- `<mobius/mobius.h>` — core embedding API (lifecycle, config, execution, errors, metrics)
- `<mobius/mobius_plugin.h>` — the stack API, native functions, value types, refs

Link against `libmobius-core`:

```bash
g++ -o my_app my_app.cpp -lmobius-core -ldl
```

> Integers live as `int64` in the VM. The stack API offers narrow helpers
> (`pushInt8`, `asInt32`, `pushFloat32`, …) that convert at the C boundary for
> convenience, but prefer `pushInt64`/`pushUInt64`/`pushFloat64` and
> `asInt64`/`asUInt64`/`asFloat64` in new code.

[← Documentation home](../index.md)

---

## Contents

1. [Minimal example](#minimal-example)
2. [State lifecycle](#state-lifecycle)
3. [Configuration](#configuration)
4. [Footprint](#footprint)
5. [Executing scripts](#executing-scripts)
6. [The stack](#the-stack)
7. [Exchanging values](#exchanging-values)
8. [Registering native functions](#registering-native-functions)
9. [Calling Mobius functions from C](#calling-mobius-functions-from-c)
10. [Rooted value handles](#rooted-value-handles)
11. [Error handling](#error-handling)
12. [Loading plugins](#loading-plugins)
13. [Type metatables](#type-metatables)
14. [Multiple interpreters](#multiple-interpreters)
15. [Concurrency and fibers](#concurrency-and-fibers)
16. [Metrics](#metrics)

---

## Minimal example

```c
#include <mobius/mobius.h>

int main(void) {
    MobiusState* state = mobius_new_state(NULL);   // NULL = default config
    mobius_init_stdlib(state);
    mobius_exec_string(state, "print(\"Hello from Mobius!\")");
    mobius_free_state(state);
    return 0;
}
```

Always call `mobius_init_stdlib()` after `mobius_new_state()` — without it,
scripts have no `print`, `typeof`, or any built-in functions.

---

## State lifecycle

Every program runs inside a `MobiusState`, an opaque handle that owns the
interpreter, its globals, its stack, and all allocated objects.

```c
MobiusState* state = mobius_new_state(NULL);

MobiusConfig config = mobius_default_config();
config.debug_mode = true;
MobiusState* state = mobius_new_state(&config);    // or with config

mobius_init_stdlib(state);     // returns MOBIUS_OK on success
// ... use the interpreter ...
mobius_free_state(state);       // destroy and free everything
```

| Function                    | Description                                   |
|-----------------------------|-----------------------------------------------|
| `mobius_default_config()`   | A `MobiusConfig` filled with sensible defaults |
| `mobius_new_state(config)`  | Create an interpreter (`NULL` for defaults)   |
| `mobius_init_stdlib(state)` | Load the standard library                     |
| `mobius_free_state(state)`  | Destroy the interpreter                       |

---

## Configuration

Get the defaults, change what you need, and pass a pointer to
`mobius_new_state()`. Configuration is **immutable** after the state is created.

```c
MobiusConfig config = mobius_default_config();
config.max_worker_threads = 8;
MobiusState* state = mobius_new_state(&config);
```

| Field                     | Type                     | Default                | Description |
|---------------------------|--------------------------|------------------------|-------------|
| `initial_stack_size`      | `size_t`                 | `256`                  | Initial value-stack capacity |
| `max_stack_size`          | `size_t`                 | `65536`                | Max stack depth before overflow |
| `max_call_depth`          | `size_t`                 | `1000`                 | Max function-call nesting |
| `strict_mode`             | `bool`                   | `false`                | Enforce type annotations at runtime |
| `warn_on_conversion`      | `bool`                   | `false`                | Warn on implicit conversions |
| `debug_mode`              | `bool`                   | `false`                | Extra debug output |
| `override_behavior`       | `MobiusOverrideBehavior` | `MOBIUS_OVERRIDE_ERROR`| Global/function name-conflict policy |
| `fiber_stack_size`        | `size_t`                 | `524288` (512 KiB)     | C stack size for pooled worker fibers (spawned work) |
| `main_fiber_stack_size`   | `size_t`                 | `8388608` (8 MiB)      | C stack size for the top-level script fiber, which hosts the whole script and its deep native calls; `0` falls back to `fiber_stack_size` |
| `initial_fiber_pool_size` | `size_t`                 | `16`                   | Fibers pre-allocated on first spawn |
| `max_fiber_pool_size`     | `size_t`                 | `256`                  | Hard cap on pooled fibers |
| `max_worker_threads`      | `int`                    | `hardware_concurrency() / 2` (≥ 1) | Extra worker threads; `0` = single-threaded cooperative. The calling thread always participates, so total workers = this value + 1. |
| `string_pool_buckets`     | `size_t`                 | `65536`                | Initial string-intern hash buckets (rounded to a power of two; grows as needed). `0` = default. See [Footprint](#footprint). |
| `global_slot_capacity`    | `size_t`                 | `16384`                | Preallocated global-variable slots. **Also the hard cap** — exceeding it is a runtime error, so include headroom for stdlib registrations. `0` = default. |

`MobiusOverrideBehavior` is one of `MOBIUS_OVERRIDE_ERROR` (default),
`MOBIUS_OVERRIDE_WARN`, or `MOBIUS_OVERRIDE_QUIET`.

---

## Footprint

Numbers below are from a Linux x86-64 release build; exact figures vary by
platform and compiler, but the shape holds.

**Disk.** The core library — VM, compiler, fiber runtime, garbage collector,
and the entire no-import standard library — is a single shared object of
about **1.1 MB stripped**. The optional CLI adds ~100 KB. Bundled modules
(`json`, `http`, `sqlite`, …) are separate shared objects loaded only on
`import`; ship just the ones you use (`json` ≈ 55 KB, `crypto` ≈ 59 KB,
`http` ≈ 151 KB stripped).

**Memory.** A minimal host — create a state, register the stdlib, run a small
script — peaks around **6.3 MB resident** with the default configuration.
Decomposed:

| Stage | Peak RSS |
|-------|---------:|
| Bare C process (libc/libstdc++ baseline)     | ~1.4 MB |
| + `mobius_new_state` + `mobius_init_stdlib`  | ~5.7 MB |
| + first script executed                      | ~6.3 MB |

State creation dominates, and most of it is two deliberately speed-sized
preallocations you can shrink through `MobiusConfig`:

```c
MobiusConfig cfg = mobius_default_config();
cfg.string_pool_buckets   = 1024;  /* default 65536 — grows as needed  */
cfg.global_slot_capacity  = 512;   /* default 16384 — hard cap, see below */
cfg.initial_fiber_pool_size = 2;   /* default 16 */
MobiusState* state = mobius_new_state(&cfg);
```

The tuned configuration above measures ~**5.4 MB** peak on the same host.
The remainder is code pages and thread/fiber stacks; fiber stacks are
virtual memory sized by `fiber_stack_size` / `main_fiber_stack_size`, of
which only touched pages become resident.

Two cautions:

- `global_slot_capacity` is a **hard cap** as well as a preallocation. The
  standard library registers ~60 globals; leave headroom for your own. An
  undersized cap fails scripts with *"Global slot capacity exceeded"* rather
  than crashing.
- Shrinking `string_pool_buckets` trades interning speed for memory; the
  pool grows automatically under load, so start small only when memory is
  the constraint.

Startup — state creation through first script execution — is under 10 ms.

---

## Executing scripts

```c
int rc = mobius_exec_string(state, "var x = 42\nprint(x)");
int rc = mobius_exec_file(state, "scripts/init.mob");
```

Both return `MOBIUS_OK` (0) on success; on failure they return an error code and
invoke the registered error handler.

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

`mobius_start_repl(state)` runs the interactive REPL (blocks until exit).

---

## The stack

All data exchange between C and Mobius goes through a value stack, much like
Lua's:

- **Positive indices** count from the bottom: `1` is the first element.
- **Negative indices** count from the top: `-1` is the top.
- Push functions add to the top; pop removes from the top.

```
  Index:   1     2      3     ← positive (from bottom)
Stack:  [ 10 | "hi" | true ]
  Index:  -3    -2     -1     ← negative (from top)
```

### Push

```c
mobius_stack_pushNil(state);
mobius_stack_pushBool(state, true);
mobius_stack_pushInt64(state, 42);
mobius_stack_pushUInt64(state, 1000000ULL);
mobius_stack_pushFloat64(state, 3.14);
mobius_stack_pushString(state, "hello");
mobius_stack_pushStringLength(state, data, len);   // non-NUL-terminated
mobius_stack_pushNewTable(state, capacity_hint);
mobius_stack_pushNewArray(state, capacity_hint);
mobius_stack_pushNewBuffer(state, size);
```

### Read

Permissive getters (`as` prefix) auto-convert; strict getters (`get` prefix)
respect the `strict_types` pragma and error on mismatch:

```c
double      d = mobius_stack_asFloat64(state, -1);
int64_t     i = mobius_stack_asInt64(state, -1);
const char* s = mobius_stack_asString(state, -1);
bool        b = mobius_stack_asBool(state, -1);

size_t len;
const char* raw = mobius_stack_getStringData(state, -1, &len);  // bytes + length

int64_t strict = mobius_stack_getInt64(state, -1);              // strict variant
```

### Inspect & manipulate

```c
int n = mobius_stack_size(state);
MobiusValueType t = mobius_stack_type(state, -1);

mobius_stack_isNumber(state, -1);   // also isInteger/isFloat/isString/isBool/
mobius_stack_isNil(state, -1);      // isNil/isTable/isArray/isFunction/
mobius_stack_isBuffer(state, -1);   // isUserdata/isBuffer

mobius_stack_pop(state, 2);         // pop 2 values
mobius_stack_copy(state, -1);       // duplicate the value at an index
```

---

## Exchanging values

### Globals

```c
mobius_stack_pushString(state, "Mobius 0.1.0");
mobius_stack_setGlobal(state, "APP_VERSION");      // pops the value into a global

mobius_stack_getGlobal(state, "result");           // pushes the global (nil if absent)
if (mobius_stack_isNumber(state, -1)) {
    double v = mobius_stack_asFloat64(state, -1);
}
mobius_stack_pop(state, 1);

mobius_set_global_readonly(state, "APP_VERSION", true);  // scripts can't reassign
mobius_remove_global(state, "APP_VERSION");              // truly remove (not nil)
```

### Tables

```c
mobius_stack_pushNewTable(state, 4);

mobius_stack_pushString(state, "Alice");
mobius_stack_setTableField(state, -2, "name");     // pops value into table[key]

mobius_stack_getTableField(state, -1, "name");     // pushes table[key]
const char* name = mobius_stack_asString(state, -1);
mobius_stack_pop(state, 1);

size_t count = mobius_stack_getTableSize(state, -1);
mobius_stack_getTableKeys(state, -1);              // pushes an array of keys
mobius_stack_pop(state, 1);

mobius_stack_setGlobal(state, "player");
```

### Arrays

```c
mobius_stack_pushNewArray(state, 8);

mobius_stack_pushString(state, "first");
mobius_stack_setArrayElement(state, -2, 0);        // pops value into array[0]

mobius_stack_getArrayElement(state, -1, 0);        // pushes array[0]
size_t len = mobius_stack_getArrayLength(state, -1);

mobius_stack_pushInt64(state, 99);
mobius_stack_arrayPush(state, -2);                 // append top to the array
mobius_stack_arrayPop(state, -1);                  // pop the array's last element
mobius_stack_arrayInsert(state, arr_idx, index);   // insert top at index
mobius_stack_arrayRemove(state, arr_idx, index);   // remove element at index
```

### Buffers

```c
mobius_stack_pushNewBuffer(state, 1024);            // owned, zeroed
mobius_stack_pushBufferCopy(state, data, size);     // copy of external bytes

// Wrap external memory without copying; release() is called when the
// buffer is collected. Pass readonly = true to forbid writes from scripts.
mobius_stack_pushBufferExternal(state, ptr, size, release_fn, userdata, false);

size_t size;
void* p = mobius_stack_getBufferData(state, -1, &size);   // raw pointer
size_t n = mobius_stack_getBufferSize(state, -1);
bool fixed = mobius_stack_bufferIsFixed(state, -1);
bool ro = mobius_stack_bufferIsReadonly(state, -1);
```

### Userdata

Userdata passes an arbitrary C pointer into scripts; attach a destructor for
cleanup when the value is collected.

```c
typedef struct { float x, y, z; } Vec3;
void vec3_destroy(void* p) { free(p); }

Vec3* v = malloc(sizeof(Vec3));
mobius_stack_pushUserdata(state, v, vec3_destroy, "Vec3", sizeof(Vec3));
mobius_stack_setGlobal(state, "position");

mobius_stack_getGlobal(state, "position");
const char* type_name = NULL;
Vec3* pos = (Vec3*)mobius_stack_getUserdata(state, -1, &type_name);
mobius_stack_pop(state, 1);
```

To give a userdata type methods callable from scripts, register a
[userdata type metatable](#type-metatables).

---

## Registering native functions

A native function has the signature:

```c
int my_function(MobiusState* state, int arg_count);
```

Arguments are on the stack (last argument at `-1`). Read them, pop what you
consume, push return values, and return the count pushed (`>= 0`). On error,
return `mobius_error()` (always negative).

```c
#include <mobius/mobius_plugin.h>

int native_square(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "square() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "square() expects a number");

    double v = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, v * v);
    return 1;               // one return value
}

// ... after mobius_init_stdlib():
mobius_register_function(state, "square", native_square);
```

```mobius
print(square(9))    // 81
```

Return multiple values by pushing several and returning the count; return
nothing by returning `0`.

---

## Calling Mobius functions from C

`mobius_pcall` calls a function already set up on the stack (push the function,
then its arguments):

```c
mobius_stack_getGlobal(state, "greet");      // push the function
mobius_stack_pushString(state, "World");     // push 1 argument
int nresults = mobius_pcall(state, /*nargs=*/1, /*nresults=*/1);
if (nresults >= 0) {
    const char* msg = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
}
```

`mobius_pcall` returns the number of results on success (≥ 0) or a negative
error code.

---

## Rooted value handles

A `MobiusValueRef` keeps a value alive across calls without holding a stack
slot — useful for storing callbacks or objects in your host structures.

```c
MobiusValueRef ref = mobius_ref_value(state, -1);   // root the value at -1
// ... later ...
mobius_push_ref(state, ref);                          // push it back onto the stack
mobius_unref_value(state, ref);                       // release when done
```

`mobius_call_ref(state, fn_ref, arg_refs, nargs, nresults)` calls a rooted
function with rooted arguments, pushing the results onto the current stack —
handy for invoking stored callbacks from arbitrary C code.

---

## Error handling

By default, errors print to `stderr` with line/column info. Install a callback
to intercept them:

```c
void my_error_handler(MobiusState* state, const MobiusError* error, void* userdata) {
    FILE* log = (FILE*)userdata;
    fprintf(log, "[ERROR %d] %s:%d:%d: %s\n",
            error->code, error->filename ? error->filename : "?",
            error->line, error->column, error->message);
    if (error->suggestion)    fprintf(log, "  hint: %s\n", error->suggestion);
    if (error->function_name) fprintf(log, "  in: %s\n", error->function_name);
}

mobius_set_error_handler(state, my_error_handler, logfile);   // returns previous handler
```

The `MobiusError` fields are `code`, `message`, `suggestion` (may be NULL),
`filename` (may be NULL), `line`, `column`, and `function_name` (may be NULL).
**All pointers are valid only for the duration of the callback** — copy them if
you need to keep them.

After handling an error, call `mobius_clear_error(state)` so execution can
continue. From inside a native function, report errors with
`mobius_error(state, "message")` and return its result.

---

## Loading plugins

Plugins are shared libraries (`.so`/`.dll`) that export `mobius_plugin_info()`.
See the [Plugin Guide](plugin-guide.md) for how to write them.

Plugin search directories are **per-state**; add them after creating the state:

```c
mobius_add_plugin_directory(state, "./modules");
mobius_add_plugin_directory(state, "/usr/lib/mobius/modules");
mobius_clear_plugin_directories(state);   // reset all search paths
```

The interpreter scans these directories when a script runs `import "name"`,
loading the matching module lazily:

```mobius
import "math"
print(math.sin(3.14))
```

---

## Type metatables

A type metatable attaches methods to **all** values of a type. When a script
uses `obj:method(...)`, the VM looks the method up in the metatable for `obj`'s
type. This is how the built-in array, table, buffer, and channel methods work.
`mobius_init_stdlib()` installs the standard metatables; you can extend them or
add metatables for other types.

```c
// Read the current array metatable (pushes nil if none set)
mobius_push_type_metatable(state, MOBIUS_VAL_ARRAY);

// Add a method to it, then re-install
mobius_register_function(state, "__temp_sum", native_array_sum);
mobius_stack_getGlobal(state, "__temp_sum");
mobius_stack_setTableField(state, -2, "sum");
mobius_remove_global(state, "__temp_sum");
mobius_set_type_metatable(state, MOBIUS_VAL_ARRAY);
```

For userdata, register methods by **type name** so different userdata types can
have different prototypes:

```c
mobius_push_userdata_type_metatable(state, "Vec3");   // or nil
mobius_set_userdata_type_metatable(state, "Vec3");    // pop + install
```

A userdata type metatable resolves `obj.field` / `obj:method()` like a regular
table: it checks the metatable directly, then follows `__index` — which may be a
**function** (called as `__index(obj, key)`) or a **table** (a prototype chain,
followed recursively, exactly as with `setmetatable`). Lookups that miss the
type metatable fall through to the generic `MOBIUS_VAL_USERDATA` metatable. So
you can give userdata full prototype-style inheritance:

```mobius
var Base  = { kind: func(self) { return "base" } }
var Proto = { describe: func(self) { return "proto" } }
setmetatable(Proto, { __index: Base })   // Proto inherits from Base
// (register Proto as the userdata type metatable from C)
// obj:describe() resolves in Proto; obj:kind() chains through to Base.
```

A method's receiver (`self`) arrives as the first argument on the native stack:

```c
int native_array_sum(MobiusState* state, int arg_count) {
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

---

## Multiple interpreters

Each `MobiusState` is fully independent — its own globals, stack, plugin paths,
and fiber pool — so you can run several concurrently:

```c
MobiusState* a = mobius_new_state(NULL);
MobiusState* b = mobius_new_state(NULL);
mobius_init_stdlib(a);
mobius_init_stdlib(b);

mobius_stack_pushInt64(a, 100); mobius_stack_setGlobal(a, "value");
mobius_stack_pushInt64(b, 200); mobius_stack_setGlobal(b, "value");

mobius_exec_string(a, "print(value)");   // 100
mobius_exec_string(b, "print(value)");   // 200

mobius_free_state(a);
mobius_free_state(b);
```

---

## Concurrency and fibers

Scripts use `spawn`, `await`, `yield`, `shared`, and `atomic` to run fibers
concurrently within a single state (see [Concurrency](../guide/concurrency.md)).
Each state owns a fiber pool and worker threads, configured via `MobiusConfig`:

```c
MobiusConfig config = mobius_default_config();
config.max_worker_threads     = 8;
config.initial_fiber_pool_size = 32;
config.max_fiber_pool_size     = 2048;
config.fiber_stack_size        = 256 * 1024;
MobiusState* state = mobius_new_state(&config);
```

Threading model:

- Each state maintains a pool of worker threads (`max_worker_threads`); the
  calling thread also participates as a worker.
- The **top-level script runs on a dedicated main fiber** with its own,
  larger stack (`main_fiber_stack_size`, default 8 MiB). Pooled worker fibers
  use `fiber_stack_size` (default 512 KiB). Because native calls the script
  makes directly run on the main fiber's stack, the main fiber is sized to
  behave like a normal thread stack — size it for the deepest native call
  chain you expect (e.g. graphics drivers). Native calls made from inside a
  `spawn`ed function run on a worker fiber's smaller stack.
- `spawn` acquires a fiber from the pool and schedules it on a worker.
- Reference counts are atomic and shared containers use locks.
- Different `MobiusState` instances share no mutable state and run independently.
- Native functions may be invoked from any worker thread — make them
  thread-safe if they touch shared C/C++ state.

---

## Metrics

`mobius_get_metrics` copies a snapshot into a caller-provided struct (it returns
`void`):

```c
MobiusMetrics m;
mobius_get_metrics(state, &m);
printf("fibers spawned: %zu\n", m.total_fibers_spawned);
printf("jobs executed:  %zu\n", m.total_jobs_executed);
printf("peak fibers:    %zu\n", m.peak_fibers);
printf("exec time (ns): %llu\n", (unsigned long long)m.total_execution_time_ns);

mobius_reset_metrics(state);
```

`MobiusMetrics` includes VM high-water marks (`peak_call_depth`,
`peak_registers`, `peak_upvalues`, `peak_try_depth`), state marks
(`peak_globals`, `peak_interned_strings`), fiber/thread stats
(`peak_fibers`, `peak_worker_threads`, `total_fibers_spawned`,
`total_jobs_executed`, `peak_fiber_stack_bytes`, `avg_fiber_stack_bytes`), and
`total_execution_time_ns`.

---

Next: [Plugin Guide](plugin-guide.md) · [Packaging](packaging.md).
