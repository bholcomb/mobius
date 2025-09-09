#include "value.h"
#include "table.h"
#include "ast.h"  // For AST reference counting functions
#include "token.h"  // For token copying functions
#include "bytecode.h"  // For bytecode function management
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

Value make_float32_value(float val) {
    Value value = {0};
    value.type = VAL_FLOAT32;
    value.as.float32_val = val;
    return value;
}

Value make_float_value(double val) {
    Value value = {0};
    value.type = VAL_FLOAT;
    value.as.float_val = val;
    return value;
}

Value make_string_value(RefCountedString* string) {
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
        case VAL_FLOAT: return value.as.float_val != 0.0;
        case VAL_STRING: return value.as.string != NULL && string_length(value.as.string) > 0;
        case VAL_CHAR: return value.as.character != '\0';
        case VAL_ARRAY: return value.as.array != NULL && array_length(value.as.array) > 0;
        case VAL_FUNCTION: return value.as.function != NULL;
        case VAL_TABLE: return value.as.table != NULL;
        case VAL_USERDATA: return value.as.userdata.ptr != NULL;
        case VAL_ENUM: return true;  // Enums are always truthy
        default: return false;
    }
}

bool values_equal(Value a, Value b) {
    // Handle numeric type coercion for equality
    if ((a.type == VAL_INTEGER || a.type == VAL_FLOAT32 || a.type == VAL_FLOAT) &&
        (b.type == VAL_INTEGER || b.type == VAL_FLOAT32 || b.type == VAL_FLOAT)) {
        
        // Convert both values to double for comparison
        double a_val = 0.0, b_val = 0.0;
        
        // Convert a to double
        if (a.type == VAL_FLOAT) {
            a_val = a.as.float_val;
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
        if (b.type == VAL_FLOAT) {
            b_val = b.as.float_val;
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
            printf("%g", value.as.float32_val);
            break;
        case VAL_FLOAT:
            printf("%g", value.as.float_val);
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
            snprintf(buffer, sizeof(buffer), "%g", value.as.float32_val);
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
        case VAL_FLOAT: return "float";
        case VAL_STRING: return "string";
        case VAL_CHAR: return "char";
        case VAL_ARRAY: return "array";
        case VAL_FUNCTION: return "function";
        case VAL_BYTECODE_FUNCTION: return "function";
        case VAL_BUILTIN_FUNCTION: return "function";
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
        RefCountedString* retained_str = string_retain(value.as.string);
        return make_string_value(retained_str);
    } else if (value.type == VAL_ARRAY && value.as.array) {
        // For arrays, increment reference count (shared ownership)
        ArrayValue* retained_array = array_retain(value.as.array);
        return make_array_value(retained_array);
    } else if (value.type == VAL_FUNCTION && value.as.function) {
        // For AST functions, increment reference count (shared ownership)
        value.as.function->ref_count++;
        return value;
    } else if (value.type == VAL_BYTECODE_FUNCTION && value.as.bytecode_func) {
        // For bytecode functions, increment reference count (shared ownership)
        value.as.bytecode_func->ref_count++;
        return value;
    } else if (value.type == VAL_BUILTIN_FUNCTION) {
        // Builtin functions don't need reference counting - they're static
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
    } else if (value.type == VAL_BYTECODE_FUNCTION && value.as.bytecode_func) {
        bytecode_function_free(value.as.bytecode_func);
    } else if (value.type == VAL_BUILTIN_FUNCTION) {
        // Builtin functions are static - no cleanup needed
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

// ============================================================================
// STRING REFERENCE COUNTING IMPLEMENTATION
// ============================================================================

RefCountedString* string_create(const char* data) {
    if (!data) return NULL;
    
    RefCountedString* str = malloc(sizeof(RefCountedString));
    if (!str) return NULL;
    
    str->length = strlen(data);
    str->data = malloc(str->length + 1);
    if (!str->data) {
        free(str);
        return NULL;
    }
    
    strcpy(str->data, data);
    str->ref_count = 1;
    str->is_literal = false;
    
    return str;
}

RefCountedString* string_create_literal(const char* data) {
    if (!data) return NULL;
    
    RefCountedString* str = malloc(sizeof(RefCountedString));
    if (!str) return NULL;
    
    str->data = (char*)data;  // Point to literal, don't copy
    str->length = strlen(data);
    str->ref_count = 1;
    str->is_literal = true;   // Never free the data
    
    return str;
}

RefCountedString* string_retain(RefCountedString* str) {
    if (str) {
        str->ref_count++;
    }
    return str;
}

void string_release(RefCountedString* str) {
    if (!str) return;
    
    str->ref_count--;
    if (str->ref_count <= 0) {
        if (!str->is_literal) {
            free(str->data);  // Only free non-literals
        }
        free(str);
    }
}

size_t string_length(RefCountedString* str) {
    return str ? str->length : 0;
}

const char* string_data(RefCountedString* str) {
    return str ? str->data : NULL;
}

bool string_equals(RefCountedString* a, RefCountedString* b) {
    if (a == b) return true;  // Same pointer
    if (!a || !b) return false;  // One is NULL
    if (a->length != b->length) return false;  // Different lengths
    return strcmp(a->data, b->data) == 0;  // Compare content
}

// Helper function to create Value from C string
Value make_string_value_from_cstr(const char* cstr) {
    RefCountedString* str = string_create(cstr);
    return make_string_value(str);
}

// ============================================================================
// ARRAY REFERENCE COUNTING IMPLEMENTATION
// ============================================================================

ArrayValue* array_create(size_t initial_capacity) {
    ArrayValue* array = calloc(1, sizeof(ArrayValue));
    if (!array) return NULL;
    
    array->capacity = initial_capacity > 0 ? initial_capacity : 8;  // Default capacity
    array->elements = calloc(array->capacity, sizeof(Value));
    if (!array->elements) {
        free(array);
        return NULL;
    }
    
    array->length = 0;
    array->ref_count = 1;
    
    return array;
}

ArrayValue* array_retain(ArrayValue* array) {
    if (array) {
        array->ref_count++;
    }
    return array;
}

void array_release(ArrayValue* array) {
    if (!array) return;
    
    array->ref_count--;
    if (array->ref_count <= 0) {
        // Free all elements
        for (size_t i = 0; i < array->length; i++) {
            free_value(array->elements[i]);
        }
        
        // Free the elements array
        free(array->elements);
        
        // Free the array structure
        free(array);
    }
}

void array_push(ArrayValue* array, Value value) {
    if (!array) return;
    
    // Resize if necessary
    if (array->length >= array->capacity) {
        array_resize(array, array->capacity * 2);
    }
    
    // Add the element (copy to handle reference counting)
    array->elements[array->length] = copy_value(value);
    array->length++;
}

Value array_pop(ArrayValue* array) {
    if (!array || array->length == 0) {
        return make_nil_value();
    }
    
    array->length--;
    Value result = array->elements[array->length];
    
    // Clear the slot (set to nil without freeing, since we're returning it)
    array->elements[array->length] = make_nil_value();
    
    return result;
}

Value array_get(ArrayValue* array, size_t index) {
    if (!array || index >= array->length) {
        return make_nil_value();
    }
    
    return copy_value(array->elements[index]);
}

void array_set(ArrayValue* array, size_t index, Value value) {
    if (!array || index >= array->length) return;
    
    // Free the old value
    free_value(array->elements[index]);
    
    // Set the new value (copy to handle reference counting)
    array->elements[index] = copy_value(value);
}

size_t array_length(ArrayValue* array) {
    return array ? array->length : 0;
}

void array_resize(ArrayValue* array, size_t new_capacity) {
    if (!array || new_capacity <= array->capacity) return;
    
    Value* new_elements = realloc(array->elements, new_capacity * sizeof(Value));
    if (!new_elements) return;  // Failed to resize
    
    // Initialize new slots to nil
    for (size_t i = array->capacity; i < new_capacity; i++) {
        new_elements[i] = make_nil_value();
    }
    
    array->elements = new_elements;
    array->capacity = new_capacity;
}

// ============================================================================
// ENUM IMPLEMENTATION
// ============================================================================

EnumDefinition* enum_definition_create(const char* name, NumericType underlying_type) {
    EnumDefinition* enum_def = calloc(1, sizeof(EnumDefinition));
    if (!enum_def) return NULL;
    
    enum_def->name = malloc(strlen(name) + 1);
    if (!enum_def->name) {
        free(enum_def);
        return NULL;
    }
    strcpy(enum_def->name, name);
    
    enum_def->underlying_type = underlying_type;
    enum_def->members = NULL;
    enum_def->ref_count = 1;
    enum_def->next_auto_value = 0;
    
    return enum_def;
}

EnumDefinition* enum_definition_retain(EnumDefinition* enum_def) {
    if (enum_def) {
        enum_def->ref_count++;
    }
    return enum_def;
}

void enum_definition_release(EnumDefinition* enum_def) {
    if (!enum_def) return;
    
    enum_def->ref_count--;
    if (enum_def->ref_count <= 0) {
        // Free all members
        EnumMember* member = enum_def->members;
        while (member) {
            EnumMember* next = member->next;
            free(member->name);
            free(member);
            member = next;
        }
        
        // Free the name
        free(enum_def->name);
        
        // Free the definition
        free(enum_def);
    }
}

void enum_definition_add_member(EnumDefinition* enum_def, const char* name, int64_t value) {
    if (!enum_def || !name) return;
    
    EnumMember* member = malloc(sizeof(EnumMember));
    if (!member) return;
    
    member->name = malloc(strlen(name) + 1);
    if (!member->name) {
        free(member);
        return;
    }
    strcpy(member->name, name);
    
    member->value = value;
    member->next = enum_def->members;
    enum_def->members = member;
    
    // Update next auto value
    enum_def->next_auto_value = value + 1;
}

void enum_definition_add_auto_member(EnumDefinition* enum_def, const char* name) {
    if (!enum_def || !name) return;
    
    enum_definition_add_member(enum_def, name, enum_def->next_auto_value);
}

EnumMember* enum_definition_find_member(EnumDefinition* enum_def, const char* name) {
    if (!enum_def || !name) return NULL;
    
    EnumMember* member = enum_def->members;
    while (member) {
        if (strcmp(member->name, name) == 0) {
            return member;
        }
        member = member->next;
    }
    
    return NULL;
}

EnumMember* enum_definition_find_member_by_value(EnumDefinition* enum_def, int64_t value) {
    if (!enum_def) return NULL;
    
    EnumMember* member = enum_def->members;
    while (member) {
        if (member->value == value) {
            return member;
        }
        member = member->next;
    }
    
    return NULL;
}

bool enum_values_equal(Value a, Value b) {
    if (a.type != VAL_ENUM || b.type != VAL_ENUM) return false;
    return a.as.enum_val.definition == b.as.enum_val.definition &&
           a.as.enum_val.value == b.as.enum_val.value;
}

const char* enum_value_name(Value enum_val) {
    if (enum_val.type != VAL_ENUM || !enum_val.as.enum_val.definition) return NULL;
    
    EnumMember* member = enum_definition_find_member_by_value(
        enum_val.as.enum_val.definition, 
        enum_val.as.enum_val.value
    );
    
    return member ? member->name : NULL;
}
