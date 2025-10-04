#include "data/value.h"
#include "data/enum.h"
#include "data/table.h"
#include "data/array.h"
#include "data/function.h"
#include "util/utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Value creation functions
Value make_nil_value() {
    Value value = {0};
    value.type = VAL_NIL;
    return value;
}

Value make_bool_value(bool val) {
    Value value = {0};
    value.type = VAL_BOOL;
    value.as.boolean = val;
    return value;
}

Value make_integer_value(NumberType type, int64_t val) {
    Value value = {0};
    value.type = VAL_INTEGER;
    value.as.integer.num_type = type;
    
    switch (type) {
        case NUM_INT8:   value.as.integer.value.i8  = (int8_t)val; break;
        case NUM_UINT8:  value.as.integer.value.u8  = (uint8_t)val; break;
        case NUM_INT16:  value.as.integer.value.i16 = (int16_t)val; break;
        case NUM_UINT16: value.as.integer.value.u16 = (uint16_t)val; break;
        case NUM_INT32:  value.as.integer.value.i32 = (int32_t)val; break;
        case NUM_UINT32: value.as.integer.value.u32 = (uint32_t)val; break;
        case NUM_INT64:  value.as.integer.value.i64 = val; break;
        case NUM_UINT64: value.as.integer.value.u64 = (uint64_t)val; break;
        default: break;
    }
    
    return value;
}

Value make_float32_value(float val) {
    Value value = {0};
    value.type = VAL_FLOAT32;
    value.as.float32_val = val;
    return value;
}

Value make_float_value(double val) {
    Value value = {0};
    value.type = VAL_FLOAT64;
    value.as.float64_val = val;
    return value;
}

Value make_string_value(MobiusString* string) {
    Value value = {0};
    value.type = VAL_STRING;
    value.as.string = string;  // Takes ownership of the ref-counted string
    return value;
}

Value make_char_value(char val) {
    Value value = {0};
    value.type = VAL_CHAR;
    value.as.character = val;
    return value;
}

Value make_array_value(ArrayValue* array) {
    Value value = {0};
    value.type = VAL_ARRAY;
    value.as.array = array;
    return value;
}

Value make_function_value(MobiusFunction* function) {
    Value value = {0};
    value.type = VAL_FUNCTION;
    value.as.function = function;
    return value;
}

Value make_native_function_value(MobiusCFunction function) {
    Value value = {0};
    value.type = VAL_NATIVE_FUNCTION;
    value.as.native_function = function;
    return value;
}

Value make_table_value(Table* table) {
    Value value = {0};
    value.type = VAL_TABLE;
    value.as.table = table;
    return value;
}

Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size) {
    Value value = {0};
    value.type = VAL_USERDATA;
    value.as.userdata.ptr = ptr;
    value.as.userdata.destructor = destructor;
    value.as.userdata.type_name = type_name;
    value.as.userdata.size = size;
    return value;
}

Value make_enum_value(EnumDefinition* definition, int64_t val) {
    Value value = {0};
    value.type = VAL_ENUM;
    value.as.enum_val.definition = enum_definition_retain(definition);
    value.as.enum_val.value = (int32_t)val;  // Store as int32 for now
    return value;
}

// Value utility functions
bool is_truthy(Value value) {
    switch (value.type) {
        case VAL_NIL: return false;
        case VAL_BOOL: return value.as.boolean;
        case VAL_INTEGER: {
            switch (value.as.integer.num_type) {
                case NUM_INT8:   return value.as.integer.value.i8 != 0;
                case NUM_UINT8:  return value.as.integer.value.u8 != 0;
                case NUM_INT16:  return value.as.integer.value.i16 != 0;
                case NUM_UINT16: return value.as.integer.value.u16 != 0;
                case NUM_INT32:  return value.as.integer.value.i32 != 0;
                case NUM_UINT32: return value.as.integer.value.u32 != 0;
                case NUM_INT64:  return value.as.integer.value.i64 != 0;
                case NUM_UINT64: return value.as.integer.value.u64 != 0;
                default: return false;
            }
        }
        case VAL_FLOAT32: return value.as.float32_val != 0.0f;
        case VAL_FLOAT64: return value.as.float64_val != 0.0;
        case VAL_STRING: return value.as.string != NULL && string_length(value.as.string) > 0;
        case VAL_CHAR: return value.as.character != '\0';
        case VAL_ARRAY: return value.as.array != NULL && array_length(value.as.array) > 0;
        case VAL_FUNCTION: return value.as.function != NULL;
        case VAL_NATIVE_FUNCTION: return value.as.native_function != NULL;
        case VAL_TABLE: return value.as.table != NULL;
        case VAL_USERDATA: return value.as.userdata.ptr != NULL;
        case VAL_ENUM: return true;  // Enums are always truthy
        default: return false;
    }
}

