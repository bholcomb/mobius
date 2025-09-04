# Mobius Embedding API Guide

This guide demonstrates how to embed the Mobius scripting language into your C application, similar to how you would embed Lua or Python.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [API Reference](#api-reference)
4. [Examples](#examples)
5. [Best Practices](#best-practices)
6. [Advanced Features](#advanced-features)

## Quick Start

### Basic Embedding Example

```c
#include "mobius/mobius.h"

int main(void) {
    // Create interpreter state
    MobiusState* state = mobius_new_state();
    if (!state) {
        printf("Failed to create Mobius state\n");
        return 1;
    }
    
    // Initialize core functionality
    if (mobius_init_core(state) != MOBIUS_OK) {
        printf("Failed to initialize Mobius core\n");
        mobius_free_state(state);
        return 1;
    }
    
    // Execute a script
    const char* script = 
        "print(\"Hello from Mobius!\");\n"
        "var result = 2 + 3;\n"
        "print(\"2 + 3 =\", result);\n";
    
    int result = mobius_exec_string(state, script);
    if (result != MOBIUS_OK) {
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            printf("Error: %s\n", error->message);
            mobius_free_error(error);
        }
    }
    
    // Cleanup
    mobius_free_state(state);
    return 0;
}
```

### Building Your Application

```makefile
# Link against libmobius.a
my_app: main.c
    gcc -o my_app main.c -Ipath/to/mobius/src -Lpath/to/mobius/build -lmobius -lm -ldl
```

## Core Concepts

### 1. MobiusState

The `MobiusState` is the main context for an embedded Mobius interpreter instance. It contains:

- Global environment (variables and functions)
- Plugin registry
- Error state
- Custom function registry

```c
// Create a new interpreter instance
MobiusState* state = mobius_new_state();

// Initialize with core functionality
mobius_init_core(state);

// Always clean up when done
mobius_free_state(state);
```

### 2. Value Exchange

The API provides safe wrappers for exchanging values between C and Mobius:

```c
// Create values from C
MobiusValue* str_val = mobius_create_string(state, "Hello");
MobiusValue* num_val = mobius_create_integer(state, 42);
MobiusValue* bool_val = mobius_create_bool(state, true);

// Set global variables
mobius_set_global(state, "message", str_val);
mobius_set_global(state, "count", num_val);
mobius_set_global(state, "flag", bool_val);

// Execute script that uses these variables
mobius_exec_string(state, "print(message, count, flag);");

// Get variables back
MobiusValue* result = mobius_get_global(state, "some_result");
if (result && mobius_is_integer(result)) {
    int64_t value = mobius_to_integer(result);
    printf("Result: %ld\n", value);
}

// Clean up
mobius_free_value(str_val);
mobius_free_value(num_val);
mobius_free_value(bool_val);
mobius_free_value(result);
```

### 3. Custom C Functions

Register C functions that can be called from Mobius scripts:

```c
// Custom function implementation
int my_add(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    // Validate arguments
    MOBIUS_CHECK_ARG_COUNT(2);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_integer, "integer");
    MOBIUS_CHECK_ARG_TYPE(1, mobius_is_integer, "integer");
    
    // Get values
    int64_t a = mobius_to_integer(args[0]);
    int64_t b = mobius_to_integer(args[1]);
    
    // Return result
    *result = mobius_create_integer(state, a + b);
    return MOBIUS_OK;
}

// Register the function
mobius_register_function(state, "my_add", my_add, 2, "Add two integers");

// Use in script
mobius_exec_string(state, "print(\"5 + 3 =\", my_add(5, 3));");
```

### 4. Error Handling

The API provides comprehensive error handling:

```c
int result = mobius_exec_string(state, "invalid syntax");
if (result != MOBIUS_OK) {
    MobiusError* error = mobius_get_last_error(state);
    if (error) {
        printf("Error Code: %d\n", error->code);
        printf("Message: %s\n", error->message);
        if (error->suggestion) {
            printf("Suggestion: %s\n", error->suggestion);
        }
        if (error->line > 0) {
            printf("Location: line %d, column %d\n", error->line, error->column);
        }
        mobius_free_error(error);
    }
}
```

## API Reference

### State Management

| Function | Description |
|----------|-------------|
| `mobius_new_state()` | Create new interpreter state |
| `mobius_free_state(state)` | Free interpreter state |
| `mobius_init_core(state)` | Initialize core functionality |
| `mobius_load_plugin(state, path)` | Load a plugin module |

### Script Execution

| Function | Description |
|----------|-------------|
| `mobius_exec_string(state, code)` | Execute Mobius code string |
| `mobius_exec_file(state, filename)` | Execute Mobius script file |
| `mobius_eval_string(state, code, result)` | Execute and return result |

### Value Management

| Function | Description |
|----------|-------------|
| `mobius_create_nil(state)` | Create nil value |
| `mobius_create_bool(state, value)` | Create boolean value |
| `mobius_create_integer(state, value)` | Create integer value |
| `mobius_create_float(state, value)` | Create float value |
| `mobius_create_string(state, value)` | Create string value |
| `mobius_free_value(value)` | Free a value |

### Type Checking

| Function | Description |
|----------|-------------|
| `mobius_is_nil(value)` | Check if value is nil |
| `mobius_is_bool(value)` | Check if value is boolean |
| `mobius_is_integer(value)` | Check if value is integer |
| `mobius_is_float(value)` | Check if value is float |
| `mobius_is_string(value)` | Check if value is string |

### Value Extraction

| Function | Description |
|----------|-------------|
| `mobius_to_bool(value)` | Extract boolean (strict) |
| `mobius_to_integer(value)` | Extract integer (strict) |
| `mobius_to_float(value)` | Extract float (strict) |
| `mobius_to_string(value)` | Extract string (strict) |
| `mobius_convert_to_bool(value)` | Convert to boolean |
| `mobius_convert_to_integer(value)` | Convert to integer |
| `mobius_convert_to_float(value)` | Convert to float |
| `mobius_convert_to_string(value)` | Convert to string (caller frees) |

### Variable Management

| Function | Description |
|----------|-------------|
| `mobius_set_global(state, name, value)` | Set global variable |
| `mobius_get_global(state, name)` | Get global variable |
| `mobius_has_global(state, name)` | Check if global exists |

### Function Registration

| Function | Description |
|----------|-------------|
| `mobius_register_function(state, name, func, arg_count, desc)` | Register global function |
| `mobius_register_module_function(state, module, name, func, arg_count, desc)` | Register namespaced function |

### Error Handling

| Function | Description |
|----------|-------------|
| `mobius_get_last_error(state)` | Get last error info |
| `mobius_clear_error(state)` | Clear error state |
| `mobius_free_error(error)` | Free error info |
| `mobius_set_error(state, code, message)` | Set error (for custom functions) |

### Utility Functions

| Function | Description |
|----------|-------------|
| `mobius_version_string()` | Get version string |
| `mobius_plugin_count(state)` | Get number of loaded plugins |
| `mobius_function_count(state)` | Get total function count |

## Examples

### 1. Basic Application Integration

```c
// game.c - Simple game with Mobius scripting
#include "mobius/mobius.h"

typedef struct {
    int player_health;
    int player_score;
    MobiusState* script_state;
} GameState;

int game_get_health(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    
    // Get game state from somewhere (global variable, passed context, etc.)
    extern GameState* g_game;
    
    *result = mobius_create_integer(state, g_game->player_health);
    return MOBIUS_OK;
}

int game_set_health(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_integer, "integer");
    
    extern GameState* g_game;
    g_game->player_health = (int)mobius_to_integer(args[0]);
    
    *result = mobius_create_nil(state);
    return MOBIUS_OK;
}

void init_game_scripting(GameState* game) {
    game->script_state = mobius_new_state();
    mobius_init_core(game->script_state);
    
    // Register game functions
    mobius_register_function(game->script_state, "get_health", game_get_health, 0, "Get player health");
    mobius_register_function(game->script_state, "set_health", game_set_health, 1, "Set player health");
    
    // Load game scripts
    mobius_exec_file(game->script_state, "scripts/init.mob");
}

void run_game_event(GameState* game, const char* event_script) {
    mobius_exec_string(game->script_state, event_script);
}
```

### 2. Configuration System

```c
// config.c - Application configuration via Mobius
#include "mobius/mobius.h"

typedef struct {
    char* window_title;
    int window_width;
    int window_height;
    bool fullscreen;
} AppConfig;

AppConfig load_config(const char* config_file) {
    AppConfig config = {0};
    
    MobiusState* state = mobius_new_state();
    mobius_init_core(state);
    
    // Set default values
    mobius_set_global(state, "window_title", mobius_create_string(state, "My App"));
    mobius_set_global(state, "window_width", mobius_create_integer(state, 800));
    mobius_set_global(state, "window_height", mobius_create_integer(state, 600));
    mobius_set_global(state, "fullscreen", mobius_create_bool(state, false));
    
    // Execute config script
    if (mobius_exec_file(state, config_file) == MOBIUS_OK) {
        // Extract configuration values
        MobiusValue* title = mobius_get_global(state, "window_title");
        if (title && mobius_is_string(title)) {
            config.window_title = strdup(mobius_to_string(title));
        }
        
        MobiusValue* width = mobius_get_global(state, "window_width");
        if (width && mobius_is_integer(width)) {
            config.window_width = (int)mobius_to_integer(width);
        }
        
        // ... extract other values
    }
    
    mobius_free_state(state);
    return config;
}
```

### 3. Plugin System Extension

```c
// app_with_plugins.c
#include "mobius/mobius.h"

void load_application_plugins(MobiusState* state) {
    // Try to load various plugins
    const char* plugins[] = {
        "./plugins/math.so",
        "./plugins/file.so",
        "./plugins/network.so",
        "./plugins/graphics.so"
    };
    
    for (size_t i = 0; i < sizeof(plugins) / sizeof(plugins[0]); i++) {
        if (mobius_load_plugin(state, plugins[i]) == MOBIUS_OK) {
            printf("✅ Loaded plugin: %s\n", plugins[i]);
        } else {
            printf("ℹ️  Plugin not available: %s\n", plugins[i]);
        }
    }
    
    printf("Total available functions: %zu\n", mobius_function_count(state));
}
```

## Best Practices

### 1. Error Handling

Always check return codes and handle errors gracefully:

```c
int result = mobius_exec_string(state, script);
if (result != MOBIUS_OK) {
    MobiusError* error = mobius_get_last_error(state);
    if (error) {
        log_error("Mobius error: %s", error->message);
        mobius_free_error(error);
    }
    return false; // Handle the error appropriately
}
```

### 2. Memory Management

Always free allocated resources:

```c
MobiusValue* value = mobius_create_string(state, "Hello");
// ... use the value
mobius_free_value(value); // Don't forget this!
```

### 3. State Isolation

Use separate states for different contexts:

```c
MobiusState* ui_state = mobius_new_state();    // For UI scripts
MobiusState* game_state = mobius_new_state();  // For game logic
MobiusState* config_state = mobius_new_state(); // For configuration
```

### 4. Custom Function Validation

Use the validation macros for robustness:

```c
int my_function(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(2);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_string, "string");
    MOBIUS_CHECK_ARG_TYPE(1, mobius_is_integer, "integer");
    
    // Function implementation...
    return MOBIUS_OK;
}
```

## Advanced Features

### 1. Multiple Interpreter Instances

```c
// Create isolated interpreter instances
MobiusState* user_scripts = mobius_new_state();
MobiusState* system_scripts = mobius_new_state();
MobiusState* config_scripts = mobius_new_state();

// Each has its own environment and can load different plugins
mobius_load_plugin(user_scripts, "./plugins/user_api.so");
mobius_load_plugin(system_scripts, "./plugins/system_api.so");
```

### 2. Context Passing to Custom Functions

```c
// Store application context in a global or thread-local variable
// that your custom functions can access
__thread MyAppContext* current_app_context = NULL;

int my_function(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MyAppContext* app = current_app_context;
    if (!app) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "No application context");
    }
    
    // Use the application context...
    return MOBIUS_OK;
}
```

### 3. Sandboxing and Security

```c
// Create a restricted environment
MobiusState* sandbox = mobius_new_state();
mobius_init_core(sandbox);

// Don't load potentially dangerous plugins
// Only register safe functions
mobius_register_function(sandbox, "safe_math", safe_math_function, 2, "Safe math operations");

// Execute untrusted scripts in the sandbox
mobius_exec_string(sandbox, untrusted_script);
```

## Error Codes

| Code | Constant | Description |
|------|----------|-------------|
| 0 | `MOBIUS_OK` | Success |
| 1 | `MOBIUS_ERROR_SYNTAX` | Syntax error |
| 2 | `MOBIUS_ERROR_RUNTIME` | Runtime error |
| 3 | `MOBIUS_ERROR_MEMORY` | Memory allocation error |
| 4 | `MOBIUS_ERROR_TYPE` | Type error |
| 5 | `MOBIUS_ERROR_ARGUMENT` | Argument error |

## Conclusion

The Mobius embedding API provides a comprehensive and safe way to integrate the Mobius scripting language into your C applications. With features like isolated interpreter states, bidirectional value exchange, custom function registration, and robust error handling, you can easily add scripting capabilities to your application.

For more examples and advanced usage, see the `examples/` directory in the Mobius source code.
