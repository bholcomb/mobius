#include "data/value.h"
#include "data/enum.h"
#include "data/table.h"
#include "data/array.h"
#include "data/function.h"
#include "frontend/ast.h"
#include "state/mobius_state.h"
#include "util/utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Value refcount slow paths (called only for heap-allocated types)
// ============================================================================

void Value::retainSlow() const {
    switch (type) {
        case VAL_STRING:
            if (as.string) as.string->retain();
            break;
        case VAL_ARRAY:
            if (as.array) as.array->retain();
            break;
        case VAL_FUNCTION:
            if (as.function) as.function->ref_count++;
            break;
        case VAL_TABLE:
            if (as.table) as.table->retain();
            break;
        case VAL_ENUM:
            if (as.enum_val.definition) as.enum_val.definition->retain();
            break;
        default:
            break;
    }
}

void Value::releaseRefSlow() {
    switch (type) {
        case VAL_STRING:
            if (as.string) as.string->release();
            break;
        case VAL_ARRAY:
            if (as.array) as.array->release();
            break;
        case VAL_FUNCTION:
            if (as.function) {
                MobiusFunction* func = as.function;
                func->ref_count--;
                if (func->ref_count <= 0) {
                    free(func->param_names);
                    if (func->body) {
                        for (size_t i = 0; i < func->body_count; i++) {
                            if (func->body[i]) ast_release_stmt(func->body[i]);
                        }
                        free(func->body);
                    }
                    free(func);
                }
            }
            break;
        case VAL_TABLE:
            if (as.table) as.table->release();
            break;
        case VAL_ENUM:
            if (as.enum_val.definition) {
                as.enum_val.definition->release();
            }
            break;
        default:
            break;
    }
    type = VAL_NIL;
}

// ============================================================================
// Value creation functions
// ============================================================================

Value make_nil_value() {
    return Value();
}

Value make_bool_value(bool val) {
    Value value;
    value.type = VAL_BOOL;
    value.as.boolean = val;
    return value;
}

Value make_integer_value(NumberType numtype, int64_t val) {
    Value value;
    value.type = VAL_INTEGER;
    value.as.integer.num_type = numtype;

    switch (numtype) {
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
    Value value;
    value.type = VAL_FLOAT32;
    value.as.float32_val = val;
    return value;
}

Value make_float_value(double val) {
    Value value;
    value.type = VAL_FLOAT64;
    value.as.float64_val = val;
    return value;
}

Value make_string_value(MobiusString* string) {
    Value value;
    value.type = VAL_STRING;
    value.as.string = string;
    return value;
}

Value make_char_value(char val) {
    Value value;
    value.type = VAL_CHAR;
    value.as.character = val;
    return value;
}

Value make_array_value(ArrayValue* array) {
    Value value;
    value.type = VAL_ARRAY;
    value.as.array = array;
    return value;
}

Value make_function_value(MobiusFunction* function) {
    Value value;
    value.type = VAL_FUNCTION;
    value.as.function = function;
    return value;
}

Value make_native_function_value(MobiusCFunction function) {
    Value value;
    value.type = VAL_NATIVE_FUNCTION;
    value.as.native_function = function;
    return value;
}

Value make_table_value(Table* table) {
    Value value;
    value.type = VAL_TABLE;
    value.as.table = table;
    return value;
}

Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size) {
    Value value;
    value.type = VAL_USERDATA;
    value.as.userdata.ptr = ptr;
    value.as.userdata.destructor = destructor;
    value.as.userdata.type_name = type_name;
    value.as.userdata.size = size;
    return value;
}

// ============================================================================
// Value utility functions
// ============================================================================

bool is_truthy(const Value& value) {
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
        case VAL_STRING: return value.as.string != NULL && value.as.string->length > 0;
        case VAL_CHAR: return value.as.character != '\0';
        case VAL_ARRAY: return value.as.array != NULL && value.as.array->length() > 0;
        case VAL_FUNCTION: return value.as.function != NULL;
        case VAL_NATIVE_FUNCTION: return value.as.native_function != NULL;
        case VAL_TABLE: return value.as.table != NULL;
        case VAL_USERDATA: return value.as.userdata.ptr != NULL;
        case VAL_ENUM: return true;
        default: return false;
    }
}

