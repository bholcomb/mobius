# Comprehensive Embedding Example

This example demonstrates how to embed the Mobius scripting language in a C application, similar to embedding Lua or Python. It showcases advanced embedding techniques beyond the simple embedding example.

## Files

- `embedding_example.c` - Comprehensive C application demonstrating advanced embedding features

## Features Demonstrated

- **Creating and managing interpreter states**
- **Executing Mobius scripts from C**
- **Exchanging values between C and Mobius**
- **Registering custom C functions**
- **Error handling**
- **Loading plugins**
- **Advanced integration patterns**

## What This Example Does

1. **Custom Function Registration**: Shows how to expose C functions to Mobius scripts
2. **Bidirectional Value Exchange**: Demonstrates passing data between C and Mobius
3. **Error Handling**: Comprehensive error checking and reporting
4. **Plugin Integration**: Loading and using extension modules
5. **Advanced Script Execution**: Multiple script execution patterns

## Key Components

### 1. Custom C Functions

The example registers several custom functions that can be called from Mobius scripts:

```c
/**
 * Custom function: add two numbers
 * Demonstrates basic value exchange and error handling
 */
int custom_add(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    // Validate argument count
    if (arg_count != 2) {
        mobius_set_error(state, "custom_add() expects exactly 2 arguments");
        return MOBIUS_ERROR;
    }
    
    // Validate argument types
    if (!mobius_is_number(args[0]) || !mobius_is_number(args[1])) {
        mobius_set_error(state, "custom_add() expects numeric arguments");
        return MOBIUS_ERROR;
    }
    
    // Perform operation
    double a = mobius_to_number(args[0]);
    double b = mobius_to_number(args[1]);
    double sum = a + b;
    
    // Return result
    *result = mobius_from_number(sum);
    return MOBIUS_OK;
}
```

### 2. Function Registration

```c
// Register custom functions
mobius_register_function(state, "custom_add", custom_add, 
                        "Add two numbers (custom C function)");
mobius_register_function(state, "get_system_info", get_system_info,
                        "Get system information");
mobius_register_function(state, "log_message", log_message,
                        "Log a message with timestamp");
```

### 3. Value Exchange Patterns

**From C to Mobius:**
```c
// Set variables in Mobius from C
mobius_set_global_number(state, "app_version", 1.5);
mobius_set_global_string(state, "app_name", "Mobius Demo App");
mobius_set_global_boolean(state, "debug_mode", true);
```

**From Mobius to C:**
```c
// Get values from Mobius in C
MobiusValue* result = mobius_get_global(state, "calculated_result");
if (mobius_is_number(result)) {
    double value = mobius_to_number(result);
    printf("Result from script: %f\n", value);
}
mobius_free_value(result);
```

### 4. Error Handling

```c
int result = mobius_exec_string(state, script);
if (result != MOBIUS_OK) {
    MobiusError* error = mobius_get_last_error(state);
    if (error) {
        printf("Script Error: %s\n", error->message);
        printf("Line: %d, Column: %d\n", error->line, error->column);
        mobius_free_error(error);
    }
}
```

## Building and Running

### Prerequisites
- GCC with C99 support
- Mobius library (built with `make`)

### Building
```bash
make examples
```

### Running
```bash
./bin/embedding_example
```

## Example Scripts

The example executes several scripts demonstrating different features:

### Basic Computation Script
```javascript
// Use custom C function
var result = custom_add(10, 20);
print("Custom add result:", result);

// Use system info function
var info = get_system_info();
print("System info:", info);

// Use built-in functions
var x = sqrt(16);
print("sqrt(16) =", x);
```

### Value Exchange Script
```javascript
// Access variables set from C
print("App name:", app_name);
print("App version:", app_version);
print("Debug mode:", debug_mode);

// Set variables that C can read
var calculated_result = app_version * 100;
var user_message = "Hello from " + app_name;
```

### Error Handling Demo
```javascript
// Intentional error to test error handling
try_divide_by_zero = 5 / 0;  // This will trigger error handling
```

## Advanced Patterns

### 1. Multiple Interpreter Instances

```c
// Create multiple independent interpreters
MobiusState* state1 = mobius_new_state();
MobiusState* state2 = mobius_new_state();

// Each has independent global state
mobius_exec_string(state1, "var x = 10;");
mobius_exec_string(state2, "var x = 20;");

// x is different in each interpreter
```

### 2. Script File Loading

```c
// Load and execute script files
int result = mobius_exec_file(state, "config.mob");
if (result != MOBIUS_OK) {
    handle_script_error(state);
}
```

### 3. Function Call from C

```c
// Call Mobius functions from C
MobiusValue* args[2] = {
    mobius_from_number(5),
    mobius_from_number(3)
};

MobiusValue* result;
int status = mobius_call_function(state, "my_function", args, 2, &result);
if (status == MOBIUS_OK) {
    // Use result
    mobius_free_value(result);
}
```

## Best Practices Demonstrated

1. **Always validate arguments** in custom functions
2. **Use proper error codes** and set error messages
3. **Free allocated values** to prevent memory leaks
4. **Check return codes** from all API functions
5. **Handle plugin loading gracefully** (plugins may not be available)
6. **Separate initialization** from execution for better error handling

## API Functions Used

| Function | Purpose |
|----------|---------|
| `mobius_new_state()` | Create interpreter instance |
| `mobius_register_function()` | Register custom C function |
| `mobius_set_global_*()` | Set global variables from C |
| `mobius_get_global()` | Get global variables in C |
| `mobius_exec_string()` | Execute script string |
| `mobius_exec_file()` | Execute script file |
| `mobius_call_function()` | Call Mobius function from C |
| `mobius_load_plugin()` | Load extension plugin |
| `mobius_get_last_error()` | Get detailed error information |

## Use Cases

This advanced embedding pattern is suitable for:

- **Application scripting**: Add user customization to applications
- **Configuration systems**: Complex configuration with logic
- **Plugin architectures**: Extensible application frameworks
- **Game engines**: Scriptable game logic and AI
- **Automation tools**: Complex automation with custom functions
- **Data processing**: Scriptable data transformation pipelines

## Comparison with Simple Embedding

| Feature | Simple Embedding | Comprehensive Embedding |
|---------|------------------|-------------------------|
| Custom Functions | ❌ No | ✅ Yes |
| Value Exchange | ❌ Limited | ✅ Full bidirectional |
| Error Details | ❌ Basic | ✅ Comprehensive |
| Multiple Scripts | ❌ Single | ✅ Multiple patterns |
| File Loading | ❌ No | ✅ Yes |
| Function Calls | ❌ No | ✅ C → Mobius calls |

This example serves as a complete reference for production-quality Mobius embedding!