bool values_equal(Value a, Value b) {
    // Handle numeric type coercion for equality
    if ((a.type == VAL_INTEGER || a.type == VAL_FLOAT32 || a.type == VAL_FLOAT64) &&
        (b.type == VAL_INTEGER || b.type == VAL_FLOAT32 || b.type == VAL_FLOAT64)) {
        
        // Convert both values to double for comparison
        double a_val = 0.0, b_val = 0.0;
        
        // Convert a to double
        if (a.type == VAL_FLOAT64) {
            a_val = a.as.float64_val;
        } else if (a.type == VAL_FLOAT32) {
            a_val = (double)a.as.float32_val;
        } else if (a.type == VAL_INTEGER) {
            switch (a.as.integer.num_type) {
                case NUM_INT8:   a_val = a.as.integer.value.i8; break;
                case NUM_UINT8:  a_val = a.as.integer.value.u8; break;
                case NUM_INT16:  a_val = a.as.integer.value.i16; break;
                case NUM_UINT16: a_val = a.as.integer.value.u16; break;
                case NUM_INT32:  a_val = a.as.integer.value.i32; break;
                case NUM_UINT32: a_val = a.as.integer.value.u32; break;
                case NUM_INT64:  a_val = a.as.integer.value.i64; break;
                case NUM_UINT64: a_val = a.as.integer.value.u64; break;
                default: a_val = 0.0; break;
            }
        }
        
        // Convert b to double
        if (b.type == VAL_FLOAT64) {
            b_val = b.as.float64_val;
        } else if (b.type == VAL_FLOAT32) {
            b_val = (double)b.as.float32_val;
        } else if (b.type == VAL_INTEGER) {
            switch (b.as.integer.num_type) {
                case NUM_INT8:   b_val = b.as.integer.value.i8; break;
                case NUM_UINT8:  b_val = b.as.integer.value.u8; break;
                case NUM_INT16:  b_val = b.as.integer.value.i16; break;
                case NUM_UINT16: b_val = b.as.integer.value.u16; break;
                case NUM_INT32:  b_val = b.as.integer.value.i32; break;
                case NUM_UINT32: b_val = b.as.integer.value.u32; break;
                case NUM_INT64:  b_val = b.as.integer.value.i64; break;
                case NUM_UINT64: b_val = b.as.integer.value.u64; break;
                default: b_val = 0.0; break;
            }
        }
        
        return a_val == b_val;
    }
    
    // Non-numeric types must have exact type match
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_STRING: 
            return (a.as.string == b.as.string) || string_equals(a.as.string, b.as.string);
        case VAL_CHAR: return a.as.character == b.as.character;
        case VAL_ARRAY:
            // Array equality: same reference (arrays are reference types)
            return a.as.array == b.as.array;
        case VAL_FUNCTION: return a.as.function == b.as.function;
        case VAL_NATIVE_FUNCTION: return a.as.native_function == b.as.native_function;
        case VAL_TABLE: return a.as.table == b.as.table; // Reference equality
        case VAL_USERDATA: 
            // Userdata equality: same pointer AND same type
            return a.as.userdata.ptr == b.as.userdata.ptr && 
                   a.as.userdata.type_name == b.as.userdata.type_name;
        case VAL_ENUM:
            // Enum equality: same definition and same value
            return a.as.enum_val.definition == b.as.enum_val.definition &&
                   a.as.enum_val.value == b.as.enum_val.value;
        default: return false;
    }
}

