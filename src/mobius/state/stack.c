#include "state/stack.h"
#include "state/mobius_state.h"
#include "state/environment.h"
#include "data/value.h"
#include "data/table.h"
#include "data/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Convert stack index to absolute index
// Supports 0-based indexing with negative indices from top
// 0 = bottom, -1 = top
static size_t normalize_index(MobiusState* state, int idx) {
    if (!state || !state->main_context) {
        fprintf(stderr, "FATAL: Invalid state or context\n");
        exit(1);
    }
    
    ExecutionContext* ctx = state->main_context;
    int size = (int)ctx->stack_top;
    
    if (idx < 0) {
        // Negative index: count from top
        idx = size + idx;
    }
    
    if (idx < 0 || idx >= size) {
        fprintf(stderr, "FATAL: Stack index %d out of bounds (stack size: %d)\n", 
                idx < 0 ? idx - size : idx, size);
        exit(1);
    }
    
    return (size_t)idx;
}

// Get value at index (with bounds checking)
static Value* get_value_at(MobiusState* state, int idx) {
    size_t abs_idx = normalize_index(state, idx);
    return &state->main_context->stack[abs_idx];
}

// Fatal error helper
static void fatal_type_error(const char* func, ValueType expected, ValueType actual, int idx) {
    fprintf(stderr, "FATAL: %s() - Expected %s at index %d, got %s\n",
            func, value_type_name(expected), idx, value_type_name(actual));
    exit(1);
}

// Check if conversion is needed and fail in strict mode
static bool check_strict_conversion(MobiusState* state, ValueType from, ValueType to) {
    if (from == to) return false;  // No conversion needed
    
    if (state->config.strict_mode) {
        fprintf(stderr, "FATAL: Type conversion not allowed in strict mode (from %s to %s)\n",
                value_type_name(from), value_type_name(to));
        exit(1);
    }
    
    return true;  // Conversion allowed
}

// ============================================================================
// STACK INSPECTION
// ============================================================================

int mobius_stack_size(MobiusState* state) {
    if (!state || !state->main_context) {
        fprintf(stderr, "FATAL: Invalid state or context\n");
        exit(1);
    }
    return (int)state->main_context->stack_top;
}

ValueType mobius_stack_type(MobiusState* state, int idx) {
    if (!state || !state->main_context) return VAL_NIL;
    
    ExecutionContext* ctx = state->main_context;
    int size = (int)ctx->stack_top;
    
    // Normalize negative indices
    if (idx < 0) {
        idx = size + idx;
    }
    
    // Return VAL_NIL for out of bounds
    if (idx < 0 || idx >= size) {
        return VAL_NIL;
    }
    
    return ctx->stack[idx].type;
}

bool mobius_stack_isNumber(MobiusState* state, int idx) {
    ValueType type = mobius_stack_type(state, idx);
    return type == VAL_INTEGER || type == VAL_FLOAT32 || type == VAL_FLOAT64;
}

bool mobius_stack_isInteger(MobiusState* state, int idx) {
    return mobius_stack_type(state, idx) == VAL_INTEGER;
}

bool mobius_stack_isFloat(MobiusState* state, int idx) {
    ValueType type = mobius_stack_type(state, idx);
    return type == VAL_FLOAT32 || type == VAL_FLOAT64;
}

bool mobius_stack_isString(MobiusState* state, int idx) {
    return mobius_stack_type(state, idx) == VAL_STRING;
}

bool mobius_stack_isBool(MobiusState* state, int idx) {
    return mobius_stack_type(state, idx) == VAL_BOOL;
}

bool mobius_stack_isNil(MobiusState* state, int idx) {
    return mobius_stack_type(state, idx) == VAL_NIL;
}

bool mobius_stack_isTable(MobiusState* state, int idx) {
    return mobius_stack_type(state, idx) == VAL_TABLE;
}

bool mobius_stack_isArray(MobiusState* state, int idx) {
    return mobius_stack_type(state, idx) == VAL_ARRAY;
}

