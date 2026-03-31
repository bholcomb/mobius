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
// Value refcount slow path — release (called only for heap-allocated types)
// ============================================================================

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
                    free(func->upvalues);
                    free(func);
                }
            }
            break;
        case VAL_TABLE:
            if (as.table) as.table->release();
            break;
        case VAL_USERDATA:
            if (as.userdata) {
                if (--as.userdata->ref_count <= 0) {
                    if (as.userdata->destructor && as.userdata->ptr)
                        as.userdata->destructor(as.userdata->ptr);
                    free(as.userdata);
                }
            }
            break;
        case VAL_ENUM:
            if (as.enum_def) {
                as.enum_def->release();
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

Value make_string_value(MobiusString* string) {
    Value value;
    value.type = VAL_STRING;
    value.as.string = string;
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
    UserdataObject* ud = (UserdataObject*)malloc(sizeof(UserdataObject));
    ud->ref_count  = 1;
    ud->ptr        = ptr;
    ud->destructor = destructor;
    ud->type_name  = type_name;
    ud->size       = size;
    Value value;
    value.type = VAL_USERDATA;
    value.as.userdata = ud;
    return value;
}

// ============================================================================
// Value utility functions
// ============================================================================

bool Value::operator==(const Value& other) const {
    // Numeric cross-type equality: int64 == uint64 == float64
    bool a_numeric = (type == VAL_INT64 || type == VAL_UINT64 || type == VAL_FLOAT64);
    bool b_numeric = (other.type == VAL_INT64 || other.type == VAL_UINT64 || other.type == VAL_FLOAT64);
    if (a_numeric && b_numeric) {
        auto to_double = [](const Value& v) -> double {
            if (v.type == VAL_FLOAT64)  return v.as.double_val;
            if (v.type == VAL_UINT64)   return (double)v.as.u64;
            return (double)v.as.i64;
        };
        return to_double(*this) == to_double(other);
    }

    if (type != other.type) return false;

    switch (type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return as.boolean == other.as.boolean;
        case VAL_STRING:
            return (as.string == other.as.string) || (as.string && other.as.string && *as.string == *other.as.string);
        case VAL_CHAR: return as.character == other.as.character;
        case VAL_ARRAY: return as.array == other.as.array;
        case VAL_FUNCTION: return as.function == other.as.function;
        case VAL_NATIVE_FUNCTION: return as.native_function == other.as.native_function;
        case VAL_TABLE: return as.table == other.as.table;
        case VAL_USERDATA:
            return as.userdata && other.as.userdata &&
                   as.userdata->ptr == other.as.userdata->ptr &&
                   as.userdata->type_name == other.as.userdata->type_name;
        case VAL_ENUM:
            return as.enum_def == other.as.enum_def && aux == other.aux;
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
        case VAL_INT64:
            printf("%ld", value.as.i64);
            break;
        case VAL_UINT64:
            printf("%lu", value.as.u64);
            break;
        case VAL_FLOAT64:
            if (value.as.double_val == (double)(long long)value.as.double_val) {
                printf("%.1f", value.as.double_val);
            } else {
                printf("%g", value.as.double_val);
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
                printf("<func %s>", value.as.function->name ? value.as.function->name->data : "anonymous");
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
            if (value.as.userdata && value.as.userdata->ptr) {
                printf("<%s userdata %p>",
                       value.as.userdata->type_name ? value.as.userdata->type_name : "unknown",
                       value.as.userdata->ptr);
            } else {
                printf("<userdata (null)>");
            }
            break;
        case VAL_ENUM: {
            const char* member_name = enum_value_name(value);
            if (member_name) {
                printf("%s.%s", value.as.enum_def->name().c_str(), member_name);
            } else {
                printf("%s(%d)", value.as.enum_def->name().c_str(), value.aux);
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
        case VAL_INT64:
            snprintf(buffer, sizeof(buffer), "%ld", value.as.i64);
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_UINT64:
            snprintf(buffer, sizeof(buffer), "%lu", value.as.u64);
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_FLOAT64:
            if (value.as.double_val == (double)(long long)value.as.double_val) {
                snprintf(buffer, sizeof(buffer), "%.1f", value.as.double_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%g", value.as.double_val);
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
            if (value.as.userdata && value.as.userdata->ptr) {
                snprintf(buffer, sizeof(buffer), "<%s userdata %p>",
                         value.as.userdata->type_name ? value.as.userdata->type_name : "unknown",
                         value.as.userdata->ptr);
            } else {
                strcpy(buffer, "<userdata (null)>");
            }
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_ENUM: {
            const char* member_name = enum_value_name(value);
            if (member_name) {
                snprintf(buffer, sizeof(buffer), "%s.%s", value.as.enum_def->name().c_str(), member_name);
            } else {
                snprintf(buffer, sizeof(buffer), "%s(%d)", value.as.enum_def->name().c_str(), value.aux);
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
        case VAL_INT64: return "integer";
        case VAL_UINT64:  return "uint64";
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
    value.as.enum_def = definition->retain();
    value.aux = (int32_t)val;
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
        bool conversion_needed = false;

        if (value.type == VAL_INT64) {
            int_value = value.as.i64;
            conversion_needed = (target_type == NUM_UINT64);
        } else if (value.type == VAL_UINT64) {
            int_value = (int64_t)value.as.u64;
            conversion_needed = (target_type == NUM_INT64);
        } else if (value.type == VAL_FLOAT64) {
            if (config.strict_mode) {
                result.error_message = mobius_strdup("Cannot convert float to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.double_val;
            conversion_needed = true;
        } else {
            result.error_message = (char*)malloc(256);
            snprintf(result.error_message, 256, "Cannot convert %s to %s",
                    value_type_name(value.type), number_type_name(target_type));
            return result;
        }

        result.converted_value = make_integer_value(target_type, int_value);
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }

    if (is_float_type(target_type)) {
        double float_value = 0.0;
        bool conversion_needed = false;

        if (value.type == VAL_FLOAT64) {
            float_value = value.as.double_val;
        } else if (value.type == VAL_INT64 || value.type == VAL_UINT64) {
            if (config.strict_mode) {
                result.error_message = mobius_strdup("Cannot convert integer to float in strict mode");
                return result;
            }
            float_value = (value.type == VAL_UINT64) ? (double)value.as.u64 : (double)value.as.i64;
            conversion_needed = true;
        } else {
            result.error_message = (char*)malloc(256);
            snprintf(result.error_message, 256, "Cannot convert %s to %s",
                    value_type_name(value.type), number_type_name(target_type));
            return result;
        }

        result.converted_value = make_float_value(float_value);
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }

    result.error_message = mobius_strdup("Unknown target type");
    return result;
}

Value increment_integer(Value val, bool is_increment, bool* success) {
    *success = false;
    if (val.type == VAL_INT64) {
        *success = true;
        return make_int64_value(val.as.i64 + (is_increment ? 1 : -1));
    }
    if (val.type == VAL_UINT64) {
        *success = true;
        return make_uint64_value(val.as.u64 + (is_increment ? 1u : (uint64_t)-1));
    }
    return make_nil_value();
}
