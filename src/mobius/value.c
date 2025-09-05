#include "value.h"
#include "table.h"
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

Value make_integer_value(NumericType type, int64_t val) {
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

Value make_float_value(double val) {
    Value value = {0};
    value.type = VAL_FLOAT;
    value.as.float_val = val;
    return value;
}

Value make_string_value(char* string) {
    Value value = {0};
    value.type = VAL_STRING;
    value.as.string = string;  // Takes ownership of the string
    return value;
}

Value make_char_value(char val) {
    Value value = {0};
    value.type = VAL_CHAR;
    value.as.character = val;
    return value;
}

Value make_function_value(MobiusFunction* function) {
    Value value = {0};
    value.type = VAL_FUNCTION;
    value.as.function = function;
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
        case VAL_FLOAT: return value.as.float_val != 0.0;
        case VAL_STRING: return value.as.string != NULL && strlen(value.as.string) > 0;
        case VAL_CHAR: return value.as.character != '\0';
        case VAL_FUNCTION: return value.as.function != NULL;
        case VAL_TABLE: return value.as.table != NULL;
        case VAL_USERDATA: return value.as.userdata.ptr != NULL;
        default: return false;
    }
}

bool values_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_INTEGER: {
            if (a.as.integer.num_type != b.as.integer.num_type) return false;
            switch (a.as.integer.num_type) {
                case NUM_INT8:   return a.as.integer.value.i8 == b.as.integer.value.i8;
                case NUM_UINT8:  return a.as.integer.value.u8 == b.as.integer.value.u8;
                case NUM_INT16:  return a.as.integer.value.i16 == b.as.integer.value.i16;
                case NUM_UINT16: return a.as.integer.value.u16 == b.as.integer.value.u16;
                case NUM_INT32:  return a.as.integer.value.i32 == b.as.integer.value.i32;
                case NUM_UINT32: return a.as.integer.value.u32 == b.as.integer.value.u32;
                case NUM_INT64:  return a.as.integer.value.i64 == b.as.integer.value.i64;
                case NUM_UINT64: return a.as.integer.value.u64 == b.as.integer.value.u64;
                default: return false;
            }
        }
        case VAL_FLOAT: return a.as.float_val == b.as.float_val;
        case VAL_STRING: 
            return (a.as.string == b.as.string) || 
                   (a.as.string && b.as.string && strcmp(a.as.string, b.as.string) == 0);
        case VAL_CHAR: return a.as.character == b.as.character;
        case VAL_FUNCTION: return a.as.function == b.as.function;
        case VAL_TABLE: return a.as.table == b.as.table; // Reference equality
        case VAL_USERDATA: 
            // Userdata equality: same pointer AND same type
            return a.as.userdata.ptr == b.as.userdata.ptr && 
                   a.as.userdata.type_name == b.as.userdata.type_name;
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
        case VAL_FLOAT:
            printf("%g", value.as.float_val);
            break;
        case VAL_STRING:
            printf("%s", value.as.string ? value.as.string : "(null)");
            break;
        case VAL_CHAR:
            printf("'%c'", value.as.character);
            break;
        case VAL_FUNCTION:
            if (value.as.function) {
                printf("<func %.*s>", value.as.function->name.length, value.as.function->name.start);
            } else {
                printf("<func (null)>");
            }
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
        case VAL_FLOAT:
            snprintf(buffer, sizeof(buffer), "%g", value.as.float_val);
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_STRING:
            if (value.as.string) {
                result = malloc(strlen(value.as.string) + 1);
                if (result) strcpy(result, value.as.string);
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
        case VAL_FLOAT: return "float";
        case VAL_STRING: return "string";
        case VAL_CHAR: return "char";
        case VAL_FUNCTION: return "function";
        case VAL_TABLE: return "table";
        case VAL_USERDATA: return "userdata";
        default: return "unknown";
    }
}

// Create a deep copy of a value (handles string duplication)
Value copy_value(Value value) {
    if (value.type == VAL_STRING && value.as.string) {
        // Create a deep copy of the string
        char* str_copy = malloc(strlen(value.as.string) + 1);
        if (str_copy) {
            strcpy(str_copy, value.as.string);
            return make_string_value(str_copy);
        } else {
            // Allocation failed, return nil
            return make_nil_value();
        }
    } else if (value.type == VAL_FUNCTION && value.as.function) {
        // For functions, we don't copy the function structure itself
        // as they should be shared. Just return the same value.
        return value;
    } else if (value.type == VAL_TABLE && value.as.table) {
        // For tables, increment reference count (shared ownership)
        value.as.table->ref_count++;
        return value;
    } else if (value.type == VAL_USERDATA) {
        // For userdata, we don't manage the memory - the external code does
        // Just return the same value (shared reference)
        return value;
    } else {
        // For other types (primitives), simple copy is sufficient
        return value;
    }
}

void free_value(Value value) {
    if (value.type == VAL_STRING && value.as.string) {
        free(value.as.string);
        // Note: We can't set value.as.string to NULL here since value is passed by copy
        // The caller should manage this to prevent double free
    } else if (value.type == VAL_FUNCTION && value.as.function) {
        MobiusFunction* func = value.as.function;
        if (func->params) free(func->params);
        if (func->body) {
            for (size_t i = 0; i < func->body_count; i++) {
                // Note: free_stmt is in ast.c, need to declare it or move it
                // For now, we'll skip this to avoid circular dependencies
                // free_stmt(func->body[i]);
            }
            free(func->body);
        }
        // Note: don't free closure environment - it's managed separately
        free(func);
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
    }
}