bool mobius_stack_isFunction(MobiusState* state, int idx) {
    ValueType type = mobius_stack_type(state, idx);
    return type == VAL_FUNCTION || type == VAL_NATIVE_FUNCTION;
}

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

static int64_t value_to_int64(Value* val) {
    switch (val->type) {
        case VAL_INTEGER:
            // Extract based on number type
            switch (val->as.integer.num_type) {
                case NUM_INT8:   return (int64_t)val->as.integer.value.i8;
                case NUM_UINT8:  return (int64_t)val->as.integer.value.u8;
                case NUM_INT16:  return (int64_t)val->as.integer.value.i16;
                case NUM_UINT16: return (int64_t)val->as.integer.value.u16;
                case NUM_INT32:  return (int64_t)val->as.integer.value.i32;
                case NUM_UINT32: return (int64_t)val->as.integer.value.u32;
                case NUM_INT64:  return (int64_t)val->as.integer.value.i64;
                case NUM_UINT64: return (int64_t)val->as.integer.value.u64;
                default: return 0;
            }
        case VAL_FLOAT32:
            return (int64_t)val->as.float32_val;
        case VAL_FLOAT64:
            return (int64_t)val->as.float64_val;
        case VAL_BOOL:
            return val->as.boolean ? 1 : 0;
        case VAL_STRING:
            if (val->as.string && val->as.string->data) {
                return strtoll(val->as.string->data, NULL, 10);
            }
            return 0;
        default:
            fprintf(stderr, "FATAL: Cannot convert %s to integer\n", value_type_name(val->type));
            exit(1);
    }
}

static double value_to_double(Value* val) {
    switch (val->type) {
        case VAL_INTEGER:
            // Extract based on number type
            switch (val->as.integer.num_type) {
                case NUM_INT8:   return (double)val->as.integer.value.i8;
                case NUM_UINT8:  return (double)val->as.integer.value.u8;
                case NUM_INT16:  return (double)val->as.integer.value.i16;
                case NUM_UINT16: return (double)val->as.integer.value.u16;
                case NUM_INT32:  return (double)val->as.integer.value.i32;
                case NUM_UINT32: return (double)val->as.integer.value.u32;
                case NUM_INT64:  return (double)val->as.integer.value.i64;
                case NUM_UINT64: return (double)val->as.integer.value.u64;
                default: return 0.0;
            }
        case VAL_FLOAT32:
            return (double)val->as.float32_val;
        case VAL_FLOAT64:
            return val->as.float64_val;
        case VAL_STRING:
            if (val->as.string && val->as.string->data) {
                return strtod(val->as.string->data, NULL);
            }
            return 0.0;
        default:
            fprintf(stderr, "FATAL: Cannot convert %s to float\n", value_type_name(val->type));
            exit(1);
    }
}

static bool value_to_bool(Value* val) {
    switch (val->type) {
        case VAL_NIL:
            return false;
        case VAL_BOOL:
            return val->as.boolean;
        case VAL_INTEGER:
            return value_to_int64(val) != 0;
        case VAL_FLOAT32:
            return val->as.float32_val != 0.0f;
        case VAL_FLOAT64:
            return val->as.float64_val != 0.0;
        case VAL_STRING:
            return val->as.string && val->as.string->data && 
                   val->as.string->data[0] != '\0';
        default:
            return true;  // All other types are truthy
    }
}

static const char* stack_value_to_string(Value* val) {
    static char buffer[256];  // Static buffer for conversions
    
    switch (val->type) {
        case VAL_STRING:
            if (val->as.string && val->as.string->data) {
                return val->as.string->data;
            }
            return "";
        case VAL_INTEGER:
            snprintf(buffer, sizeof(buffer), "%" PRId64, value_to_int64(val));
            return buffer;
        case VAL_FLOAT32:
            snprintf(buffer, sizeof(buffer), "%g", (double)val->as.float32_val);
            return buffer;
        case VAL_FLOAT64:
            snprintf(buffer, sizeof(buffer), "%g", val->as.float64_val);
            return buffer;
        case VAL_BOOL:
            return val->as.boolean ? "true" : "false";
        case VAL_NIL:
            return "nil";
        default:
            snprintf(buffer, sizeof(buffer), "<%s>", value_type_name(val->type));
            return buffer;
    }
}

