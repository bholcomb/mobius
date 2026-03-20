/*
 * Text Processing Plugin for Mobius
 * 
 * This example demonstrates how to create a custom plugin that extends
 * Mobius with specialized functionality - in this case, advanced text
 * processing operations.
 * 
 * Features demonstrated:
 * - Plugin structure and metadata
 * - Custom function implementations
 * - String manipulation in C
 * - Error handling in plugins
 * - Plugin initialization and cleanup
 * - Documentation and categorization
 * 
 * Build: gcc -shared -fPIC -o text_processing.so text_processing_plugin.c -I../src
 * Usage: Load this plugin in Mobius with load_plugin("text_processing.so")
 */

#include "../src/mobius/plugin/plugin.h"
#include "../src/mobius/frontend/ast.h"
#include "../src/mobius/eval/evaluator.h"
#include "../src/mobius/state/mobius_state.h"
#include <mobius/mobius_plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Helper function to safely duplicate a string
 */
static char* safe_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

/**
 * Helper function to count character occurrences
 */
static int count_char(const char* str, char ch) {
    int count = 0;
    while (*str) {
        if (*str == ch) count++;
        str++;
    }
    return count;
}

/**
 * Helper function to reverse a string in place
 */
static void reverse_string(char* str) {
    if (!str) return;
    int len = strlen(str);
    for (int i = 0; i < len / 2; i++) {
        char temp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = temp;
    }
}

// ============================================================================
// TEXT ANALYSIS FUNCTIONS
// ============================================================================

/**
 * Count words in a string
 * word_count(text) -> integer
 */
int text_word_count(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("word_count() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isString(state, -1)) {
        return state->error("word_count() expects a string argument");
    }
    
    const char* text = mobius_stack_asString(state, -1);
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
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt32(state, word_count);
    return 1;
}

/**
 * Count lines in a string
 * line_count(text) -> integer
 */
int text_line_count(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("line_count() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isString(state, -1)) {
        return state->error("line_count() expects a string argument");
    }
    
    const char* text = mobius_stack_asString(state, -1);
    int line_count = 1; // At least one line if string is not empty
    
    if (strlen(text) == 0) {
        line_count = 0;
    } else {
        while (*text) {
            if (*text == '\n') {
                line_count++;
            }
            text++;
        }
    }
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt32(state, line_count);
    return 1;
}

/**
 * Count occurrences of a character
 * char_count(text, character) -> integer
 */
int text_char_count(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("char_count() expects exactly 2 arguments");
    }
    
    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2)) {
        return state->error("char_count() expects string arguments");
    }
    
    const char* char_str = mobius_stack_asString(state, -1);
    const char* text = mobius_stack_asString(state, -2);
    
    if (strlen(char_str) != 1) {
        return state->error("char_count() second argument must be a single character");
    }
    
    char target = char_str[0];
    int count = count_char(text, target);
    
    mobius_stack_pop(state, 2);
    mobius_stack_pushInt32(state, count);
    return 1;
}

// ============================================================================
// STRING MANIPULATION FUNCTIONS
// ============================================================================

/**
 * Reverse a string
 * reverse(text) -> string
 */
int text_reverse(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("reverse() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isString(state, -1)) {
        return state->error("reverse() expects a string argument");
    }

    const char* text = mobius_stack_asString(state, -1);
    char* reversed = safe_strdup(text);
    if (!reversed) {
        return state->error("Memory allocation failed");
    }
    
    reverse_string(reversed);
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, reversed);
    free(reversed);
    return 1;
}

/**
 * Convert to title case
 * title_case(text) -> string
 */
int text_title_case(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("title_case() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isString(state, -1)) {
        return state->error("title_case() expects a string argument");
    }
    
    const char* input = mobius_stack_asString(state, -1);
    char* result = safe_strdup(input);
    if (!result) {
        return state->error("Memory allocation failed");
    }
    
    int capitalize_next = 1;
    for (char* p = result; *p; p++) {
        if (isalpha(*p)) {
            if (capitalize_next) {
                *p = toupper(*p);
                capitalize_next = 0;
            } else {
                *p = tolower(*p);
            }
        } else if (isspace(*p)) {
            capitalize_next = 1;
        }
    }
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, result);
    free(result);
    return 1;
}