void print_value(Value value) {
    switch (value.type) {
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_BOOL:
            printf(value.as.boolean ? "true" : "false");
            break;
        case VAL_INTEGER:
            switch (value.as.integer.num_type) {
                case NUM_INT8:   printf("%d", value.as.integer.value.i8); break;
                case NUM_UINT8:  printf("%u", value.as.integer.value.u8); break;
                case NUM_INT16:  printf("%d", value.as.integer.value.i16); break;
                case NUM_UINT16: printf("%u", value.as.integer.value.u16); break;
                case NUM_INT32:  printf("%d", value.as.integer.value.i32); break;
                case NUM_UINT32: printf("%u", value.as.integer.value.u32); break;
                case NUM_INT64:  printf("%ld", value.as.integer.value.i64); break;
                case NUM_UINT64: printf("%lu", value.as.integer.value.u64); break;
                default: printf("unknown_int"); break;
            }
            break;
        case VAL_FLOAT32:
            // Use %g but ensure at least one decimal place for whole numbers
            if (value.as.float32_val == (float)(int)value.as.float32_val) {
                printf("%.1f", value.as.float32_val);
            } else {
                printf("%g", value.as.float32_val);
            }
            break;
        case VAL_FLOAT64:
            // Use %g but ensure at least one decimal place for whole numbers
            if (value.as.float64_val == (double)(long long)value.as.float64_val) {
                printf("%.1f", value.as.float64_val);
            } else {
                printf("%g", value.as.float64_val);
            }
            break;
        case VAL_STRING:
            printf("%s", value.as.string ? string_data(value.as.string) : "(null)");
            break;
        case VAL_CHAR:
            printf("'%c'", value.as.character);
            break;
        case VAL_ARRAY:
            if (value.as.array) {
                printf("[");
                for (size_t i = 0; i < value.as.array->length; i++) {
                    if (i > 0) printf(", ");
                    print_value(value.as.array->elements[i]);
                }
                printf("]");
            } else {
                printf("<array (null)>");
            }
            break;
        case VAL_FUNCTION:
            if (value.as.function) {
                printf("<func %s>", value.as.function->name ? value.as.function->name : "anonymous");
            } else {
                printf("<func (null)>");
            }
            break;
        case VAL_NATIVE_FUNCTION:
            printf("<native function>");
            break;
        case VAL_TABLE:
            if (value.as.table) {
                print_table(value.as.table);
            } else {
                printf("<table (null)>");
            }
            break;
        case VAL_USERDATA:
            if (value.as.userdata.ptr) {
                printf("<%s userdata %p>", 
                       value.as.userdata.type_name ? value.as.userdata.type_name : "unknown",
                       value.as.userdata.ptr);
            } else {
                printf("<userdata (null)>");
            }
            break;
        case VAL_ENUM: {
            const char* member_name = enum_value_name(value);
            if (member_name) {
                printf("%s.%s", value.as.enum_val.definition->name, member_name);
            } else {
                printf("%s(%d)", value.as.enum_val.definition->name, value.as.enum_val.value);
            }
            break;
        }
        default:
            printf("unknown_value");
            break;
    }
}