// ============================================================================
// STACK GETTERS - PERMISSIVE (always convert)
// ============================================================================

int8_t mobius_stack_asInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (int8_t)value_to_int64(val);
}

uint8_t mobius_stack_asUInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (uint8_t)value_to_int64(val);
}

int16_t mobius_stack_asInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (int16_t)value_to_int64(val);
}

uint16_t mobius_stack_asUInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (uint16_t)value_to_int64(val);
}

int32_t mobius_stack_asInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (int32_t)value_to_int64(val);
}

uint32_t mobius_stack_asUInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (uint32_t)value_to_int64(val);
}

int64_t mobius_stack_asInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return value_to_int64(val);
}

uint64_t mobius_stack_asUInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (uint64_t)value_to_int64(val);
}

float mobius_stack_asFloat32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return (float)value_to_double(val);
}

double mobius_stack_asFloat64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return value_to_double(val);
}

bool mobius_stack_asBool(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return value_to_bool(val);
}

const char* mobius_stack_asString(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return stack_value_to_string(val);
}

// ============================================================================
// STACK GETTERS - STRICT (respects strict_types pragma)
// ============================================================================

int8_t mobius_stack_getInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (int8_t)value_to_int64(val);
}

uint8_t mobius_stack_getUInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (uint8_t)value_to_int64(val);
}

int16_t mobius_stack_getInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (int16_t)value_to_int64(val);
}

uint16_t mobius_stack_getUInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (uint16_t)value_to_int64(val);
}

int32_t mobius_stack_getInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (int32_t)value_to_int64(val);
}

uint32_t mobius_stack_getUInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (uint32_t)value_to_int64(val);
}

int64_t mobius_stack_getInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return value_to_int64(val);
}

uint64_t mobius_stack_getUInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_INTEGER) {
        check_strict_conversion(state, val->type, VAL_INTEGER);
    }
    return (uint64_t)value_to_int64(val);
}

float mobius_stack_getFloat32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_FLOAT32 && val->type != VAL_FLOAT64) {
        check_strict_conversion(state, val->type, VAL_FLOAT32);
    }
    return (float)value_to_double(val);
}

double mobius_stack_getFloat64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_FLOAT32 && val->type != VAL_FLOAT64) {
        check_strict_conversion(state, val->type, VAL_FLOAT64);
    }
    return value_to_double(val);
}

bool mobius_stack_getBool(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_BOOL) {
        check_strict_conversion(state, val->type, VAL_BOOL);
    }
    return value_to_bool(val);
}

const char* mobius_stack_getString(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (val->type != VAL_STRING) {
        check_strict_conversion(state, val->type, VAL_STRING);
    }
    return stack_value_to_string(val);
}

// ============================================================================
// STACK PUSH OPERATIONS
// ============================================================================

void mobius_stack_pushInt8(MobiusState* state, int8_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_INT8, value));
}

void mobius_stack_pushUInt8(MobiusState* state, uint8_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_UINT8, value));
}

void mobius_stack_pushInt16(MobiusState* state, int16_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_INT16, value));
}

void mobius_stack_pushUInt16(MobiusState* state, uint16_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_UINT16, value));
}

void mobius_stack_pushInt32(MobiusState* state, int32_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_INT32, value));
}

void mobius_stack_pushUInt32(MobiusState* state, uint32_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_UINT32, value));
}

void mobius_stack_pushInt64(MobiusState* state, int64_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_INT64, value));
}

void mobius_stack_pushUInt64(MobiusState* state, uint64_t value) {
    ctx_push(state->main_context, make_integer_value(NUM_UINT64, value));
}

void mobius_stack_pushFloat32(MobiusState* state, float value) {
    ctx_push(state->main_context, make_float32_value(value));
}

void mobius_stack_pushFloat64(MobiusState* state, double value) {
    ctx_push(state->main_context, make_float_value(value));
}

