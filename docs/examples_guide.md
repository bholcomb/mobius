# Mobius Examples Guide

This guide covers the practical examples included with Mobius, demonstrating both embedding the interpreter in applications and creating custom plugins.

## Table of Contents

1. [Game Engine Example](#game-engine-example) - Embedding Mobius in Applications
2. [Text Processing Plugin](#text-processing-plugin) - Creating Custom Extensions
3. [Building and Running](#building-and-running)
4. [Advanced Techniques](#advanced-techniques)

---

## Game Engine Example

**File:** `examples/game_engine.c`

This example demonstrates how to embed Mobius in a real-world application - a simple 2D game engine where game logic is scripted in Mobius.

### Features Demonstrated

- **Application Integration**: Embedding Mobius in a C application
- **Custom API Exposure**: Registering C functions callable from scripts
- **Event-Driven Architecture**: Handling game events through scripts
- **Dynamic Script Loading**: Loading and executing scripts at runtime
- **State Management**: Sharing application state with scripts
- **Error Handling**: Robust error handling in embedded scenarios

### Architecture Overview

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Game Engine   │◄──►│  Mobius State   │◄──►│  Game Scripts   │
│   (C Code)      │    │  (Interpreter)  │    │  (.mob files)   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Game State    │    │   Custom API    │    │ Event Handlers  │
│ - Player pos    │    │ - get_health()  │    │ - on_damage()   │
│ - Health/Score  │    │ - spawn_enemy() │    │ - on_powerup()  │
│ - Enemies       │    │ - game_log()    │    │ - on_move()     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Game API Functions

The example exposes these C functions to Mobius scripts:

| Function | Parameters | Returns | Description |
|----------|------------|---------|-------------|
| `get_player_pos()` | none | string | Get player position as "x,y" |
| `set_player_pos(x, y)` | float, float | nil | Set player position |
| `get_player_health()` | none | integer | Get current health |
| `set_player_health(health)` | integer | nil | Set health (0-100) |
| `get_score()` | none | integer | Get current score |
| `add_score(points)` | integer | integer | Add points to score |
| `spawn_enemy(x, y, type)` | float, float, int | integer | Spawn enemy at position |
| `get_level()` | none | integer | Get current level |
| `game_log(message)` | string | nil | Log a game message |

### Script Example

The engine automatically loads `scripts/game_init.mob`:

```javascript
// Game Initialization Script
game_log("Initializing Mobius Adventure...");

// Set up initial game state
set_player_pos(25, 25);
set_player_health(100);

// Define event handlers
func on_player_move(direction) {
    var pos = get_player_pos();
    game_log("Player moved " + direction + " to " + pos);
    
    // Award points for movement
    add_score(10);
}

func on_damage(amount) {
    var current_health = get_player_health();
    var new_health = current_health - int(amount);
    set_player_health(new_health);
    
    if (new_health <= 0) {
        game_log("Game Over! Final score: " + str(get_score()));
    } else {
        game_log("Player took " + amount + " damage. Health: " + str(new_health));
    }
}

func on_powerup(type) {
    if (type == "health") {
        var current_health = get_player_health();
        set_player_health(min(100, current_health + 25));
        game_log("Health restored!");
    } else if (type == "score") {
        add_score(500);
        game_log("Bonus points!");
    }
}
```

### Key Implementation Details

#### 1. State Management

```c
typedef struct {
    Player player;
    Enemy enemies[10];
    int enemy_count;
    int game_running;
    int level;
    MobiusState* script_state;  // Embedded interpreter
} GameEngine;
```

#### 2. Function Registration

```c
void init_game_engine(GameEngine* game) {
    // Create interpreter
    game->script_state = mobius_new_state();
    mobius_init_core(game->script_state);
    
    // Register game API
    mobius_register_function(game->script_state, "get_player_pos", 
                           game_get_player_pos, 0, "Get player position");
    mobius_register_function(game->script_state, "set_player_health", 
                           game_set_player_health, 1, "Set player health");
    // ... register other functions
}
```

#### 3. Event Handling

```c
void run_game_event(GameEngine* game, const char* event_name, const char* event_data) {
    // Set event variables in script context
    char script[512];
    snprintf(script, sizeof(script), 
        "var event_name = \"%s\";\n"
        "var event_data = \"%s\";\n", 
        event_name, event_data ? event_data : "");
    mobius_exec_string(game->script_state, script);
    
    // Call event handler if it exists
    snprintf(script, sizeof(script), 
        "if (typeof(on_%s) == \"function\") {\n"
        "    on_%s(event_data);\n"
        "}", event_name, event_name);
    
    mobius_exec_string(game->script_state, script);
}
```

#### 4. Custom Function Implementation

```c
int game_get_player_health(MobiusState* state, MobiusValue** args, 
                          size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    
    if (!g_game) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Game not initialized");
    }
    
    *result = mobius_create_integer(state, g_game->player.health);
    return MOBIUS_OK;
}
```

### Use Cases

This pattern is excellent for:

- **Game Engines**: Script game logic, AI, events
- **Application Configuration**: Load settings via scripts
- **Plugin Systems**: Extensible application functionality
- **Automation Tools**: Script complex operations
- **Interactive Applications**: User-customizable behavior

---

## Text Processing Plugin

**File:** `examples/text_processing_plugin.c`

This example shows how to create a custom plugin that extends Mobius with specialized functionality.

### Features Demonstrated

- **Plugin Structure**: Complete plugin implementation
- **Function Categories**: Organized function groups
- **String Manipulation**: Advanced C string operations
- **Memory Management**: Safe allocation and cleanup
- **Error Handling**: Comprehensive validation
- **Documentation**: Function descriptions and examples

### Plugin Architecture

```
┌─────────────────────────────────────────────────────────┐
│                 Text Processing Plugin                  │
├─────────────────────────────────────────────────────────┤
│  Plugin Metadata                                        │
│  - Name: "text_processing"                              │
│  - Version: "1.0.0"                                     │
│  - Description: "Advanced Text Processing Functions"    │
│  - Functions: 9                                         │
├─────────────────────────────────────────────────────────┤
│  Function Categories                                     │
│                                                         │
│  📊 Text Analysis          🔧 String Manipulation       │
│  - word_count()           - reverse()                   │
│  - line_count()           - title_case()                │
│  - char_count()           - trim()                      │
│                           - replace_all()               │
│                                                         │
│  📝 Text Formatting                                     │
│  - pad_left()                                           │
│  - split()                                              │
└─────────────────────────────────────────────────────────┘
```

### Available Functions

#### Text Analysis Functions

| Function | Parameters | Returns | Description | Example |
|----------|------------|---------|-------------|---------|
| `word_count(text)` | string | integer | Count words in text | `word_count("hello world")` → `2` |
| `line_count(text)` | string | integer | Count lines in text | `line_count("line1\nline2")` → `2` |
| `char_count(text, char)` | string, string | integer | Count character occurrences | `char_count("hello", "l")` → `2` |

#### String Manipulation Functions

| Function | Parameters | Returns | Description | Example |
|----------|------------|---------|-------------|---------|
| `reverse(text)` | string | string | Reverse a string | `reverse("hello")` → `"olleh"` |
| `title_case(text)` | string | string | Convert to title case | `title_case("hello world")` → `"Hello World"` |
| `trim(text)` | string | string | Remove leading/trailing whitespace | `trim(" hello ")` → `"hello"` |
| `replace_all(text, old, new)` | string, string, string | string | Replace all occurrences | `replace_all("hello", "l", "x")` → `"hexxo"` |

#### Text Formatting Functions

| Function | Parameters | Returns | Description | Example |
|----------|------------|---------|-------------|---------|
| `pad_left(text, width, char)` | string, integer, string | string | Pad string to width on left | `pad_left("hi", 5, "0")` → `"000hi"` |
| `split(text, delimiter)` | string, string | string | Split string by delimiter | `split("a,b,c", ",")` → `"a \| b \| c"` |

### Plugin Implementation Details

#### 1. Plugin Structure

```c
static Plugin text_processing_plugin = {
    .metadata = {
        .name = "text_processing",
        .version = "1.0.0",
        .description = "Advanced Text Processing Functions",
        .author = "Mobius Examples",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = text_processing_functions,
    .function_count = sizeof(text_processing_functions) / sizeof(text_processing_functions[0]),
    .init_plugin = init_text_processing_plugin,
    .cleanup_plugin = cleanup_text_processing_plugin,
    .get_help = get_text_processing_help,
    .validate_env = validate_text_processing_env
};
```

#### 2. Function Implementation Pattern

```c
EvalResult text_word_count(Value* args, size_t arg_count) {
    // 1. Validate arguments
    if (arg_count != 1) {
        return make_error("word_count() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_STRING) {
        return make_error("word_count() expects a string argument", 0, 0);
    }
    
    // 2. Extract input
    const char* text = args[0].as.string;
    
    // 3. Perform operation
    int word_count = 0;
    int in_word = 0;
    
    while (*text) {
        if (isspace(*text)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            word_count++;
        }
        text++;
    }
    
    // 4. Return result
    return make_success(make_integer_value(NUM_INT32, word_count));
}
```

#### 3. Memory Management

```c
static char* safe_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

EvalResult text_reverse(Value* args, size_t arg_count) {
    // ... validation ...
    
    char* reversed = safe_strdup(args[0].as.string);
    if (!reversed) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    reverse_string(reversed);
    
    // Mobius will take ownership of the allocated string
    Value result = make_string_value(reversed);
    return make_success(result);
}
```

#### 4. Entry Point

```c
MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &text_processing_plugin;
}
```

### Usage Example

```javascript
// Load the plugin (done automatically or via load_plugin())
print("Text Processing Demo");

var text = "  Hello, wonderful world!  ";

// Analysis
print("Original:", text);
print("Word count:", word_count(text));
print("Character 'l' count:", char_count(text, "l"));

// Manipulation
var trimmed = trim(text);
print("Trimmed:", trimmed);
print("Reversed:", reverse(trimmed));
print("Title case:", title_case(trimmed));

// Formatting
print("Padded:", pad_left("Hi", 10, "*"));
var parts = split("apple,banana,cherry", ",");
print("Split result:", parts);

// Complex operations
var processed = replace_all(title_case(trim(text)), " ", "_");
print("Processed:", processed);
```

Expected output:
```
Text Processing Demo
Original:   Hello, wonderful world!  
Word count: 3
Character 'l' count: 3
Trimmed: Hello, wonderful world!
Reversed: !dlrow lufrednow ,olleH
Title case: Hello, Wonderful World!
Padded: ********Hi
Split result: apple | banana | cherry
Processed: Hello,_Wonderful_World!
```

---

## Building and Running

### Prerequisites

- GCC compiler with C99 support
- Make build system
- Mobius core library (built from source)

### Building the Examples

Update your `Makefile` to include the new examples:

```makefile
# Add to Makefile
GAME_ENGINE_EXAMPLE = $(BIN_DIR)/game_engine
TEXT_PROCESSING_PLUGIN = $(BIN_DIR)/modules/text_processing.so

# Add to examples target
examples: $(EMBEDDING_EXAMPLE) $(SIMPLE_EMBEDDING_EXAMPLE) $(GAME_ENGINE_EXAMPLE)

# Add build rules
$(GAME_ENGINE_EXAMPLE): examples/game_engine.c $(MOBIUS_LIB) | directories
	@echo "🎮 Building game engine example..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

$(TEXT_PROCESSING_PLUGIN): examples/text_processing_plugin.c $(MOBIUS_LIB) | directories
	@echo "📝 Building text processing plugin..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -shared -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)
```

### Building Commands

```bash
# Build everything
make clean && make

# Build just the examples
make examples

# Build specific example
make bin/game_engine

# Build the plugin
make bin/modules/text_processing.so
```

### Running the Examples

#### Game Engine Example

```bash
# Run the game engine simulation
./bin/game_engine
```

Expected output shows a simulated game with events, script execution, and state changes.

#### Text Processing Plugin

```bash
# Load and test the plugin interactively
echo 'print("Testing:", word_count("hello world"));' | ./bin/mobius

# Or create a test script
cat > test_text_plugin.mob << 'EOF'
var text = "  Hello, Beautiful World!  ";
print("Original:", text);
print("Words:", word_count(text));
print("Trimmed:", trim(text));
print("Title case:", title_case(trim(text)));
print("Reversed:", reverse(trim(text)));
EOF

./bin/mobius test_text_plugin.mob
```

---

## Advanced Techniques

### 1. Plugin Development Best Practices

#### Error Handling
```c
EvalResult my_function(Value* args, size_t arg_count) {
    // Always validate arguments first
    if (arg_count != expected_count) {
        return make_error("Function expects N arguments", 0, 0);
    }
    
    // Check types before use
    if (args[0].type != VAL_STRING) {
        return make_error("Argument must be a string", 0, 0);
    }
    
    // Handle edge cases
    const char* input = args[0].as.string;
    if (strlen(input) == 0) {
        return make_error("Input cannot be empty", 0, 0);
    }
    
    // Check memory allocations
    char* result = malloc(size);
    if (!result) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    // ... perform operation ...
    
    return make_success(make_string_value(result));
}
```

#### Memory Management
```c
// Always check malloc returns
char* buffer = malloc(size);
if (!buffer) {
    return make_error("Memory allocation failed", 0, 0);
}

// Use safe string functions
char* safe_copy = safe_strdup(input);
if (!safe_copy) {
    free(buffer); // Clean up already allocated memory
    return make_error("String duplication failed", 0, 0);
}

// Let Mobius manage the final result
return make_success(make_string_value(result));
// Don't free 'result' - Mobius takes ownership
```

### 2. Embedding Patterns

#### Context Passing
```c
// Option 1: Global state (simple but not thread-safe)
static MyAppState* g_app_state = NULL;

// Option 2: Thread-local storage
__thread MyAppState* thread_app_state = NULL;

// Option 3: Store context in Mobius variables
int init_app_context(MobiusState* state, MyAppState* app) {
    // Store as an opaque string (pointer as string)
    char ptr_str[32];
    snprintf(ptr_str, sizeof(ptr_str), "%p", (void*)app);
    
    MobiusValue* context = mobius_create_string(state, ptr_str);
    mobius_set_global(state, "__app_context", context);
    mobius_free_value(context);
    
    return MOBIUS_OK;
}

// Retrieve in custom functions
MyAppState* get_app_context(MobiusState* state) {
    MobiusValue* context = mobius_get_global(state, "__app_context");
    if (!context || !mobius_is_string(context)) {
        return NULL;
    }
    
    void* ptr;
    sscanf(mobius_to_string(context), "%p", &ptr);
    mobius_free_value(context);
    
    return (MyAppState*)ptr;
}
```

#### Multi-Instance Management
```c
typedef struct {
    MobiusState* ui_scripts;
    MobiusState* game_scripts;
    MobiusState* config_scripts;
} AppScriptingSystem;

void init_scripting_system(AppScriptingSystem* system) {
    // Create separate instances for different purposes
    system->ui_scripts = mobius_new_state();
    system->game_scripts = mobius_new_state();
    system->config_scripts = mobius_new_state();
    
    // Initialize each with appropriate APIs
    init_ui_api(system->ui_scripts);
    init_game_api(system->game_scripts);
    init_config_api(system->config_scripts);
    
    // Load different plugins for each
    mobius_load_plugin(system->ui_scripts, "ui_plugin.so");
    mobius_load_plugin(system->game_scripts, "game_plugin.so");
    // config_scripts might not need any plugins
}
```

### 3. Security Considerations

#### Sandboxing
```c
MobiusState* create_sandbox(void) {
    MobiusState* sandbox = mobius_new_state();
    mobius_init_core(sandbox);
    
    // Don't load potentially dangerous plugins
    // Only register safe functions
    mobius_register_function(sandbox, "safe_log", safe_log_function, 1, "Safe logging");
    mobius_register_function(sandbox, "safe_math", safe_math_function, 2, "Safe math ops");
    
    // Don't register file I/O or system functions
    
    return sandbox;
}
```

#### Input Validation
```c
int validate_user_script(const char* script) {
    // Check for dangerous patterns
    if (strstr(script, "system(") || 
        strstr(script, "exec(") || 
        strstr(script, "__") || 
        strlen(script) > MAX_SCRIPT_SIZE) {
        return 0; // Reject
    }
    
    return 1; // Accept
}
```

### 4. Performance Optimization

#### Script Caching
```c
typedef struct {
    char* script_content;
    TokenArray cached_tokens;
    ParseResult cached_ast;
    time_t last_modified;
} CachedScript;

CachedScript* load_and_cache_script(const char* filename) {
    // Check if file has changed
    struct stat file_stat;
    if (stat(filename, &file_stat) != 0) return NULL;
    
    CachedScript* cached = find_cached_script(filename);
    if (cached && cached->last_modified >= file_stat.st_mtime) {
        return cached; // Use cached version
    }
    
    // Load and parse script
    // Cache the parsed AST for reuse
    // ...
}
```

#### Batch Operations
```c
void run_batch_operations(MobiusState* state) {
    // Prepare all data first
    for (int i = 0; i < operation_count; i++) {
        prepare_operation_data(i);
    }
    
    // Execute operations in batch
    const char* batch_script = 
        "for (var i = 0; i < operation_count; i = i + 1) {\n"
        "    process_operation(i);\n"
        "}\n";
    
    mobius_exec_string(state, batch_script);
}
```

---

## Conclusion

These examples demonstrate the power and flexibility of Mobius for both embedding in applications and extending with custom plugins. The game engine example shows real-world application integration, while the text processing plugin demonstrates how to create reusable extensions.

Key takeaways:

1. **Embedding**: Mobius can be seamlessly integrated into C applications
2. **Extensibility**: Custom plugins can add domain-specific functionality
3. **Safety**: Proper error handling and validation are essential
4. **Performance**: Consider caching and batching for high-performance scenarios
5. **Security**: Implement appropriate sandboxing for untrusted scripts

For more advanced topics, see the main documentation and API reference.
