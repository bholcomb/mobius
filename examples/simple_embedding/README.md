# Simple Embedding Example

A minimal example showing how to embed Mobius in a C application. This demonstrates the core embedding functionality without complex features.

## Files

- `simple_embedding.c` - Minimal C application with embedded Mobius interpreter

## Features Demonstrated

- **Basic Embedding**: Creating and initializing a Mobius interpreter state
- **Script Execution**: Running Mobius scripts from C strings
- **Error Handling**: Checking for and handling execution errors
- **Plugin Loading**: Dynamically loading extension modules
- **Resource Management**: Proper cleanup of interpreter resources

## What This Example Does

1. **Initializes Mobius**: Creates interpreter state and loads core functions
2. **Executes a script**: Runs basic arithmetic and function calls
3. **Tests plugin loading**: Attempts to load the math extension
4. **Demonstrates cleanup**: Shows proper resource management

## Code Overview

### 1. Initialization

```c
// Create interpreter state
MobiusState* state = mobius_new_state();
if (!state) {
    printf("❌ Failed to create Mobius state\n");
    return 1;
}

// Initialize core functionality
if (mobius_init_core(state) != MOBIUS_OK) {
    printf("❌ Failed to initialize Mobius core\n");
    mobius_free_state(state);
    return 1;
}
```

### 2. Script Execution

```c
const char* script = 
    "print(\"Hello from embedded Mobius!\");\n"
    "var x = 10;\n"
    "var y = 20;\n"
    "print(\"x =\", x, \", y =\", y);\n"
    "print(\"x + y =\", x + y);\n"
    "print(\"sqrt(x * y) =\", sqrt(x * y));\n";

int result = mobius_exec_string(state, script);
if (result != MOBIUS_OK) {
    printf("❌ Script execution failed!\n");
    MobiusError* error = mobius_get_last_error(state);
    if (error) {
        printf("Error: %s\n", error->message);
        mobius_free_error(error);
    }
}
```

### 3. Plugin Loading

```c
printf("🔌 Testing plugin loading...\n");
if (mobius_load_plugin(state, "./bin/modules/math.so") == MOBIUS_OK) {
    printf("✅ Math plugin loaded!\n");
    printf("Updated function count: %zu\n", mobius_function_count(state));
    
    // Test math plugin function
    const char* math_script = "print(\"sin(pi() / 2) =\", sin(pi() / 2));";
    mobius_exec_string(state, math_script);
} else {
    printf("ℹ️  Math plugin not available (this is okay)\n");
}
```

### 4. Cleanup

```c
// Always cleanup when done
mobius_free_state(state);
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
./bin/simple_embedding
```

## Expected Output

```
🚀 Simple Mobius Embedding Example
===================================

✅ Mobius interpreter initialized
Version: 0.1.0
Available functions: 22

📝 Executing Mobius script:
---------------------------
Hello from embedded Mobius!
x = 10 , y = 20
x + y = 30
sqrt(x * y) = 14.14213562373095

✅ Script executed successfully!

🔌 Testing plugin loading...
✅ Math plugin loaded!
Updated function count: 40

📝 Testing math plugin:
sin(pi() / 2) = 1

🧹 Cleaning up...
✅ Mobius embedding example completed!
```

## Key API Functions

| Function | Purpose | Returns |
|----------|---------|---------|
| `mobius_new_state()` | Create new interpreter | `MobiusState*` or `NULL` |
| `mobius_init_core(state)` | Initialize core functions | `MOBIUS_OK` or error |
| `mobius_exec_string(state, script)` | Execute script string | `MOBIUS_OK` or error |
| `mobius_load_plugin(state, path)` | Load plugin module | `MOBIUS_OK` or error |
| `mobius_get_last_error(state)` | Get last error details | `MobiusError*` or `NULL` |
| `mobius_free_state(state)` | Clean up interpreter | `void` |

## Error Handling Best Practices

1. **Always check return codes**: Most functions return `MOBIUS_OK` on success
2. **Handle initialization failures**: Check if `mobius_new_state()` returns `NULL`
3. **Get error details**: Use `mobius_get_last_error()` for detailed error information
4. **Free error objects**: Call `mobius_free_error()` after using error objects
5. **Clean up on exit**: Always call `mobius_free_state()` when done

## Use Cases

This simple embedding pattern is perfect for:

- **Quick prototyping**: Test Mobius integration in existing applications
- **Configuration scripts**: Allow users to write configuration in Mobius
- **Simple automation**: Add scripting to command-line tools
- **Learning**: Understand the basics before building complex integrations
- **Testing**: Validate Mobius functionality in minimal environments

## Next Steps

After understanding this example, explore:

- [Game Engine Example](../game_engine/) - More complex embedding with custom API
- [Multi Environment Demo](../multi_environment_demo/) - Multiple interpreter instances
- [Embedding Example](../embedding_example/) - Advanced embedding features

This example provides the foundation for all Mobius embedding scenarios!