void mobius_stack_pushBool(MobiusState* state, bool value) {
    ctx_push(state->main_context, make_bool_value(value));
}

void mobius_stack_pushString(MobiusState* state, const char* str) {
    if (!str) {
        ctx_push(state->main_context, make_nil_value());
        return;
    }
    ctx_push(state->main_context, make_string_value_from_cstr(state, str));
}

void mobius_stack_pushNil(MobiusState* state) {
    ctx_push(state->main_context, make_nil_value());
}

void mobius_stack_pushNewTable(MobiusState* state, size_t capacity) {
    Table* table = create_table(state, capacity == 0 ? 16 : capacity);
    if (!table) {
        fprintf(stderr, "FATAL: Failed to create table\n");
        exit(1);
    }
    ctx_push(state->main_context, make_table_value(table));
}

void mobius_stack_pushNewArray(MobiusState* state, size_t capacity) {
    ArrayValue* array = array_create(capacity == 0 ? 8 : capacity);
    if (!array) {
        fprintf(stderr, "FATAL: Failed to create array\n");
        exit(1);
    }
    ctx_push(state->main_context, make_array_value(array));
}

// ============================================================================
// VARIABLE OPERATIONS
// ============================================================================

void mobius_stack_getVariable(MobiusState* state, const char* name) {
    if (!name) {
        fprintf(stderr, "FATAL: mobius_stack_getVariable() - NULL variable name\n");
        exit(1);
    }
    
    bool found = false;
    Value val = get_variable(state->main_context->current_env, name, &found);
    
    if (!found) {
        ctx_push(state->main_context, make_nil_value());
    } else {
        ctx_push(state->main_context, val);
    }
}

void mobius_stack_getGlobal(MobiusState* state, const char* name) {
    if (!name) {
        fprintf(stderr, "FATAL: mobius_stack_getGlobal() - NULL variable name\n");
        exit(1);
    }
    
    bool found = false;
    Value val = get_variable(state->global_env, name, &found);
    
    if (!found) {
        ctx_push(state->main_context, make_nil_value());
    } else {
        ctx_push(state->main_context, val);
    }
}

void mobius_stack_setVariable(MobiusState* state, const char* name) {
    if (!name) {
        fprintf(stderr, "FATAL: mobius_stack_setVariable() - NULL variable name\n");
        exit(1);
    }
    
    if (state->main_context->stack_top == 0) {
        fprintf(stderr, "FATAL: mobius_stack_setVariable() - Stack is empty\n");
        exit(1);
    }
    
    Value val = ctx_pop(state->main_context);
    define_variable(state->main_context->current_env, name, val);
}

void mobius_stack_setGlobal(MobiusState* state, const char* name) {
    if (!name) {
        fprintf(stderr, "FATAL: mobius_stack_setGlobal() - NULL variable name\n");
        exit(1);
    }
    
    if (state->main_context->stack_top == 0) {
        fprintf(stderr, "FATAL: mobius_stack_setGlobal() - Stack is empty\n");
        exit(1);
    }
    
    Value val = ctx_pop(state->main_context);
    define_variable(state->global_env, name, val);
}

// ============================================================================
// TABLE OPERATIONS
// ============================================================================

void mobius_stack_setTableField(MobiusState* state, int table_idx, const char* key) {
    if (!key) {
        fprintf(stderr, "FATAL: mobius_stack_setTableField() - NULL key\n");
        exit(1);
    }
    
    if (state->main_context->stack_top == 0) {
        fprintf(stderr, "FATAL: mobius_stack_setTableField() - Stack is empty\n");
        exit(1);
    }
    
    Value* table_val = get_value_at(state, table_idx);
    if (table_val->type != VAL_TABLE) {
        fatal_type_error("mobius_stack_setTableField", VAL_TABLE, table_val->type, table_idx);
    }
    
    Value key_val = make_string_value_from_cstr(state, key);
    Value field_val = ctx_pop(state->main_context);
    
    table_set(table_val->as.table, key_val, field_val);
    
    free_value(key_val);
}