char* value_to_string(Value value) {
    char* result = NULL;
    char buffer[64];
    
    switch (value.type) {
        case VAL_NIL:
            result = malloc(4);
            if (result) strcpy(result, "nil");
            break;
        case VAL_BOOL:
            result = malloc(6);
            if (result) strcpy(result, value.as.boolean ? "true" : "false");
            break;
        case VAL_INTEGER:
            switch (value.as.integer.num_type) {
                case NUM_INT8:   snprintf(buffer, sizeof(buffer), "%d", value.as.integer.value.i8); break;
                case NUM_UINT8:  snprintf(buffer, sizeof(buffer), "%u", value.as.integer.value.u8); break;
                case NUM_INT16:  snprintf(buffer, sizeof(buffer), "%d", value.as.integer.value.i16); break;
                case NUM_UINT16: snprintf(buffer, sizeof(buffer), "%u", value.as.integer.value.u16); break;
                case NUM_INT32:  snprintf(buffer, sizeof(buffer), "%d", value.as.integer.value.i32); break;
                case NUM_UINT32: snprintf(buffer, sizeof(buffer), "%u", value.as.integer.value.u32); break;
                case NUM_INT64:  snprintf(buffer, sizeof(buffer), "%ld", value.as.integer.value.i64); break;
                case NUM_UINT64: snprintf(buffer, sizeof(buffer), "%lu", value.as.integer.value.u64); break;
                default: strcpy(buffer, "0"); break;
            }
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_FLOAT32:
            // Use %g but ensure at least one decimal place for whole numbers
            if (value.as.float32_val == (float)(int)value.as.float32_val) {
                snprintf(buffer, sizeof(buffer), "%.1f", value.as.float32_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%g", value.as.float32_val);
            }
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_FLOAT64:
            // Use %g but ensure at least one decimal place for whole numbers
            if (value.as.float64_val == (double)(long long)value.as.float64_val) {
                snprintf(buffer, sizeof(buffer), "%.1f", value.as.float64_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%g", value.as.float64_val);
            }
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_STRING:
            if (value.as.string) {
                const char* str_data = string_data(value.as.string);
                size_t str_len = string_length(value.as.string);
                result = malloc(str_len + 1);
                if (result) strcpy(result, str_data);
            } else {
                result = malloc(7);
                if (result) strcpy(result, "(null)");
            }
            break;
        case VAL_CHAR:
            result = malloc(2);
            if (result) {
                result[0] = value.as.character;
                result[1] = '\0';
            }
            break;
        case VAL_FUNCTION:
            snprintf(buffer, sizeof(buffer), "<function>");
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_NATIVE_FUNCTION:
            snprintf(buffer, sizeof(buffer), "<native function>");
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_TABLE:
            snprintf(buffer, sizeof(buffer), "<table>");
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_USERDATA:
            if (value.as.userdata.ptr) {
                snprintf(buffer, sizeof(buffer), "<%s userdata %p>", 
                         value.as.userdata.type_name ? value.as.userdata.type_name : "unknown",
                         value.as.userdata.ptr);
            } else {
                strcpy(buffer, "<userdata (null)>");
            }
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_ENUM: {
            const char* member_name = enum_value_name(value);
            if (member_name) {
                snprintf(buffer, sizeof(buffer), "%s.%s", value.as.enum_val.definition->name, member_name);
            } else {
                snprintf(buffer, sizeof(buffer), "%s(%d)", value.as.enum_val.definition->name, value.as.enum_val.value);
            }
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        }
        default:
            result = malloc(8);
            if (result) strcpy(result, "unknown");
            break;
    }
    
    return result;
}

const char* value_type_name(ValueType type) {
    switch (type) {
        case VAL_NIL: return "nil";
        case VAL_BOOL: return "bool";
        case VAL_INTEGER: return "integer";
        case VAL_FLOAT32: return "float32";
        case VAL_FLOAT64: return "float";
        case VAL_STRING: return "string";
        case VAL_CHAR: return "char";
        case VAL_ARRAY: return "array";
        case VAL_FUNCTION: return "function";
        case VAL_NATIVE_FUNCTION: return "function";
        case VAL_TABLE: return "table";
        case VAL_USERDATA: return "userdata";
        case VAL_ENUM: return "enum";
        default: return "unknown";
    }
}

// Create a copy of a value (uses reference counting for strings)
Value copy_value(Value value) {
    if (value.type == VAL_STRING && value.as.string) {
        // Use reference counting - just increment ref count!
        MobiusString* retained_str = string_retain(value.as.string);
        return make_string_value(retained_str);
    } else if (value.type == VAL_ARRAY && value.as.array) {
        // For arrays, increment reference count (shared ownership)
        ArrayValue* retained_array = array_retain(value.as.array);
        return make_array_value(retained_array);
    } else if (value.type == VAL_FUNCTION && value.as.function) {
        // For functions (both AST and builtin), increment reference count (shared ownership)
        value.as.function->ref_count++;
        return value;
    } else if (value.type == VAL_TABLE && value.as.table) {
        // For tables, increment reference count (shared ownership)
        value.as.table->ref_count++;
        return value;
    } else if (value.type == VAL_USERDATA) {
        // For userdata, we don't manage the memory - the external code does
        // Just return the same value (shared reference)
        return value;
    } else if (value.type == VAL_ENUM && value.as.enum_val.definition) {
        // For enums, increment the definition reference count
        value.as.enum_val.definition = enum_definition_retain(value.as.enum_val.definition);
        return value;
    } else {
        // For other types (primitives), simple copy is sufficient
        return value;
    }
}

void free_value(Value value) {
    if (value.type == VAL_STRING && value.as.string) {
        // Use reference counting - release the string
        string_release(value.as.string);
    } else if (value.type == VAL_ARRAY && value.as.array) {
        // Use reference counting - release the array
        array_release(value.as.array);
    } else if (value.type == VAL_FUNCTION && value.as.function) {
        MobiusFunction* func = value.as.function;
        
        // Decrement reference count
        func->ref_count--;
        
        // Only free if reference count reaches zero
        if (func->ref_count <= 0) {
            // Free function name string
            if (func->name) {
                free(func->name);
            }
            
            // Free parameter name strings
            if (func->param_names) {
                for (size_t i = 0; i < func->param_count; i++) {
                    if (func->param_names[i]) {
                        free(func->param_names[i]);
                    }
                }
                free(func->param_names);
            }
            // Note: don't free closure environment - it's managed separately
            
            free(func);
        }
    } else if (value.type == VAL_NATIVE_FUNCTION) {
        // Native functions are just function pointers, nothing to free
    } else if (value.type == VAL_TABLE && value.as.table) {
        Table* table = value.as.table;
        table->ref_count--;
        if (table->ref_count <= 0) {
            // Free the table when reference count reaches zero
            free_table(table);
        }
    } else if (value.type == VAL_USERDATA && value.as.userdata.ptr) {
        // For userdata, we don't automatically call destructors during free_value
        // The embedding code is responsible for managing the userdata lifetime
        // Destructors should only be called when the embedding code explicitly
        // cleans up or when the MobiusState is freed
        // 
        // This prevents double-free issues when userdata values are copied
    } else if (value.type == VAL_ENUM && value.as.enum_val.definition) {
        // For enums, release the definition reference
        enum_definition_release(value.as.enum_val.definition);
    }
}


// Helper function to create Value from C string
Value make_string_value_from_cstr(struct MobiusState* state, const char* cstr) {
    if (!state || !cstr) {
        return make_nil_value();
    }
    MobiusString* str = string_create(state, cstr);
    return make_string_value(str);
}


TypeConversionResult validate_and_convert_value(Value value, NumberType target_type, bool is_annotated, TypeCheckConfig config) {
    TypeConversionResult result = {false, make_nil_value(), NULL, false};
    
    // If no type annotation, accept any value
    if (!is_annotated || target_type == NUM_UNKNOWN) {
        result.success = true;
        result.converted_value = copy_value(value);
        result.was_converted = false;
        return result;
    }
    
    // Handle integer target types
    if (is_integer_type(target_type)) {
        int64_t int_value = 0;
        bool conversion_needed = true;
        
        if (value.type == VAL_INTEGER) {
            int_value = value.as.integer.value.i64;
            conversion_needed = (target_type != NUM_INT64);
        } else if (value.type == VAL_FLOAT32) {
            if (config.strict_mode) {
                result.error_message = mobius_strdup("Cannot convert float32 to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.float32_val;
            conversion_needed = true;
        } else if (value.type == VAL_FLOAT64) {
            if (config.strict_mode) {
                result.error_message = mobius_strdup("Cannot convert float to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.float64_val;
            conversion_needed = true;
        } else {
            result.error_message = malloc(256);
            snprintf(result.error_message, 256, "Cannot convert %s to %s", 
                    value_type_name(value.type), number_type_name(target_type));
            return result;
        }
        
        // Check if value fits in target type range
        if (!value_fits_in_type(int_value, target_type)) {
            result.error_message = malloc(256);
            snprintf(result.error_message, 256, "Value %ld out of range for %s", 
                    int_value, number_type_name(target_type));
            return result;
        }
        
        // Create the converted value
        result.converted_value = make_integer_value(target_type, int_value);
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }
    
    // Handle float target types
    if (is_float_type(target_type)) {
        bool conversion_needed = false;
        
        if (target_type == NUM_FLOAT32) {
            float float32_value = 0.0f;
            
            if (value.type == VAL_FLOAT32) {
                float32_value = value.as.float32_val;
            } else if (value.type == VAL_FLOAT64) {
                if (config.strict_mode) {
                    result.error_message = mobius_strdup("Cannot convert float64 to float32 in strict mode");
                    return result;
                }
                float32_value = (float)value.as.float64_val;
                conversion_needed = true;
            } else if (value.type == VAL_INTEGER) {
                if (config.strict_mode) {
                    result.error_message = mobius_strdup("Cannot convert integer to float32 in strict mode");
                    return result;
                }
                float32_value = (float)value.as.integer.value.i64;
                conversion_needed = true;
            } else {
                result.error_message = malloc(256);
                snprintf(result.error_message, 256, "Cannot convert %s to %s", 
                        value_type_name(value.type), number_type_name(target_type));
                return result;
            }
            
            result.converted_value = make_float32_value(float32_value);
        } else { // NUM_FLOAT64 (float64)
            double float_value = 0.0;
            
            if (value.type == VAL_FLOAT64) {
                float_value = value.as.float64_val;
            } else if (value.type == VAL_FLOAT32) {
                float_value = (double)value.as.float32_val;
                conversion_needed = true;
            } else if (value.type == VAL_INTEGER) {
                if (config.strict_mode) {
                    result.error_message = mobius_strdup("Cannot convert integer to float in strict mode");
                    return result;
                }
                float_value = (double)value.as.integer.value.i64;
                conversion_needed = true;
            } else {
                result.error_message = malloc(256);
                snprintf(result.error_message, 256, "Cannot convert %s to %s", 
                        value_type_name(value.type), number_type_name(target_type));
                return result;
            }
            
            result.converted_value = make_float_value(float_value);
        }
        
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }
    
    // Unknown target type - shouldn't happen
    result.error_message = mobius_strdup("Unknown target type");
    return result;
}