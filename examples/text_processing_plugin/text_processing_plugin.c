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

#include "../src/mobius/plugin.h"
#include "../src/mobius/ast.h"
#include "../src/mobius/evaluator.h"
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
EvalResult text_word_count(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("word_count() expects exactly 1 argument", 0, 0);
    }
    
    Value text_val = env_pop(env);
    if (text_val.type != VAL_STRING) {
        free_value(text_val);
        return make_error("word_count() expects a string argument", 0, 0);
    }
    
    const char* text = string_data(text_val.as.string);
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
    
    env_push(env, make_integer_value(NUM_INT32, word_count));
    free_value(text_val);
    return make_success(1);
}

/**
 * Count lines in a string
 * line_count(text) -> integer
 */
EvalResult text_line_count(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("line_count() expects exactly 1 argument", 0, 0);
    }
    
    Value text_val = env_pop(env);
    if (text_val.type != VAL_STRING) {
        free_value(text_val);
        return make_error("line_count() expects a string argument", 0, 0);
    }
    
    const char* text = string_data(text_val.as.string);
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
    
    env_push(env, make_integer_value(NUM_INT32, line_count));
    return make_success(1);
}

/**
 * Count occurrences of a character
 * char_count(text, character) -> integer
 */
EvalResult text_char_count(Environment* env, int arg_count) {
    if (arg_count != 2) {
        return make_error("char_count() expects exactly 2 arguments", 0, 0);
    }
    
    Value char_val = env_pop(env);
    Value text_val = env_pop(env);

    if (text_val.type != VAL_STRING || char_val.type != VAL_STRING) {
        return make_error("char_count() expects string arguments", 0, 0);
    }
    
    const char* text = string_data(text_val.as.string);
    const char* char_str = string_data(char_val.as.string);
    
    if (strlen(char_str) != 1) {
        return make_error("char_count() second argument must be a single character", 0, 0);
    }
    
    char target = char_str[0];
    int count = count_char(text, target);
    
    env_push(env, make_integer_value(NUM_INT32, count));
    return make_success(1);
}

// ============================================================================
// STRING MANIPULATION FUNCTIONS
// ============================================================================

/**
 * Reverse a string
 * reverse(text) -> string
 */
EvalResult text_reverse(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("reverse() expects exactly 1 argument", 0, 0);
    }
    
    Value text_val = env_pop(env);
    if (text_val.type != VAL_STRING) {
        free_value(text_val);
        return make_error("reverse() expects a string argument", 0, 0);
    }

    char* reversed = safe_strdup(string_data(text_val.as.string));
    if (!reversed) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    reverse_string(reversed);
    
    Value result = make_string_value_from_cstr(reversed);
    free(reversed);
    env_push(env, result);
    return make_success(1);
}

/**
 * Convert to title case
 * title_case(text) -> string
 */
EvalResult text_title_case(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("title_case() expects exactly 1 argument", 0, 0);
    }
    
    Value text_val = env_pop(env);
    if (text_val.type != VAL_STRING) {
        free_value(text_val);
        return make_error("title_case() expects a string argument", 0, 0);
    }
    
    const char* input = string_data(text_val.as.string);
    char* result = safe_strdup(input);
    if (!result) {
        return make_error("Memory allocation failed", 0, 0);
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
    
    Value return_val = make_string_value_from_cstr(result);
    free(result);
    env_push(env, return_val);
    return make_success(1);
}

/**
 * Remove whitespace from both ends
 * trim(text) -> string
 */
EvalResult text_trim(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("trim() expects exactly 1 argument", 0, 0);
    }

    Value text_val = env_pop(env);
    if (text_val.type != VAL_STRING) {
        free_value(text_val);
        return make_error("trim() expects a string argument", 0, 0);
    }
    
    const char* input = string_data(text_val.as.string);
    
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
        return make_error("Memory allocation failed", 0, 0);
    }
    
    strncpy(result, input, len);
    result[len] = '\0';
    
    Value return_val = make_string_value_from_cstr(result);
    env_push(env, return_val);
    free(result);
    env_push(env, return_val);
    return make_success(1);
}

/**
 * Replace all occurrences of a substring
 * replace_all(text, old_substr, new_substr) -> string
 */