void mobius_stack_getTableField(MobiusState* state, int table_idx, const char* key) {
    if (!key) {
        fprintf(stderr, "FATAL: mobius_stack_getTableField() - NULL key\n");
        exit(1);
    }
    
    Value* table_val = get_value_at(state, table_idx);
    if (table_val->type != VAL_TABLE) {
        fatal_type_error("mobius_stack_getTableField", VAL_TABLE, table_val->type, table_idx);
    }
    
    Value key_val = make_string_value_from_cstr(state, key);
    Value result = table_get(table_val->as.table, key_val);
    
    ctx_push(state->main_context, result);
    
    free_value(key_val);
}

// ============================================================================
// ARRAY OPERATIONS
// ============================================================================

void mobius_stack_setArrayElement(MobiusState* state, int array_idx, size_t element_idx) {
    if (state->main_context->stack_top == 0) {
        fprintf(stderr, "FATAL: mobius_stack_setArrayElement() - Stack is empty\n");
        exit(1);
    }
    
    Value* array_val = get_value_at(state, array_idx);
    if (array_val->type != VAL_ARRAY) {
        fatal_type_error("mobius_stack_setArrayElement", VAL_ARRAY, array_val->type, array_idx);
    }
    
    Value element_val = ctx_pop(state->main_context);
    array_set(array_val->as.array, element_idx, element_val);
}

void mobius_stack_getArrayElement(MobiusState* state, int array_idx, size_t element_idx) {
    Value* array_val = get_value_at(state, array_idx);
    if (array_val->type != VAL_ARRAY) {
        fatal_type_error("mobius_stack_getArrayElement", VAL_ARRAY, array_val->type, array_idx);
    }
    
    Value result = array_get(array_val->as.array, element_idx);
    ctx_push(state->main_context, result);
}

size_t mobius_stack_getArrayLength(MobiusState* state, int array_idx) {
    Value* array_val = get_value_at(state, array_idx);
    if (array_val->type != VAL_ARRAY) {
        return 0;
    }
    
    return array_val->as.array->length;
}

// ============================================================================
// STACK MANIPULATION
// ============================================================================

void mobius_stack_pop(MobiusState* state, int count) {
    if (count < 0) {
        fprintf(stderr, "FATAL: mobius_stack_pop() - Invalid count %d\n", count);
        exit(1);
    }
    
    if ((size_t)count > state->main_context->stack_top) {
        fprintf(stderr, "FATAL: mobius_stack_pop() - Cannot pop %d values from stack of size %zu\n",
                count, state->main_context->stack_top);
        exit(1);
    }
    
    for (int i = 0; i < count; i++) {
        ctx_pop(state->main_context);
    }
}

void mobius_stack_copy(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    
    // Use copy_value which handles reference counting properly
    Value copy = copy_value(*val);
    
    ctx_push(state->main_context, copy);
}

// ============================================================================
// PUBLIC API — mobius_error
// ============================================================================

int mobius_error(MobiusState* state, const char* message) {
    mobius_set_error(state, MOBIUS_ERROR_RUNTIME, message, NULL, 0, 0, NULL);
    return -1;
}

// ============================================================================
// PUBLIC API — Native function registration
// ============================================================================

void mobius_register_function(MobiusState* state, const char* name,
                              MobiusCFunction func) {
    if (!state || !name || !func) return;
    define_variable(state->global_env, name, make_native_function_value(func));
}

// ============================================================================
// PUBLIC API — Userdata
// ============================================================================

void mobius_stack_pushUserdata(MobiusState* state, void* ptr,
                               void (*destructor)(void*),
                               const char* type_name, size_t size) {
    Value val = make_userdata_value(ptr, destructor, type_name, size);
    ctx_push(state->main_context, val);
}

bool mobius_stack_isUserdata(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return val && val->type == VAL_USERDATA;
}

void* mobius_stack_getUserdata(MobiusState* state, int idx, const char** out_type_name) {
    Value* val = get_value_at(state, idx);
    if (!val || val->type != VAL_USERDATA) return NULL;
    if (out_type_name) *out_type_name = val->as.userdata.type_name;
    return val->as.userdata.ptr;
}

