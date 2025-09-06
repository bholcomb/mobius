#include "stdlib.h"
#include "ast.h"
#include "table.h"
#include "file_io.h"
#include "scanner.h"
#include "parser.h"
#include "evaluator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

// =============================================================================
// CORE FUNCTIONS
// =============================================================================

EvalResult builtin_print(Environment* env, Value* args, size_t arg_count) {
    (void)env; // Print doesn't need environment access
    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) printf(" ");
        print_value(args[i]);
    }
    printf("\n");
    return make_success(make_nil_value());
}

EvalResult builtin_typeof(Environment* env, Value* args, size_t arg_count) {
    (void)env; // typeof doesn't need environment access
    if (arg_count != 1) {
        return make_error_detailed("typeof() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    const char* type_name = value_type_name(args[0].type);
    // Create ref-counted string from type name
    Value result = make_string_value_from_cstr(type_name);
    return make_success(result);
}

EvalResult builtin_str(Environment* env, Value* args, size_t arg_count) {
    (void)env; // str() doesn't need environment access
    if (arg_count != 1) {
        return make_error_detailed("str() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    char* temp_str = value_to_string(args[0]);
    Value result = make_string_value_from_cstr(temp_str);
    free(temp_str);  // Clean up temporary string
    return make_success(result);
}

EvalResult builtin_int(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("int() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    switch (arg.type) {
        case VAL_INTEGER:
            return make_success(arg);  // Already an integer
        case VAL_FLOAT:
            return make_success(make_integer_value(NUM_INT32, (int32_t)arg.as.float_val));
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str_data = string_data(arg.as.string);
                char* endptr;
                long val = strtol(str_data, &endptr, 10);
                if (*endptr == '\0') {
                    return make_success(make_integer_value(NUM_INT32, (int32_t)val));
                }
            }
            return make_error_detailed("Cannot convert string to integer", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        case VAL_BOOL:
            return make_success(make_integer_value(NUM_INT32, arg.as.boolean ? 1 : 0));
        default:
            return make_error_detailed("Cannot convert value to integer", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
}

EvalResult builtin_float(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("float() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    switch (arg.type) {
        case VAL_FLOAT:
            return make_success(arg);  // Already a float
        case VAL_INTEGER: {
            // Convert integer to float based on its numeric type
            double val = 0.0;
            switch (arg.as.integer.num_type) {
                case NUM_INT8:   val = arg.as.integer.value.i8; break;
                case NUM_UINT8:  val = arg.as.integer.value.u8; break;
                case NUM_INT16:  val = arg.as.integer.value.i16; break;
                case NUM_UINT16: val = arg.as.integer.value.u16; break;
                case NUM_INT32:  val = arg.as.integer.value.i32; break;
                case NUM_UINT32: val = arg.as.integer.value.u32; break;
                case NUM_INT64:  val = arg.as.integer.value.i64; break;
                case NUM_UINT64: val = arg.as.integer.value.u64; break;
                case NUM_FLOAT32: val = 0.0; break; // Shouldn't happen
                case NUM_FLOAT64: val = 0.0; break; // Shouldn't happen
            }
            return make_success(make_float_value(val));
        }
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str_data = string_data(arg.as.string);
                char* endptr;
                double val = strtod(str_data, &endptr);
                if (*endptr == '\0') {
                    return make_success(make_float_value(val));
                }
            }
            return make_error_detailed("Cannot convert string to float", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        default:
            return make_error_detailed("Cannot convert value to float", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
}

// =============================================================================
// MATH FUNCTIONS
// =============================================================================

EvalResult builtin_abs(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("abs() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    if (arg.type == VAL_INTEGER) {
        int64_t val = arg.as.integer.value.i64;  // Simplified
        if (val < 0) val = -val;
        return make_success(make_integer_value(NUM_INT64, val));
    } else if (arg.type == VAL_FLOAT) {
        return make_success(make_float_value(fabs(arg.as.float_val)));
    }
    
    return make_error_detailed("abs() requires a numeric argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
}

EvalResult builtin_min(Environment* env, Value* args, size_t arg_count) {
    if (arg_count < 2) {
        return make_error_detailed("min() expects at least 2 arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value min_val = args[0];
    for (size_t i = 1; i < arg_count; i++) {
        Value current = args[i];
        
        // Convert to same type for comparison
        if (min_val.type == VAL_FLOAT || current.type == VAL_FLOAT) {
            double min_d = (min_val.type == VAL_FLOAT) ? min_val.as.float_val : min_val.as.integer.value.i32;
            double cur_d = (current.type == VAL_FLOAT) ? current.as.float_val : current.as.integer.value.i32;
            if (cur_d < min_d) {
                min_val = current;
            }
        } else if (min_val.type == VAL_INTEGER && current.type == VAL_INTEGER) {
            if (current.as.integer.value.i32 < min_val.as.integer.value.i32) {
                min_val = current;
            }
        }
    }
    
    return make_success(min_val);
}

EvalResult builtin_max(Environment* env, Value* args, size_t arg_count) {
    if (arg_count < 2) {
        return make_error_detailed("max() expects at least 2 arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value max_val = args[0];
    for (size_t i = 1; i < arg_count; i++) {
        Value current = args[i];
        
        // Convert to same type for comparison
        if (max_val.type == VAL_FLOAT || current.type == VAL_FLOAT) {
            double max_d = (max_val.type == VAL_FLOAT) ? max_val.as.float_val : max_val.as.integer.value.i32;
            double cur_d = (current.type == VAL_FLOAT) ? current.as.float_val : current.as.integer.value.i32;
            if (cur_d > max_d) {
                max_val = current;
            }
        } else if (max_val.type == VAL_INTEGER && current.type == VAL_INTEGER) {
            if (current.as.integer.value.i32 > max_val.as.integer.value.i32) {
                max_val = current;
            }
        }
    }
    
    return make_success(max_val);
}

EvalResult builtin_pow(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error_detailed("pow() expects exactly 2 arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value base = args[0];
    Value exp = args[1];
    
    double base_d = (base.type == VAL_FLOAT) ? base.as.float_val : 
                   (base.type == VAL_INTEGER) ? base.as.integer.value.i32 : 0.0;
    double exp_d = (exp.type == VAL_FLOAT) ? exp.as.float_val : 
                  (exp.type == VAL_INTEGER) ? exp.as.integer.value.i32 : 0.0;
    
    if (base.type != VAL_FLOAT && base.type != VAL_INTEGER) {
        return make_error_detailed("pow() base must be numeric", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    if (exp.type != VAL_FLOAT && exp.type != VAL_INTEGER) {
        return make_error_detailed("pow() exponent must be numeric", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    return make_success(make_float_value(pow(base_d, exp_d)));
}

EvalResult builtin_sqrt(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("sqrt() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    double val;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        val = arg.as.integer.value.i32;
    } else {
        return make_error_detailed("sqrt() requires a numeric argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    if (val < 0) {
        return make_error_detailed("sqrt() of negative number", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
    }
    
    return make_success(make_float_value(sqrt(val)));
}

EvalResult builtin_floor(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("floor() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    double val;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        return make_success(arg);  // Already an integer
    } else {
        return make_error_detailed("floor() requires a numeric argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    return make_success(make_integer_value(NUM_INT32, (int32_t)floor(val)));
}

EvalResult builtin_ceil(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("ceil() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    double val;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        return make_success(arg);  // Already an integer
    } else {
        return make_error_detailed("ceil() requires a numeric argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    return make_success(make_integer_value(NUM_INT32, (int32_t)ceil(val)));
}

EvalResult builtin_round(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("round() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    double val;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        return make_success(arg);  // Already an integer
    } else {
        return make_error_detailed("round() requires a numeric argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    return make_success(make_integer_value(NUM_INT32, (int32_t)round(val)));
}

// =============================================================================
// STRING FUNCTIONS
// =============================================================================

EvalResult builtin_len(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("len() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    if (arg.type == VAL_STRING) {
        size_t length = arg.as.string ? string_length(arg.as.string) : 0;
        return make_success(make_integer_value(NUM_INT32, (int32_t)length));
    }
    
    return make_error_detailed("len() requires a string argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
}

EvalResult builtin_substr(Environment* env, Value* args, size_t arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return make_error_detailed("substr() expects 2 or 3 arguments: substr(string, start [, length])", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value str_val = args[0];
    Value start_val = args[1];
    
    if (str_val.type != VAL_STRING) {
        return make_error_detailed("substr() first argument must be a string", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    if (start_val.type != VAL_INTEGER) {
        return make_error_detailed("substr() start index must be an integer", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    const char* str = str_val.as.string ? string_data(str_val.as.string) : "";
    size_t str_len = str_val.as.string ? string_length(str_val.as.string) : 0;
    int32_t start = start_val.as.integer.value.i32;
    size_t length = str_len;
    
    // Handle negative start index
    if (start < 0) start = 0;
    if ((size_t)start >= str_len) {
        return make_success(make_string_value_from_cstr(""));
    }
    
    // Handle length parameter
    if (arg_count == 3) {
        Value len_val = args[2];
        if (len_val.type != VAL_INTEGER) {
            return make_error_detailed("substr() length must be an integer", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        int32_t len = len_val.as.integer.value.i32;
        if (len < 0) len = 0;
        length = (size_t)len;
    }
    
    // Calculate actual substring length
    if ((size_t)start + length > str_len) {
        length = str_len - start;
    }
    
    char* temp_result = malloc(length + 1);
    if (!temp_result) {
        return make_error_detailed("Memory allocation failed", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
    }
    
    strncpy(temp_result, str + start, length);
    temp_result[length] = '\0';
    
    // Create ref-counted string and clean up temp
    Value result = make_string_value_from_cstr(temp_result);
    free(temp_result);
    
    return make_success(result);
}

EvalResult builtin_concat(Environment* env, Value* args, size_t arg_count) {
    if (arg_count < 2) {
        return make_error_detailed("concat() expects at least 2 arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    // Calculate total length needed
    size_t total_len = 0;
    for (size_t i = 0; i < arg_count; i++) {
        if (args[i].type != VAL_STRING) {
            return make_error_detailed("concat() all arguments must be strings", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        if (args[i].as.string) {
            total_len += string_length(args[i].as.string);
        }
    }
    
    char* result = malloc(total_len + 1);
    if (!result) {
        return make_error_detailed("Memory allocation failed", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
    }
    
    result[0] = '\0';
    for (size_t i = 0; i < arg_count; i++) {
        if (args[i].as.string) {
            const char* str_data = string_data(args[i].as.string);
            strcat(result, str_data);
        }
    }
    
    // Create ref-counted string and clean up temp
    Value final_result = make_string_value_from_cstr(result);
    free(result);
    
    return make_success(final_result);
}

EvalResult builtin_upper(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("upper() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    if (arg.type != VAL_STRING) {
        return make_error_detailed("upper() requires a string argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    const char* str = arg.as.string ? string_data(arg.as.string) : "";
    size_t len = arg.as.string ? string_length(arg.as.string) : 0;
    char* result = malloc(len + 1);
    if (!result) {
        return make_error_detailed("Memory allocation failed", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
    }
    
    for (size_t i = 0; i < len; i++) {
        result[i] = toupper(str[i]);
    }
    result[len] = '\0';
    
    // Create ref-counted string and clean up temp
    Value final_result = make_string_value_from_cstr(result);
    free(result);
    
    return make_success(final_result);
}

EvalResult builtin_lower(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("lower() expects exactly 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value arg = args[0];
    if (arg.type != VAL_STRING) {
        return make_error_detailed("lower() requires a string argument", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    const char* str = arg.as.string ? string_data(arg.as.string) : "";
    size_t len = arg.as.string ? string_length(arg.as.string) : 0;
    char* result = malloc(len + 1);
    if (!result) {
        return make_error_detailed("Memory allocation failed", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
    }
    
    for (size_t i = 0; i < len; i++) {
        result[i] = tolower(str[i]);
    }
    result[len] = '\0';
    
    // Create ref-counted string and clean up temp
    Value final_result = make_string_value_from_cstr(result);
    free(result);
    
    return make_success(final_result);
}

EvalResult builtin_contains(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error_detailed("contains() expects exactly 2 arguments: contains(string, substring)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value str_val = args[0];
    Value substr_val = args[1];
    
    if (str_val.type != VAL_STRING || substr_val.type != VAL_STRING) {
        return make_error_detailed("contains() requires string arguments", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    const char* str = str_val.as.string ? string_data(str_val.as.string) : "";
    const char* substr = substr_val.as.string ? string_data(substr_val.as.string) : "";
    
    bool found = strstr(str, substr) != NULL;
    return make_success(make_bool_value(found));
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

EvalResult builtin_random(Environment* env, Value* args, size_t arg_count) {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    
    if (arg_count == 0) {
        // Return float between 0.0 and 1.0
        double val = (double)rand() / RAND_MAX;
        return make_success(make_float_value(val));
    } else if (arg_count == 1) {
        // Return integer between 0 and max-1
        Value max_val = args[0];
        if (max_val.type != VAL_INTEGER) {
            return make_error_detailed("random() argument must be an integer", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        int32_t max = max_val.as.integer.value.i32;
        if (max <= 0) {
            return make_error_detailed("random() argument must be positive", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
        }
        int32_t val = rand() % max;
        return make_success(make_integer_value(NUM_INT32, val));
    } else if (arg_count == 2) {
        // Return integer between min and max-1
        Value min_val = args[0];
        Value max_val = args[1];
        if (min_val.type != VAL_INTEGER || max_val.type != VAL_INTEGER) {
            return make_error_detailed("random() arguments must be integers", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        int32_t min = min_val.as.integer.value.i32;
        int32_t max = max_val.as.integer.value.i32;
        if (max <= min) {
            return make_error_detailed("random() max must be greater than min", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
        }
        int32_t val = min + rand() % (max - min);
        return make_success(make_integer_value(NUM_INT32, val));
    }
    
    return make_error_detailed("random() expects 0, 1, or 2 arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
}

EvalResult builtin_time(Environment* env, Value* args, size_t arg_count) {
    (void)args; // Suppress unused warning
    if (arg_count != 0) {
        return make_error_detailed("time() expects no arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    time_t current_time = time(NULL);
    return make_success(make_integer_value(NUM_INT64, (int64_t)current_time));
}

EvalResult builtin_clock(Environment* env, Value* args, size_t arg_count) {
    (void)args; // Suppress unused warning
    if (arg_count != 0) {
        return make_error_detailed("clock() expects no arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    clock_t current_clock = clock();
    double seconds = (double)current_clock / CLOCKS_PER_SEC;
    return make_success(make_float_value(seconds));
}

// =============================================================================
// FILE I/O FUNCTIONS
// =============================================================================

EvalResult builtin_load(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("load() expects exactly 1 argument (filename)", 
                                  "Usage: load(\"script.mob\")", ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    Value filename_val = args[0];
    if (filename_val.type != VAL_STRING) {
        return make_error_detailed("load() argument must be a string", 
                                  "Usage: load(\"script.mob\")", ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    const char* filename = filename_val.as.string ? string_data(filename_val.as.string) : "";
    if (!filename || string_length(filename_val.as.string) == 0) {
        return make_error_detailed("load() filename cannot be empty", 
                                  "Provide a valid filename", ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    // Check if file exists
    if (!file_exists(filename)) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "File '%s' does not exist", filename);
        return make_error_detailed(error_msg, 
                                  "Check the file path and ensure the file exists", ERROR_RUNTIME, 0, 0, NULL, NULL);
    }
    
    // Read file content
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to read file '%s': %s", filename, file_result.error);
        return make_error_detailed(error_msg, 
                                  "Check file permissions and availability", ERROR_RUNTIME, 0, 0, NULL, NULL);
    }
    
    // Parse and execute the loaded script in the current environment
    TokenArray tokens = scan_source(file_result.content);
    if (tokens.count == 0) {
        free_file_result(&file_result);
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "No code found in file '%s'", filename);
        return make_error_detailed(error_msg, 
                                  "Ensure the file contains valid Mobius code", ERROR_RUNTIME, 0, 0, NULL, NULL);
    }
    
    // Parse AST
    ParseResult parse_result = parse(tokens);
    free_token_array(&tokens);
    
    if (parse_result.had_error) {
        free_file_result(&file_result);
        free_parse_result(&parse_result);
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Parse error in file '%s'", filename);
        return make_error_detailed(error_msg, 
                                  "Check the syntax of the loaded file", ERROR_RUNTIME, 0, 0, NULL, NULL);
    }
    
    // Use the passed environment - this is much cleaner than global tracking!
    // The loaded script will execute in the same environment as the caller
    
    // Set source context for better error reporting
    const char* old_context = get_source_context();
    set_source_context(file_result.content);
    
    // Execute the loaded script in the caller's environment
    EvalResult eval_result = evaluate_program(parse_result.statements, parse_result.count, env);
    
    // Restore previous source context
    set_source_context(old_context);
    
    // Cleanup
    // With AST reference counting, we can now safely free the parse result
    // Functions retain their own references to the AST nodes they need
    free_parse_result(&parse_result);
    free_file_result(&file_result);
    
    if (is_error(eval_result)) {
        // Propagate the error from the loaded script
        return eval_result;
    }
    
    // Return true to indicate successful loading
    return make_success(make_bool_value(true));
}

// =============================================================================
// TABLE FUNCTIONS
// =============================================================================

EvalResult builtin_table_insert(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 3) {
        return make_error_detailed("table.insert() expects 3 arguments (table, key, value)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("table.insert() first argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    Value key = args[1];
    Value value = args[2];
    
    if (!table_set(table, key, value)) {
        return make_error_detailed("Failed to insert into table", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
    }
    
    return make_success(make_nil_value());
}

EvalResult builtin_table_remove(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error_detailed("table.remove() expects 2 arguments (table, key)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("table.remove() first argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    Value key = args[1];
    
    Value old_value = table_get(table, key);
    bool removed = table_remove(table, key);
    
    if (removed) {
        return make_success(old_value);
    } else {
        return make_success(make_nil_value());
    }
}

EvalResult builtin_table_has_key(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error_detailed("table.has_key() expects 2 arguments (table, key)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("table.has_key() first argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    Value key = args[1];
    
    bool has_key = table_has_key(table, key);
    return make_success(make_bool_value(has_key));
}

EvalResult builtin_table_size(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("table.size() expects 1 argument (table)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("table.size() argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    size_t size = table_size(table);
    return make_success(make_integer_value(NUM_INT64, (int64_t)size));
}

EvalResult builtin_setmetatable(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error_detailed("setmetatable() expects 2 arguments (table, metatable)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("setmetatable() first argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    
    if (args[1].type == VAL_NIL) {
        set_metatable(table, NULL);
    } else if (args[1].type == VAL_TABLE) {
        set_metatable(table, args[1].as.table);
    } else {
        return make_error_detailed("setmetatable() second argument must be a table or nil", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    return make_success(args[0]); // Return the original table
}

EvalResult builtin_getmetatable(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("getmetatable() expects 1 argument (table)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("getmetatable() argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    Table* metatable = get_metatable(table);
    
    if (metatable) {
        return make_success(make_table_value(metatable));
    } else {
        return make_success(make_nil_value());
    }
}

EvalResult builtin_pairs(Environment* env, Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error_detailed("pairs() expects 1 argument (table)", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_TABLE) {
        return make_error_detailed("pairs() argument must be a table", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    Table* table = args[0].as.table;
    
    // Create a table to hold the array of pairs
    Table* pairs_table = create_table(table->size * 2);
    if (!pairs_table) {
        return make_error_detailed("Failed to create pairs table", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
    }
    
    // Iterate through all entries and create [key, value] pairs
    size_t pair_index = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].is_occupied) {
            // Create a sub-table for this [key, value] pair
            Table* pair = create_table(2);
            if (!pair) {
                free_table(pairs_table);
                return make_error_detailed("Failed to create pair entry", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
            }
            
            // Set key as index 0, value as index 1
            Value key_index = make_integer_value(NUM_INT64, 0);
            Value value_index = make_integer_value(NUM_INT64, 1);
            
            if (!table_set(pair, key_index, copy_value(table->entries[i].key)) ||
                !table_set(pair, value_index, copy_value(table->entries[i].value))) {
                free_table(pair);
                free_table(pairs_table);
                return make_error_detailed("Failed to set pair values", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
            }
            
            // Add this pair to the main pairs table
            Value pair_index_val = make_integer_value(NUM_INT64, (int64_t)pair_index);
            if (!table_set(pairs_table, pair_index_val, make_table_value(pair))) {
                free_table(pair);
                free_table(pairs_table);
                return make_error_detailed("Failed to add pair to result", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
            }
            
            pair_index++;
        }
    }
    
    return make_success(make_table_value(pairs_table));
}

// Type checking mode control functions
EvalResult builtin_set_strict_types(Environment* env, Value* args, size_t arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count > 1) {
        return make_error_detailed("set_strict_types() expects 0 or 1 arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    bool strict = true; // Default to strict if no argument
    if (arg_count == 1) {
        if (args[0].type != VAL_BOOL) {
            return make_error_detailed("set_strict_types() argument must be a boolean", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
        }
        strict = args[0].as.boolean;
    }
    
    global_type_config.strict_mode = strict;
    return make_success(make_nil_value());
}

EvalResult builtin_set_type_warnings(Environment* env, Value* args, size_t arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 1) {
        return make_error_detailed("set_type_warnings() expects 1 argument", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    if (args[0].type != VAL_BOOL) {
        return make_error_detailed("set_type_warnings() argument must be a boolean", NULL, ERROR_TYPE, 0, 0, NULL, NULL);
    }
    
    global_type_config.warn_on_conversion = args[0].as.boolean;
    return make_success(make_nil_value());
}

EvalResult builtin_get_type_config(Value* args __attribute__((unused)), size_t arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 0) {
        return make_error_detailed("get_type_config() expects no arguments", NULL, ERROR_ARGUMENT, 0, 0, NULL, NULL);
    }
    
    // Return a table with configuration
    Table* config_table = create_table(8);
    if (!config_table) {
        return make_error_detailed("Failed to create config table", NULL, ERROR_MEMORY, 0, 0, NULL, NULL);
    }
    
    // Create string keys - we need to allocate them
    char* strict_str = malloc(12);
    if (strict_str) strcpy(strict_str, "strict_mode");
    Value strict_key = make_string_value_from_cstr(strict_str);
    free(strict_str);
    Value strict_value = make_bool_value(global_type_config.strict_mode);
    table_set(config_table, strict_key, strict_value);
    
    char* warn_str = malloc(19);
    if (warn_str) strcpy(warn_str, "warn_on_conversion");
    Value warn_key = make_string_value_from_cstr(warn_str);
    free(warn_str);
    Value warn_value = make_bool_value(global_type_config.warn_on_conversion);
    table_set(config_table, warn_key, warn_value);
    
    return make_success(make_table_value(config_table));
}

// =============================================================================
// STANDARD LIBRARY MANAGEMENT
// =============================================================================

static const StdlibEntry stdlib_functions[] = {
    // Core functions
    {"print", builtin_print, SIZE_MAX, "Print values to stdout", "Core"},
    {"typeof", builtin_typeof, 1, "Get the type of a value", "Core"},
    {"str", builtin_str, 1, "Convert value to string", "Core"},
    {"int", builtin_int, 1, "Convert value to integer", "Core"},
    {"float", builtin_float, 1, "Convert value to float", "Core"},
    
    // Math functions
    {"abs", builtin_abs, 1, "Get absolute value", "Math"},
    {"min", builtin_min, SIZE_MAX, "Get minimum value from arguments", "Math"},
    {"max", builtin_max, SIZE_MAX, "Get maximum value from arguments", "Math"},
    {"pow", builtin_pow, 2, "Raise base to power", "Math"},
    {"sqrt", builtin_sqrt, 1, "Get square root", "Math"},
    {"floor", builtin_floor, 1, "Round down to integer", "Math"},
    {"ceil", builtin_ceil, 1, "Round up to integer", "Math"},
    {"round", builtin_round, 1, "Round to nearest integer", "Math"},
    
    // String functions
    {"len", builtin_len, 1, "Get string length", "String"},
    {"substr", builtin_substr, SIZE_MAX, "Extract substring", "String"},
    {"concat", builtin_concat, SIZE_MAX, "Concatenate strings", "String"},
    {"upper", builtin_upper, 1, "Convert to uppercase", "String"},
    {"lower", builtin_lower, 1, "Convert to lowercase", "String"},
    {"contains", builtin_contains, 2, "Check if string contains substring", "String"},
    
    // Utility functions
    {"random", builtin_random, SIZE_MAX, "Generate random number", "Utility"},
    {"time", builtin_time, 0, "Get current Unix timestamp", "Utility"},
    {"clock", builtin_clock, 0, "Get program execution time", "Utility"},
    
    // File I/O functions
    {"load", builtin_load, 1, "Load and execute a script file", "File"},
    
    // Table functions
    {"table_insert", builtin_table_insert, 3, "Insert key-value pair into table", "Table"},
    {"table_remove", builtin_table_remove, 2, "Remove key from table and return old value", "Table"},
    {"table_has_key", builtin_table_has_key, 2, "Check if table contains key", "Table"},
    {"table_size", builtin_table_size, 1, "Get number of entries in table", "Table"},
    {"setmetatable", builtin_setmetatable, 2, "Set metatable for table", "Table"},
    {"getmetatable", builtin_getmetatable, 1, "Get metatable of table", "Table"},
    {"pairs", builtin_pairs, 1, "Get array of [key, value] pairs from table", "Table"},
    
    // Type system configuration
    {"set_strict_types", builtin_set_strict_types, SIZE_MAX, "Enable/disable strict type checking", "Types"},
    {"set_type_warnings", builtin_set_type_warnings, 1, "Enable/disable type conversion warnings", "Types"},
    {"get_type_config", builtin_get_type_config, 0, "Get current type checking configuration", "Types"},
};

static const size_t stdlib_count = sizeof(stdlib_functions) / sizeof(stdlib_functions[0]);

const StdlibEntry* get_stdlib_functions(void) {
    return stdlib_functions;
}

size_t get_stdlib_count(void) {
    return stdlib_count;
}

BuiltinFunction lookup_stdlib_function(const char* name) {
    for (size_t i = 0; i < stdlib_count; i++) {
        if (strcmp(stdlib_functions[i].name, name) == 0) {
            return stdlib_functions[i].function;
        }
    }
    return NULL;
}

void register_stdlib_functions(Environment* env) {
    // Standard library functions are looked up dynamically
    // They don't need to be registered in the environment
    (void)env;
}

// =============================================================================
// HELP AND DOCUMENTATION
// =============================================================================

void print_stdlib_help(void) {
    printf("\nMobius Standard Library Functions:\n");
    printf("==================================\n\n");
    
    const char* current_category = "";
    for (size_t i = 0; i < stdlib_count; i++) {
        const StdlibEntry* entry = &stdlib_functions[i];
        
        // Print category header if changed
        if (strcmp(current_category, entry->category) != 0) {
            current_category = entry->category;
            printf("%s Functions:\n", current_category);
            printf("-------------------\n");
        }
        
        printf("  %-15s - %s\n", entry->name, entry->description);
        
        // Add spacing between categories
        if (i + 1 < stdlib_count && strcmp(entry->category, stdlib_functions[i + 1].category) != 0) {
            printf("\n");
        }
    }
    
    printf("\nUse ':help <function>' for detailed help on a specific function.\n\n");
}

void print_function_help(const char* function_name) {
    for (size_t i = 0; i < stdlib_count; i++) {
        const StdlibEntry* entry = &stdlib_functions[i];
        if (strcmp(entry->name, function_name) == 0) {
            printf("\nFunction: %s\n", entry->name);
            printf("Category: %s\n", entry->category);
            printf("Description: %s\n", entry->description);
            
            if (entry->arity == SIZE_MAX) {
                printf("Arguments: Variable number of arguments\n");
            } else if (entry->arity == 0) {
                printf("Arguments: None\n");
            } else {
                printf("Arguments: %zu\n", entry->arity);
            }
            
            printf("\n");
            return;
        }
    }
    
    printf("Unknown function: %s\n", function_name);
    printf("Use ':help' to see all available functions.\n");
}