EvalResult text_replace_all(Environment* env, int arg_count) {
    if (arg_count != 3) {
        return make_error("replace_all() expects exactly 3 arguments", 0, 0);
    }

    Value new_val = env_pop(env);
    Value old_val = env_pop(env);
    Value text_val = env_pop(env);
    
    if (text_val.type != VAL_STRING || old_val.type != VAL_STRING || new_val.type != VAL_STRING) {
        return make_error("replace_all() expects string arguments", 0, 0);
    }
    
    const char* text = string_data(text_val.as.string);
    const char* old_substr = string_data(old_val.as.string);
    const char* new_substr = string_data(new_val.as.string);
    
    if (strlen(old_substr) == 0) {
        return make_error("replace_all() old substring cannot be empty", 0, 0);
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
        env_push(env, make_string_value_from_cstr(text));
        return make_success(1);
    }
    
    // Calculate result size
    size_t old_len = strlen(old_substr);
    size_t new_len = strlen(new_substr);
    size_t result_len = strlen(text) + count * (new_len - old_len);
    
    char* result = malloc(result_len + 1);
    if (!result) {
        return make_error("Memory allocation failed", 0, 0);
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
    
    Value return_val = make_string_value_from_cstr(result);
    free(result);
    env_push(env, return_val);
    return make_success(1);
}

// ============================================================================
// TEXT FORMATTING FUNCTIONS
// ============================================================================

/**
 * Pad string to specified width with character
 * pad_left(text, width, pad_char) -> string
 */
EvalResult text_pad_left(Environment* env, int arg_count) {
    (void)env; // Unused parameter
    if (arg_count != 3) {
        return make_error("pad_left() expects exactly 3 arguments", 0, 0);
    }

    Value pad_char_val = env_pop(env);
    Value width_val = env_pop(env);
    Value text_val = env_pop(env);
    
    if (text_val.type != VAL_STRING || width_val.type != VAL_INTEGER || pad_char_val.type != VAL_STRING) {
        return make_error("pad_left() expects (string, integer, string) arguments", 0, 0);
    }
    
    const char* text = string_data(text_val.as.string);
    int width = width_val.as.integer.value.i32;
    const char* pad_char_str = string_data(pad_char_val.as.string);
    
    if (strlen(pad_char_str) != 1) {
        return make_error("pad_left() pad character must be a single character", 0, 0);
    }
    
    char pad_char = pad_char_str[0];
    int text_len = strlen(text);
    
    if (width <= text_len) {
        // No padding needed
        env_push(env, make_string_value_from_cstr(text));
        return make_success(1);
    }
    
    char* result = malloc(width + 1);
    if (!result) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    int pad_count = width - text_len;
    for (int i = 0; i < pad_count; i++) {
        result[i] = pad_char;
    }
    strcpy(result + pad_count, text);
    
    Value return_val = make_string_value_from_cstr(result);
    free(result);
    env_push(env, return_val);
    return make_success(1);
}

/**
 * Split string by delimiter
 * split(text, delimiter) -> string (comma-separated for this example)
 */
EvalResult text_split(Environment* env, int arg_count) {
    (void)env; // Unused parameter
    if (arg_count != 2) {
        return make_error("split() expects exactly 2 arguments", 0, 0);
    }

    Value delim_val = env_pop(env);
    Value text_val = env_pop(env);
    
    if (text_val.type != VAL_STRING || delim_val.type != VAL_STRING) {
        return make_error("split() expects string arguments", 0, 0);
    }
    
    const char* text = string_data(text_val.as.string);
    const char* delimiter = string_data(delim_val.as.string);
    
    if (strlen(delimiter) == 0) {
        return make_error("split() delimiter cannot be empty", 0, 0);
    }
    
    // For simplicity, return parts joined with " | " 
    // In a real implementation, you'd return an array
    char* result = malloc(strlen(text) * 2 + 100); // Rough estimate
    if (!result) {
        return make_error("Memory allocation failed", 0, 0);
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
    Value return_val = make_string_value_from_cstr(result);
    free(result);
    env_push(env, return_val);
    return make_success(1);
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
    {"word_count", text_word_count, 1, "Count words in text", "analysis", "word_count(\"hello world\")"},
    {"line_count", text_line_count, 1, "Count lines in text", "analysis", "line_count(\"line1\\nline2\")"},
    {"char_count", text_char_count, 2, "Count character occurrences", "analysis", "char_count(\"hello\", \"l\")"},
    
    // String manipulation functions
    {"reverse", text_reverse, 1, "Reverse a string", "manipulation", "reverse(\"hello\")"},
    {"title_case", text_title_case, 1, "Convert to title case", "manipulation", "title_case(\"hello world\")"},
    {"trim", text_trim, 1, "Remove leading/trailing whitespace", "manipulation", "trim(\" hello \")"},
    {"replace_all", text_replace_all, 3, "Replace all occurrences", "manipulation", "replace_all(\"hello\", \"l\", \"x\")"},
    
    // Text formatting functions
    {"pad_left", text_pad_left, 3, "Pad string to width on left", "formatting", "pad_left(\"hi\", 5, \"0\")"},
    {"split", text_split, 2, "Split string by delimiter", "formatting", "split(\"a,b,c\", \",\")"}
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
