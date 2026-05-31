#include "data/value.h"
#include "data/channel.h"
#include "data/enum.h"
#include "data/future.h"
#include "data/buffer.h"
#include "data/array_slice.h"
#include "data/shared_cell.h"
#include "data/table.h"
#include "data/array.h"
#include "data/function.h"
#include "frontend/ast.h"
#include "state/mobius_state.h"
#include "util/utility.h"

#include <charconv>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include <string.h>
#include <string>
#include <unordered_map>

// ============================================================================
// Value refcount slow path — release (called only for heap-allocated types)
// ============================================================================

void Value::releaseRefSlow() {
    switch (type) {
        case VAL_ARRAY:
            if (as.array) as.array->release();
            break;
        case VAL_FUNCTION:
            if (as.function) {
                MobiusFunction* func = as.function;
                if (func->ref_count.fetch_sub(1, std::memory_order_acq_rel) <= 1) {
                    delete[] func->param_names;
                    if (func->body) {
                        for (size_t i = 0; i < func->body_count; i++) {
                            if (func->body[i]) ast_release_stmt(func->body[i]);
                        }
                        delete[] func->body;
                    }
                    delete[] func->upvalues;
                    delete func;
                }
            }
            break;
        case VAL_TABLE:
            if (as.table) as.table->release();
            break;
        case VAL_USERDATA:
            if (as.userdata) {
                if (as.userdata->ref_count.fetch_sub(1, std::memory_order_acq_rel) <= 1) {
                    if (as.userdata->destructor && as.userdata->ptr)
                        as.userdata->destructor(as.userdata->ptr);
                    delete as.userdata;
                }
            }
            break;
        case VAL_ENUM:
            if (as.enum_def) {
                as.enum_def->release();
            }
            break;
        case VAL_FUTURE:
            if (as.future) {
                ((RefCounted*)as.future)->release();
            }
            break;
        case VAL_ARRAY_SLICE:
            if (as.array_slice) {
                ((RefCounted*)as.array_slice)->release();
            }
            break;
        case VAL_CHANNEL:
            if (as.channel) {
                ((RefCounted*)as.channel)->release();
            }
            break;
        case VAL_SHARED_CELL:
            if (as.shared_cell) {
                ((RefCounted*)as.shared_cell)->release();
            }
            break;
        case VAL_BUFFER:
            if (as.buffer) {
                ((RefCounted*)as.buffer)->release();
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

Value make_shared_cell_value(SharedCell* shared_cell) {
    Value value;
    value.type = VAL_SHARED_CELL;
    value.as.shared_cell = shared_cell;
    return value;
}

Value make_buffer_value(BufferValue* buffer) {
    Value value;
    value.type = VAL_BUFFER;
    value.as.buffer = buffer;
    if (buffer && buffer->isFixed()) value.flags |= VAL_FLAG_FIXED;
    return value;
}

Value make_userdata_value(MobiusState* state, void* ptr, UserdataDestructor destructor,
                         const char* type_name, size_t size) {
    UserdataObject* ud = new UserdataObject();
    ud->ref_count.store(1, std::memory_order_relaxed);
    ud->ptr        = ptr;
    ud->destructor = destructor;
    ud->type_tag   = (state && type_name) ? state->stringPool()->intern(type_name) : nullptr;
    ud->type_name  = ud->type_tag ? ud->type_tag->data : type_name;
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
        case VAL_FUTURE:
            return as.future == other.as.future;
        case VAL_ARRAY_SLICE:
            return as.array_slice == other.as.array_slice;
        case VAL_CHANNEL:
            return as.channel == other.as.channel;
        case VAL_SHARED_CELL:
            return as.shared_cell == other.as.shared_cell;
        case VAL_BUFFER:
            return as.buffer == other.as.buffer;
        default: return false;
    }
}

namespace {
Value deep_copy_value_impl(const Value& value, std::unordered_map<const void*, Value>& memo) {
    switch (value.type) {
        case VAL_ARRAY: {
            if (!value.as.array) return make_nil_value();
            auto it = memo.find(value.as.array);
            if (it != memo.end()) return it->second;

            ArrayValue* clone = new ArrayValue(value.as.array->length());
            Value copy = make_array_value(clone);
            copy.flags = (int8_t)(value.flags & ~VAL_FLAG_SHARED);
            copy.aux = value.aux;
            memo.emplace(value.as.array, copy);

            for (size_t i = 0; i < value.as.array->length(); i++) {
                clone->push(deep_copy_value_impl((*value.as.array)[i], memo));
            }
            return copy;
        }
        case VAL_TABLE: {
            if (!value.as.table) return make_nil_value();
            auto it = memo.find(value.as.table);
            if (it != memo.end()) return it->second;

            Table* clone = new Table(value.as.table->getState(), value.as.table->entries().size());
            Value copy = make_table_value(clone);
            copy.flags = (int8_t)(value.flags & ~VAL_FLAG_SHARED);
            copy.aux = value.aux;
            memo.emplace(value.as.table, copy);

            if (Table* mt = value.as.table->getMetatable()) {
                Value mt_copy = deep_copy_value_impl(make_table_value(mt), memo);
                if (mt_copy.type == VAL_TABLE && mt_copy.as.table) {
                    clone->setMetatable(mt_copy.as.table);
                }
            }

            for (const auto& entry : value.as.table->entries()) {
                if (!entry.occupied()) continue;
                clone->set(deep_copy_value_impl(entry.key, memo),
                           deep_copy_value_impl(entry.value, memo));
            }
            return copy;
        }
        case VAL_BUFFER: {
            if (!value.as.buffer) return make_nil_value();
            BufferValue* clone = value.as.buffer->clone();
            Value copy = make_buffer_value(clone);
            copy.flags = value.flags;
            return copy;
        }
        default: {
            Value copy = value;
            copy.flags = (int8_t)(value.flags & ~VAL_FLAG_SHARED);
            return copy;
        }
    }
}
} // namespace

Value deep_copy_value_for_spawn(const Value& value) {
    std::unordered_map<const void*, Value> memo;
    return deep_copy_value_impl(value, memo);
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
        case VAL_FUTURE:
            printf("<future %p>", (void*)value.as.future);
            break;
        case VAL_ARRAY_SLICE:
            if (value.as.array_slice) {
                printf("<slice len=%zu>", value.as.array_slice->length());
            } else {
                printf("<slice (null)>");
            }
            break;
        case VAL_CHANNEL:
            printf("<channel %p>", (void*)value.as.channel);
            break;
        case VAL_SHARED_CELL:
            if (value.as.shared_cell) {
                print_value(value.as.shared_cell->load());
            } else {
                printf("<shared (null)>");
            }
            break;
        case VAL_BUFFER:
            if (value.as.buffer) {
                printf("<buffer len=%zu%s>", value.as.buffer->size(),
                       value.as.buffer->isFixed() ? " fixed" : "");
            } else {
                printf("<buffer (null)>");
            }
            break;
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
        case VAL_ARRAY:
            snprintf(buffer, sizeof(buffer), "<array>");
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
        case VAL_FUTURE:
            snprintf(buffer, sizeof(buffer), "<future %p>", (void*)value.as.future);
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_ARRAY_SLICE:
            snprintf(buffer, sizeof(buffer), "<slice len=%zu>",
                     value.as.array_slice ? value.as.array_slice->length() : 0);
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_CHANNEL:
            snprintf(buffer, sizeof(buffer), "<channel %p>", (void*)value.as.channel);
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_SHARED_CELL:
            if (value.as.shared_cell) {
                return value_to_string(value.as.shared_cell->load());
            }
            result = (char*)malloc(15);
            if (result) strcpy(result, "<shared null>");
            break;
        case VAL_BUFFER:
            if (value.as.buffer) {
                snprintf(buffer, sizeof(buffer), "<buffer len=%zu%s>",
                         value.as.buffer->size(),
                         value.as.buffer->isFixed() ? " fixed" : "");
            } else {
                strcpy(buffer, "<buffer (null)>");
            }
            result = (char*)malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
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
        case VAL_INT64: return "int64";
        case VAL_UINT64:  return "uint64";
        case VAL_FLOAT64: return "float64";
        case VAL_STRING: return "string";
        case VAL_CHAR: return "char";
        case VAL_ARRAY: return "array";
        case VAL_FUNCTION: return "function";
        case VAL_NATIVE_FUNCTION: return "function";
        case VAL_TABLE: return "table";
        case VAL_USERDATA: return "userdata";
        case VAL_ENUM: return "enum";
        case VAL_FUTURE: return "future";
        case VAL_ARRAY_SLICE: return "array_slice";
        case VAL_CHANNEL: return "channel";
        case VAL_SHARED_CELL: return "shared";
        case VAL_BUFFER: return "buffer";
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

MobiusString* value_to_interned_string(MobiusState* state, const Value& value) {
    if (!state) return nullptr;

    char buffer[128];
    auto pool = state->stringPool();
    const auto& common = state->commonStrings();
    if (!pool) return nullptr;

    switch (value.type) {
        case VAL_NIL:
            return common.nil;
        case VAL_BOOL:
            return value.as.boolean ? common.true_value : common.false_value;
        case VAL_INT64: {
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value.as.i64);
            return (ec == std::errc()) ? pool->intern(buffer, (size_t)(ptr - buffer)) : nullptr;
        }
        case VAL_UINT64: {
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value.as.u64);
            return (ec == std::errc()) ? pool->intern(buffer, (size_t)(ptr - buffer)) : nullptr;
        }
        case VAL_FLOAT64: {
            int len = 0;
            if (value.as.double_val == (double)(long long)value.as.double_val) {
                len = snprintf(buffer, sizeof(buffer), "%.1f", value.as.double_val);
            } else {
                len = snprintf(buffer, sizeof(buffer), "%g", value.as.double_val);
            }
            return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
        }
        case VAL_STRING:
            return value.as.string ? value.as.string : common.null_string;
        case VAL_CHAR:
            buffer[0] = value.as.character;
            buffer[1] = '\0';
            return pool->intern(buffer, 1);
        case VAL_FUNCTION:
            return common.function;
        case VAL_NATIVE_FUNCTION:
            return common.native_function;
        case VAL_TABLE:
            return common.table;
        case VAL_ARRAY:
            return common.array;
        case VAL_USERDATA:
            if (value.as.userdata && value.as.userdata->ptr) {
                int len = snprintf(buffer, sizeof(buffer), "<%s userdata %p>",
                                   value.as.userdata->type_name ? value.as.userdata->type_name : "unknown",
                                   value.as.userdata->ptr);
                return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
            }
            return common.userdata_null;
        case VAL_ENUM: {
            const char* member_name = enum_value_name(value);
            if (member_name) {
                std::string text = value.as.enum_def->name();
                text.push_back('.');
                text += member_name;
                return pool->intern(text.c_str(), text.size());
            }
            int len = snprintf(buffer, sizeof(buffer), "%s(%d)",
                               value.as.enum_def->name().c_str(), value.aux);
            return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
        }
        case VAL_FUTURE: {
            int len = snprintf(buffer, sizeof(buffer), "<future %p>", (void*)value.as.future);
            return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
        }
        case VAL_ARRAY_SLICE: {
            int len = snprintf(buffer, sizeof(buffer), "<slice len=%zu>",
                               value.as.array_slice ? value.as.array_slice->length() : 0);
            return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
        }
        case VAL_CHANNEL: {
            int len = snprintf(buffer, sizeof(buffer), "<channel %p>", (void*)value.as.channel);
            return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
        }
        case VAL_SHARED_CELL:
            if (value.as.shared_cell) {
                return value_to_interned_string(state, value.as.shared_cell->load());
            }
            return common.shared_null;
        case VAL_BUFFER:
            if (value.as.buffer) {
                int len = snprintf(buffer, sizeof(buffer), "<buffer len=%zu%s>",
                                   value.as.buffer->size(),
                                   value.as.buffer->isFixed() ? " fixed" : "");
                return (len >= 0) ? pool->intern(buffer, (size_t)len) : nullptr;
            }
            return common.buffer_null;
        default:
            return common.unknown;
    }
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
            if (!result.error_message) {
                result.error_message = mobius_strdup("Out of memory formatting conversion error");
                return result;
            }
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
            if (!result.error_message) {
                result.error_message = mobius_strdup("Out of memory formatting conversion error");
                return result;
            }
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
        if (is_increment) {
            if (val.as.i64 == std::numeric_limits<int64_t>::max()) return make_nil_value();
            *success = true;
            return make_int64_value(val.as.i64 + 1);
        }
        if (val.as.i64 == std::numeric_limits<int64_t>::min()) return make_nil_value();
        *success = true;
        return make_int64_value(val.as.i64 - 1);
    }
    if (val.type == VAL_UINT64) {
        if (is_increment) {
            if (val.as.u64 == std::numeric_limits<uint64_t>::max()) return make_nil_value();
            *success = true;
            return make_uint64_value(val.as.u64 + 1u);
        }
        if (val.as.u64 == 0) return make_nil_value();
        *success = true;
        return make_uint64_value(val.as.u64 - 1u);
    }
    return make_nil_value();
}