/**
 * Remove whitespace from both ends
 * trim(text) -> string
 */
int text_trim(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("trim() expects exactly 1 argument");
    }

    if (!mobius_stack_isString(state, -1)) {
        return state->error("trim() expects a string argument");
    }
    
    const char* input = mobius_stack_asString(state, -1);
    
    // Find start of non-whitespace
    while (*input && isspace(*input)) {
        input++;
    }
    
    // Find end of non-whitespace
    const char* end = input + strlen(input) - 1;
    while (end >= input && isspace(*end)) {
        end--;
    }
    
    // Create trimmed string
    size_t len = end - input + 1;
    char* result = malloc(len + 1);
    if (!result) {
        return state->error("Memory allocation failed");
    }
    
    strncpy(result, input, len);
    result[len] = '\0';
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, result);
    free(result);
    return 1;
}

/**
 * Replace all occurrences of a substring
 * replace_all(text, old_substr, new_substr) -> string
 */
int text_replace_all(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("replace_all() expects exactly 3 arguments");
    }

    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2) || !mobius_stack_isString(state, -3)) {
        return state->error("replace_all() expects string arguments");
    }
    
    const char* new_substr = mobius_stack_asString(state, -1);
    const char* old_substr = mobius_stack_asString(state, -2);
    const char* text = mobius_stack_asString(state, -3);
    
    if (strlen(old_substr) == 0) {
        return state->error("replace_all() old substring cannot be empty");
    }
    
    // Count occurrences to determine result size
    int count = 0;
    const char* p = text;
    while ((p = strstr(p, old_substr)) != NULL) {
        count++;
        p += strlen(old_substr);
    }
    
    if (count == 0) {
        // No replacements needed
        mobius_stack_pop(state, 3);
        mobius_stack_pushString(state, text);
        return 1;
    }
    
    // Calculate result size
    size_t old_len = strlen(old_substr);
    size_t new_len = strlen(new_substr);
    size_t result_len = strlen(text) + count * (new_len - old_len);
    
    char* result = malloc(result_len + 1);
    if (!result) {
        return state->error("Memory allocation failed");
    }
    
    // Perform replacements
    char* dest = result;
    p = text;
    while (*p) {
        const char* match = strstr(p, old_substr);
        if (match == p) {
            // Copy new substring
            strcpy(dest, new_substr);
            dest += new_len;
            p += old_len;
        } else {
            // Copy one character
            *dest++ = *p++;
        }
    }
    *dest = '\0';
    
    mobius_stack_pop(state, 3);
    mobius_stack_pushString(state, result);
    free(result);
    return 1;
}

// ============================================================================
// TEXT FORMATTING FUNCTIONS
// ============================================================================

/**
 * Pad string to specified width with character
 * pad_left(text, width, pad_char) -> string
 */
int text_pad_left(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("pad_left() expects exactly 3 arguments");
    }

    if (!mobius_stack_isString(state, -1) || !mobius_stack_isInteger(state, -2) || !mobius_stack_isString(state, -3)) {
        return state->error("pad_left() expects (string, integer, string) arguments");
    }
    
    const char* pad_char_str = mobius_stack_asString(state, -1);
    int width = mobius_stack_asInt32(state, -2);
    const char* text = mobius_stack_asString(state, -3);
    
    if (strlen(pad_char_str) != 1) {
        return state->error("pad_left() pad character must be a single character");
    }
    
    char pad_char = pad_char_str[0];
    int text_len = strlen(text);
    
    if (width <= text_len) {
        // No padding needed
        mobius_stack_pop(state, 3);
        mobius_stack_pushString(state, text);
        return 1;
    }
    
    char* result = malloc(width + 1);
    if (!result) {
        return state->error("Memory allocation failed");
    }
    
    int pad_count = width - text_len;
    for (int i = 0; i < pad_count; i++) {
        result[i] = pad_char;
    }
    strcpy(result + pad_count, text);
    
    mobius_stack_pop(state, 3);
    mobius_stack_pushString(state, result);
    free(result);
    return 1;
}