bool Value::operator==(const Value& other) const {
    if ((type == VAL_INTEGER || type == VAL_FLOAT32 || type == VAL_FLOAT64) &&
        (other.type == VAL_INTEGER || other.type == VAL_FLOAT32 || other.type == VAL_FLOAT64)) {

        double a_val = 0.0, b_val = 0.0;

        if (type == VAL_FLOAT64) {
            a_val = as.float64_val;
        } else if (type == VAL_FLOAT32) {
            a_val = (double)as.float32_val;
        } else if (type == VAL_INTEGER) {
            switch (as.integer.num_type) {
                case NUM_INT8:   a_val = as.integer.value.i8; break;
                case NUM_UINT8:  a_val = as.integer.value.u8; break;
                case NUM_INT16:  a_val = as.integer.value.i16; break;
                case NUM_UINT16: a_val = as.integer.value.u16; break;
                case NUM_INT32:  a_val = as.integer.value.i32; break;
                case NUM_UINT32: a_val = as.integer.value.u32; break;
                case NUM_INT64:  a_val = as.integer.value.i64; break;
                case NUM_UINT64: a_val = as.integer.value.u64; break;
                default: a_val = 0.0; break;
            }
        }

        if (other.type == VAL_FLOAT64) {
            b_val = other.as.float64_val;
        } else if (other.type == VAL_FLOAT32) {
            b_val = (double)other.as.float32_val;
        } else if (other.type == VAL_INTEGER) {
            switch (other.as.integer.num_type) {
                case NUM_INT8:   b_val = other.as.integer.value.i8; break;
                case NUM_UINT8:  b_val = other.as.integer.value.u8; break;
                case NUM_INT16:  b_val = other.as.integer.value.i16; break;
                case NUM_UINT16: b_val = other.as.integer.value.u16; break;
                case NUM_INT32:  b_val = other.as.integer.value.i32; break;
                case NUM_UINT32: b_val = other.as.integer.value.u32; break;
                case NUM_INT64:  b_val = other.as.integer.value.i64; break;
                case NUM_UINT64: b_val = other.as.integer.value.u64; break;
                default: b_val = 0.0; break;
            }
        }

        return a_val == b_val;
    }

    if (type != other.type) return false;

    switch (type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return as.boolean == other.as.boolean;
        case VAL_STRING:
            return (as.string == other.as.string) || (as.string && other.as.string && *as.string == *other.as.string);
        case VAL_CHAR: return as.character == other.as.character;
        case VAL_ARRAY:
            return as.array == other.as.array;
        case VAL_FUNCTION: return as.function == other.as.function;
        case VAL_NATIVE_FUNCTION: return as.native_function == other.as.native_function;
        case VAL_TABLE: return as.table == other.as.table;
        case VAL_USERDATA:
            return as.userdata.ptr == other.as.userdata.ptr &&
                   as.userdata.type_name == other.as.userdata.type_name;
        case VAL_ENUM:
            return as.enum_val.definition == other.as.enum_val.definition &&
                   as.enum_val.value == other.as.enum_val.value;
        default: return false;
    }
}

bool Value::exactlyEqual(const Value& other) const {
    if (type != other.type) return false;

    switch (type) {
        case VAL_NIL:    return true;
        case VAL_BOOL:   return as.boolean == other.as.boolean;
        case VAL_INTEGER: return as.integer.value.i64 == other.as.integer.value.i64;
        case VAL_FLOAT32: return as.float32_val == other.as.float32_val;
        case VAL_FLOAT64: return as.float64_val == other.as.float64_val;
        case VAL_STRING: return as.string && other.as.string && *as.string == *other.as.string;
        case VAL_CHAR:   return as.character == other.as.character;
        case VAL_ARRAY:  return as.array == other.as.array;
        case VAL_FUNCTION: return as.function == other.as.function;
        case VAL_NATIVE_FUNCTION: return as.native_function == other.as.native_function;
        case VAL_TABLE:  return as.table == other.as.table;
        case VAL_USERDATA:
            return as.userdata.ptr == other.as.userdata.ptr &&
                   as.userdata.type_name == other.as.userdata.type_name;
        case VAL_ENUM:
            return as.enum_val.definition == other.as.enum_val.definition &&
                   as.enum_val.value == other.as.enum_val.value;
        default: return false;
    }
}

