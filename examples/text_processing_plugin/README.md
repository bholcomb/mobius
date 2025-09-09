# Text Processing Plugin Example

This example shows how to create a custom plugin that extends Mobius with specialized functionality.

## Files

- `text_processing_plugin.c` - Complete plugin implementation with 9 text processing functions

## Features Demonstrated

- **Plugin Structure**: Complete plugin implementation
- **Function Categories**: Organized function groups
- **String Manipulation**: Advanced C string operations
- **Memory Management**: Safe allocation and cleanup
- **Error Handling**: Comprehensive validation
- **Documentation**: Function descriptions and examples

## Plugin Architecture

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

## Available Functions

### Text Analysis Functions

| Function | Parameters | Returns | Description | Example |
|----------|------------|---------|-------------|---------|
| `word_count(text)` | string | integer | Count words in text | `word_count("hello world")` → `2` |
| `line_count(text)` | string | integer | Count lines in text | `line_count("line1\nline2")` → `2` |
| `char_count(text, char)` | string, string | integer | Count character occurrences | `char_count("hello", "l")` → `2` |

### String Manipulation Functions

| Function | Parameters | Returns | Description | Example |
|----------|------------|---------|-------------|---------|
| `reverse(text)` | string | string | Reverse a string | `reverse("hello")` → `"olleh"` |
| `title_case(text)` | string | string | Convert to title case | `title_case("hello world")` → `"Hello World"` |
| `trim(text)` | string | string | Remove leading/trailing whitespace | `trim(" hello ")` → `"hello"` |
| `replace_all(text, old, new)` | string, string, string | string | Replace all occurrences | `replace_all("hello", "l", "x")` → `"hexxo"` |

### Text Formatting Functions

| Function | Parameters | Returns | Description | Example |
|----------|------------|---------|-------------|---------|
| `pad_left(text, width, char)` | string, integer, string | string | Pad string to width on left | `pad_left("hi", 5, "0")` → `"000hi"` |
| `split(text, delimiter)` | string, string | string | Split string by delimiter | `split("a,b,c", ",")` → `"a | b | c"` |

## Plugin Implementation Details

### 1. Plugin Structure

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

### 2. Function Implementation Pattern

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

### 3. Memory Management

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

### 4. Entry Point

```c
MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &text_processing_plugin;
}
```

## Usage Example

```javascript
// Load the plugin (done automatically or via load_plugin())
print("Text Processing Demo");

var text = "  Hello, wonderful world!  ";

// Analysis
print("Original:", text);
print("Word count:", word_count(text));
print("Line count:", line_count(text));
print("'l' count:", char_count(text, "l"));

// Manipulation  
print("Reversed:", reverse(text));
print("Title case:", title_case(text));
print("Trimmed:", trim(text));
print("Replace 'l' with 'X':", replace_all(text, "l", "X"));

// Formatting
print("Padded:", pad_left("42", 6, "0"));
print("Split:", split("apple,banana,cherry", ","));
```

**Expected Output:**
```
Text Processing Demo
Original:   Hello, wonderful world!  
Word count: 3
Line count: 1
'l' count: 3
Reversed: !dlrow lufrednow ,olleH  
Title case: Hello, Wonderful World!
Trimmed: Hello, wonderful world!
Replace 'l' with 'X': HeXXo, wonderfuX worXd!
Padded: 000042
Split: apple | banana | cherry
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
# Load plugin and test in REPL
LD_LIBRARY_PATH=./bin/modules ./bin/mobius

# Or test directly
echo 'print("Words:", word_count("hello beautiful world"));' | LD_LIBRARY_PATH=./bin/modules ./bin/mobius
```

## Development Tips

### Creating Your Own Plugin

1. **Copy the template**: Use `text_processing_plugin.c` as a starting point
2. **Define your functions**: Create functions with the `EvalResult func_name(Value* args, size_t arg_count)` signature
3. **Register functions**: Add them to the function array
4. **Update metadata**: Change plugin name, version, description
5. **Build and test**: Compile as shared library and load in Mobius

### Best Practices

- **Always validate arguments**: Check count and types
- **Handle memory carefully**: Use safe allocation and cleanup
- **Provide good error messages**: Help users understand what went wrong
- **Document thoroughly**: Include examples and parameter descriptions
- **Test extensively**: Try edge cases and invalid inputs

This plugin demonstrates the full capabilities of the Mobius plugin system and serves as an excellent template for creating domain-specific extensions.