/**
 * Split string by delimiter
 * split(text, delimiter) -> string (comma-separated for this example)
 */
int text_split(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("split() expects exactly 2 arguments");
    }

    if (!mobius_stack_isString(state, -1) || !mobius_stack_isString(state, -2)) {
        return state->error("split() expects string arguments");
    }
    
    const char* delimiter = mobius_stack_asString(state, -1);
    const char* text = mobius_stack_asString(state, -2);
    
    if (strlen(delimiter) == 0) {
        return state->error("split() delimiter cannot be empty");
    }
    
    // For simplicity, return parts joined with " | " 
    // In a real implementation, you'd return an array
    char* result = malloc(strlen(text) * 2 + 100); // Rough estimate
    if (!result) {
        return state->error("Memory allocation failed");
    }
    
    result[0] = '\0';
    char* text_copy = safe_strdup(text);
    char* token = strtok(text_copy, delimiter);
    int first = 1;
    
    while (token != NULL) {
        if (!first) {
            strcat(result, " | ");
        }
        strcat(result, token);
        first = 0;
        token = strtok(NULL, delimiter);
    }
    
    free(text_copy);
    mobius_stack_pop(state, 2);
    mobius_stack_pushString(state, result);
    free(result);
    return 1;
}

// ============================================================================
// PLUGIN INITIALIZATION AND METADATA
// ============================================================================

/**
 * Plugin initialization function
 */
int init_text_processing_plugin(void) {
    // Perform any necessary initialization
    // For this example, no special initialization is needed
    return 0; // Success
}

/**
 * Plugin cleanup function
 */
void cleanup_text_processing_plugin(void) {
    // Perform any necessary cleanup
    // For this example, no special cleanup is needed
}

/**
 * Plugin help function
 */
const char* get_text_processing_help(const char* function_name) {
    (void)function_name; // Suppress unused parameter warning
    return "Text Processing Plugin provides advanced string manipulation functions:\n"
           "- word_count(text): Count words in text\n"
           "- line_count(text): Count lines in text\n"
           "- char_count(text, char): Count character occurrences\n"
           "- reverse(text): Reverse string\n"
           "- title_case(text): Convert to title case\n"
           "- trim(text): Remove leading/trailing whitespace\n"
           "- replace_all(text, old, new): Replace all occurrences\n"
           "- pad_left(text, width, char): Pad string to width\n"
           "- split(text, delimiter): Split string by delimiter\n";
}

/**
 * Environment validation function
 */
int validate_text_processing_env(void) {
    // Check if the environment is suitable for this plugin
    // For this example, always return success
    return 1; // Environment is valid
}

// ============================================================================
// PLUGIN FUNCTION DEFINITIONS
// ============================================================================

static PluginFunction text_processing_functions[] = {
    // Text analysis functions
    {"word_count", text_word_count, 1},
    {"line_count", text_line_count, 1},
    {"char_count", text_char_count, 2},
    
    // String manipulation functions
    {"reverse", text_reverse, 1},
    {"title_case", text_title_case, 1},
    {"trim", text_trim, 1},
    {"replace_all", text_replace_all, 3},
    
    // Text formatting functions
    {"pad_left", text_pad_left, 3},
    {"split", text_split, 2}
};

// ============================================================================
// PLUGIN INSTANCE
// ============================================================================

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

// ============================================================================
// PLUGIN ENTRY POINT
// ============================================================================

/**
 * Required plugin entry point
 * This function must be exported and named exactly "mobius_plugin_info"
 */
MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &text_processing_plugin;
}