void print_value(const Value& value) {
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
            if (value.as.float32_val == (float)(int)value.as.float32_val) {
                printf("%.1f", value.as.float32_val);
            } else {
                printf("%g", value.as.float32_val);
            }
            break;
        case VAL_FLOAT64:
            if (value.as.float64_val == (double)(long long)value.as.float64_val) {
                printf("%.1f", value.as.float64_val);
            } else {
                printf("%g", value.as.float64_val);
            }
            break;
        case VAL_STRING:
            printf("%s", value.as.string ? value.as.string->data : "(null)");
            break;
        case VAL_CHAR:
            printf("'%c'", value.as.character);
            break;
        case VAL_ARRAY:
            if (value.as.array) {
                printf("[");
                for (size_t i = 0; i < value.as.array->length(); i++) {
                    if (i > 0) printf(", ");
                    print_value((*value.as.array)[i]);
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
                value.as.table->print();
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
                printf("%s.%s", value.as.enum_val.definition->name().c_str(), member_name);
            } else {
                printf("%s(%d)", value.as.enum_val.definition->name().c_str(), value.as.enum_val.value);
            }
            break;
        }
        default:
            printf("unknown_value");
            break;
    }
}

char* value_to_string(const Value& value) {
    char* result = NULL;
    char buffer[64];

    switch (value.type) {
        case VAL_NIL:
            result = (char*)malloc(4);
            if (result) strcpy(result, "nil");
            break;
        case VAL_BOOL:
            result = (char*)malloc(6);
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
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_FLOAT32:
            if (value.as.float32_val == (float)(int)value.as.float32_val) {
                snprintf(buffer, sizeof(buffer), "%.1f", value.as.float32_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%g", value.as.float32_val);
            }
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_FLOAT64:
            if (value.as.float64_val == (double)(long long)value.as.float64_val) {
                snprintf(buffer, sizeof(buffer), "%.1f", value.as.float64_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%g", value.as.float64_val);
            }
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_STRING:
            if (value.as.string) {
                const char* str_data = value.as.string->data;
                size_t str_len = value.as.string->length;
                result = (char*)malloc(str_len + 1);
                if (result) strcpy(result, str_data);
            } else {
                result = (char*)malloc(7);
                if (result) strcpy(result, "(null)");
            }
            break;
        case VAL_CHAR:
            result = (char*)malloc(2);
            if (result) {
                result[0] = value.as.character;
                result[1] = '\0';
            }
            break;
        case VAL_FUNCTION:
            snprintf(buffer, sizeof(buffer), "<function>");
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_NATIVE_FUNCTION:
            snprintf(buffer, sizeof(buffer), "<native function>");
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_TABLE:
            snprintf(buffer, sizeof(buffer), "<table>");
            result = (char*)malloc(strlen(buffer) + 1);
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
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_ENUM: {
            const char* member_name = enum_value_name(value);
            if (member_name) {
                snprintf(buffer, sizeof(buffer), "%s.%s", value.as.enum_val.definition->name().c_str(), member_name);
            } else {
                snprintf(buffer, sizeof(buffer), "%s(%d)", value.as.enum_val.definition->name().c_str(), value.as.enum_val.value);
            }
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        }
        default:
            result = (char*)malloc(8);
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

Value make_string_value_from_cstr(MobiusState* state, const char* cstr) {
    if (!state || !cstr) {
        return make_nil_value();
    }
    MobiusString* str = state->stringPool()->intern(cstr);
    return make_string_value(str);
}

Value Value::makeEnum(EnumDefinition* definition, int64_t val) {
    Value value;
    value.type = VAL_ENUM;
    value.as.enum_val.definition = definition->retain();
    value.as.enum_val.value = (int32_t)val;
    return value;
}


TypeConversionResult validate_and_convert_value(const Value& value, NumberType target_type, bool is_annotated, TypeCheckConfig config) {
    TypeConversionResult result = {false, make_nil_value(), NULL, false};

    if (!is_annotated || target_type == NUM_UNKNOWN) {
        result.success = true;
        result.converted_value = value;
        result.was_converted = false;
        return result;
    }

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
            result.error_message = (char*)malloc(256);
            snprintf(result.error_message, 256, "Cannot convert %s to %s",
                    value_type_name(value.type), number_type_name(target_type));
            return result;
        }

        if (!value_fits_in_type(int_value, target_type)) {
            result.error_message = (char*)malloc(256);
            snprintf(result.error_message, 256, "Value %ld out of range for %s",
                    int_value, number_type_name(target_type));
            return result;
        }

        result.converted_value = make_integer_value(target_type, int_value);
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }

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
                result.error_message = (char*)malloc(256);
                snprintf(result.error_message, 256, "Cannot convert %s to %s",
                        value_type_name(value.type), number_type_name(target_type));
                return result;
            }

            result.converted_value = make_float32_value(float32_value);
        } else {
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
                result.error_message = (char*)malloc(256);
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

    result.error_message = mobius_strdup("Unknown target type");
    return result;
}
