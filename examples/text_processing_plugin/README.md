# Text Processing Plugin Example

This example shows how to create a plugin that extends Mobius with additional
native functions.

## Files

- `text_processing_plugin.cpp` - Plugin implementation with 9 text processing
  functions

## Header Used

```cpp
#include <mobius/mobius_plugin.h>   // stack API, plugin structs, export macros
```

## Plugin Architecture

```
┌─────────────────────────────────────────────────────────┐
│                 Text Processing Plugin                  │
├─────────────────────────────────────────────────────────┤
│  Metadata                                               │
│  - Name: "text_processing"                              │
│  - Version: "1.0.0"                                     │
│  - Functions: 9                                         │
├─────────────────────────────────────────────────────────┤
│  Text Analysis          String Manipulation             │
│  - word_count()         - reverse()                     │
│  - line_count()         - title_case()                  │
│  - char_count()         - trim()                        │
│                         - replace_all()                 │
│                                                         │
│  Text Formatting                                        │
│  - pad_left()                                           │
│  - split()                                              │
└─────────────────────────────────────────────────────────┘
```

## Available Functions

| Function | Parameters | Returns | Description |
|----------|------------|---------|-------------|
| `word_count(text)` | string | integer | Count words |
| `line_count(text)` | string | integer | Count lines |
| `char_count(text, ch)` | string, string | integer | Count character occurrences |
| `reverse(text)` | string | string | Reverse a string |
| `title_case(text)` | string | string | Convert to title case |
| `trim(text)` | string | string | Strip leading/trailing whitespace |
| `replace_all(text, old, new)` | 3 strings | string | Replace all occurrences |
| `pad_left(text, width, ch)` | string, int, string | string | Left-pad to width |
| `split(text, delim)` | string, string | string | Split by delimiter |

## Plugin Function Pattern

Every plugin function uses the public stack API and `mobius_error`:

```cpp
int text_word_count(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "word_count() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "word_count() expects a string");

    const char* text = mobius_stack_asString(state, -1);
    int words = 0, in_word = 0;
    while (*text) {
        if (isspace(*text)) in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
        text++;
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, words);
    return 1;
}
```

## Plugin Registration

```cpp
static MobiusPluginFunction functions[] = {
    {"word_count", text_word_count, 1, "Count words in a string"},
    {"to_upper",   text_upper,     1, "Convert string to uppercase"},
};

static MobiusPlugin plugin = {
    .metadata = {
        .name        = "text_processing",
        .version     = "1.0.0",
        .description = "Advanced Text Processing Functions",
        .author      = "Mobius Examples",
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

## Building

```bash
g++ -shared -fPIC -o text_processing.so text_processing_plugin.cpp \
    -I/path/to/mobius/include
```

## Usage in Scripts

```mobius
import "text_processing"

print(text_processing.word_count("The quick brown fox"))   // 4
print(text_processing.reverse("hello"))                    // "olleh"
print(text_processing.trim("  hello  "))                   // "hello"
```
