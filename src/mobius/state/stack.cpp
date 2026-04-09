#include <mobius/mobius_plugin.h>
#include "state/stack.h"
#include "state/mobius_state.h"
#include "vm/vm.h"
#include "data/shared_cell.h"
#include "data/value.h"
#include "data/table.h"
#include "data/array.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <new>

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static NativeCallContext* get_nctx(MobiusState* state) {
    NativeCallContext* nctx = state->nativeContext();
    if (!nctx) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_* called without an available VM context",
                        "Ensure the state has an active VM before calling stack functions",
                        0, 0, nullptr);
        return nullptr;
    }
    return nctx;
}

static int normalize_index(NativeCallContext* nctx, int idx, bool* out_ok) {
    int size = nctx->top - nctx->base;

    if (idx < 0) {
        idx = size + idx;
    }

    if (idx < 0 || idx >= size) {
        *out_ok = false;
        return 0;
    }

    *out_ok = true;
    return nctx->base + idx;
}

static Value* get_value_at(MobiusState* state, int idx) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return nullptr;
    bool ok;
    int abs = normalize_index(nctx, idx, &ok);
    if (!ok) {
        int size = nctx->top - nctx->base;
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Stack index out of bounds (size: %d, idx: %d)", size, idx);
        state->setError(MOBIUS_ERROR_ARGUMENT, buf, nullptr, 0, 0, nullptr);
        return nullptr;
    }
    return &nctx->registers[abs];
}

static bool check_strict_conversion(MobiusState* state, ValueType from, ValueType to) {
    if (from == to) return false;

    bool strict = state->config().strict_mode;
    if (state->activeVM()) strict = state->activeVM()->strict_mode_;
    if (strict) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Type conversion not allowed in strict mode (from %s to %s)",
                 value_type_name(from), value_type_name(to));
        state->setError(MOBIUS_ERROR_TYPE, buf, nullptr, 0, 0, nullptr);
        return false;
    }

    return true;
}

// ============================================================================
// PUBLIC C API — all functions below have extern "C" linkage
// ============================================================================

extern "C" {

// ============================================================================
// STACK INSPECTION
// ============================================================================

int mobius_stack_size(MobiusState* state) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return 0;
    return nctx->top - nctx->base;
}

} // extern "C" (temporarily close for internal helper)

static ValueType stack_get_internal_type(MobiusState* state, int idx) {
    NativeCallContext* nctx = state->nativeContext();
    if (!nctx) return VAL_NIL;

    int size = nctx->top - nctx->base;
    if (idx < 0) idx = size + idx;
    if (idx < 0 || idx >= size) return VAL_NIL;

    return nctx->registers[nctx->base + idx].type;
}

static MobiusValueType internal_to_public_type(ValueType t) {
    return (MobiusValueType)t;
}

extern "C" {

MobiusValueType mobius_stack_type(MobiusState* state, int idx) {
    return internal_to_public_type(stack_get_internal_type(state, idx));
}

bool mobius_stack_isNumber(MobiusState* state, int idx) {
    ValueType type = stack_get_internal_type(state, idx);
    return type == VAL_INT64 || type == VAL_FLOAT64;
}

bool mobius_stack_isInteger(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_INT64;
}

bool mobius_stack_isFloat(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_FLOAT64;
}

bool mobius_stack_isString(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_STRING;
}

bool mobius_stack_isBool(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_BOOL;
}

bool mobius_stack_isNil(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_NIL;
}

bool mobius_stack_isTable(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_TABLE;
}

bool mobius_stack_isArray(MobiusState* state, int idx) {
    return stack_get_internal_type(state, idx) == VAL_ARRAY;
}

bool mobius_stack_isFunction(MobiusState* state, int idx) {
    ValueType type = stack_get_internal_type(state, idx);
    return type == VAL_FUNCTION || type == VAL_NATIVE_FUNCTION;
}

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

static bool value_to_int64(Value* val, int64_t* out) {
    if (val->type == VAL_SHARED_CELL && val->as.shared_cell) {
        Value inner = val->as.shared_cell->load();
        return value_to_int64(&inner, out);
    }
    switch (val->type) {
        case VAL_INT64:
            *out = val->as.i64;
            return true;
        case VAL_UINT64:
            *out = (int64_t)val->as.u64;
            return true;
        case VAL_FLOAT64:
            *out = (int64_t)val->as.double_val;
            return true;
        case VAL_BOOL:
            *out = val->as.boolean ? 1 : 0;
            return true;
        case VAL_STRING:
            if (val->as.string && val->as.string->data) {
                *out = strtoll(val->as.string->data, NULL, 10);
                return true;
            }
            *out = 0;
            return true;
        default:
            return false;
    }
}

static bool value_to_double(Value* val, double* out) {
    if (val->type == VAL_SHARED_CELL && val->as.shared_cell) {
        Value inner = val->as.shared_cell->load();
        return value_to_double(&inner, out);
    }
    switch (val->type) {
        case VAL_INT64:
            *out = (double)val->as.i64;
            return true;
        case VAL_UINT64:
            *out = (double)val->as.u64;
            return true;
        case VAL_FLOAT64:
            *out = val->as.double_val;
            return true;
        case VAL_STRING:
            if (val->as.string && val->as.string->data) {
                *out = strtod(val->as.string->data, NULL);
                return true;
            }
            *out = 0.0;
            return true;
        default:
            return false;
    }
}

static bool value_to_bool(Value* val) {
    if (val->type == VAL_SHARED_CELL && val->as.shared_cell) {
        Value inner = val->as.shared_cell->load();
        return value_to_bool(&inner);
    }
    switch (val->type) {
        case VAL_NIL:    return false;
        case VAL_BOOL:   return val->as.boolean;
        case VAL_INT64:  return val->as.i64 != 0;
        case VAL_UINT64: return val->as.u64 != 0;
        case VAL_FLOAT64:return val->as.double_val != 0.0;
        case VAL_STRING:
            return val->as.string && val->as.string->data &&
                   val->as.string->data[0] != '\0';
        default:
            return true;
    }
}

static const char* stack_value_to_string(Value* val) {
    static thread_local char buffer[256];

    if (val->type == VAL_SHARED_CELL && val->as.shared_cell) {
        Value inner = val->as.shared_cell->load();
        return stack_value_to_string(&inner);
    }

    switch (val->type) {
        case VAL_STRING:
            if (val->as.string && val->as.string->data) {
                return val->as.string->data;
            }
            return "";
        case VAL_INT64:
            snprintf(buffer, sizeof(buffer), "%" PRId64, val->as.i64);
            return buffer;
        case VAL_FLOAT64:
            snprintf(buffer, sizeof(buffer), "%g", val->as.double_val);
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
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (int8_t)v;
}

uint8_t mobius_stack_asUInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint8_t)v;
}

int16_t mobius_stack_asInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (int16_t)v;
}

uint16_t mobius_stack_asUInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint16_t)v;
}

int32_t mobius_stack_asInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (int32_t)v;
}

uint32_t mobius_stack_asUInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint32_t)v;
}

int64_t mobius_stack_asInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return v;
}

uint64_t mobius_stack_asUInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint64_t)v;
}

float mobius_stack_asFloat32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0.0f;
    double v;
    if (!value_to_double(val, &v)) return 0.0f;
    return (float)v;
}

double mobius_stack_asFloat64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0.0;
    double v;
    if (!value_to_double(val, &v)) return 0.0;
    return v;
}

bool mobius_stack_asBool(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return false;
    return value_to_bool(val);
}

const char* mobius_stack_asString(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return "";
    return stack_value_to_string(val);
}

// ============================================================================
// STACK GETTERS - STRICT (respects strict_types pragma)
// ============================================================================

int8_t mobius_stack_getInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (int8_t)v;
}

uint8_t mobius_stack_getUInt8(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint8_t)v;
}

int16_t mobius_stack_getInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (int16_t)v;
}

uint16_t mobius_stack_getUInt16(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint16_t)v;
}

int32_t mobius_stack_getInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (int32_t)v;
}

uint32_t mobius_stack_getUInt32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint32_t)v;
}

int64_t mobius_stack_getInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return v;
}

uint64_t mobius_stack_getUInt64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0;
    if (val->type != VAL_INT64) check_strict_conversion(state, val->type, VAL_INT64);
    int64_t v;
    if (!value_to_int64(val, &v)) return 0;
    return (uint64_t)v;
}

float mobius_stack_getFloat32(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0.0f;
    if (val->type != VAL_FLOAT64) check_strict_conversion(state, val->type, VAL_FLOAT64);
    double v;
    if (!value_to_double(val, &v)) return 0.0f;
    return (float)v;
}

double mobius_stack_getFloat64(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return 0.0;
    if (val->type != VAL_FLOAT64) check_strict_conversion(state, val->type, VAL_FLOAT64);
    double v;
    if (!value_to_double(val, &v)) return 0.0;
    return v;
}

bool mobius_stack_getBool(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return false;
    if (val->type != VAL_BOOL) check_strict_conversion(state, val->type, VAL_BOOL);
    return value_to_bool(val);
}

const char* mobius_stack_getString(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return "";
    if (val->type != VAL_STRING) check_strict_conversion(state, val->type, VAL_STRING);
    return stack_value_to_string(val);
}

// ============================================================================
// STACK PUSH OPERATIONS
// ============================================================================

static int stack_push(MobiusState* state, Value val) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return MOBIUS_ERROR_RUNTIME;
    if (nctx->top >= nctx->capacity) {
        state->setError(MOBIUS_ERROR_MEMORY,
                        "Native stack overflow",
                        "Reduce the number of values pushed to the stack",
                        0, 0, nullptr);
        return MOBIUS_ERROR_MEMORY;
    }
    nctx->registers[nctx->top++] = val;
    return MOBIUS_OK;
}

void mobius_stack_pushInt8(MobiusState* state, int8_t value) {
    stack_push(state, make_int64_value((int64_t)value));
}

void mobius_stack_pushUInt8(MobiusState* state, uint8_t value) {
    stack_push(state, make_int64_value((int64_t)value));
}

void mobius_stack_pushInt16(MobiusState* state, int16_t value) {
    stack_push(state, make_int64_value((int64_t)value));
}

void mobius_stack_pushUInt16(MobiusState* state, uint16_t value) {
    stack_push(state, make_int64_value((int64_t)value));
}

void mobius_stack_pushInt32(MobiusState* state, int32_t value) {
    stack_push(state, make_int64_value((int64_t)value));
}

void mobius_stack_pushUInt32(MobiusState* state, uint32_t value) {
    stack_push(state, make_int64_value((int64_t)value));
}

void mobius_stack_pushInt64(MobiusState* state, int64_t value) {
    stack_push(state, make_int64_value(value));
}

void mobius_stack_pushUInt64(MobiusState* state, uint64_t value) {
    stack_push(state, make_uint64_value(value));
}

void mobius_stack_pushFloat32(MobiusState* state, float value) {
    stack_push(state, make_float_value((double)value));
}

void mobius_stack_pushFloat64(MobiusState* state, double value) {
    stack_push(state, make_float_value(value));
}

void mobius_stack_pushBool(MobiusState* state, bool value) {
    stack_push(state, make_bool_value(value));
}

void mobius_stack_pushString(MobiusState* state, const char* str) {
    if (!str) {
        stack_push(state, make_nil_value());
        return;
    }
    stack_push(state, make_string_value_from_cstr(state, str));
}

void mobius_stack_pushNil(MobiusState* state) {
    stack_push(state, make_nil_value());
}

void mobius_stack_pushNewTable(MobiusState* state, size_t capacity) {
    Table* table = new (std::nothrow) Table(state, capacity == 0 ? 16 : capacity);
    if (!table) {
        state->setError(MOBIUS_ERROR_MEMORY, "Failed to create table",
                        nullptr, 0, 0, nullptr);
        return;
    }
    stack_push(state, make_table_value(table));
}

void mobius_stack_pushNewArray(MobiusState* state, size_t capacity) {
    ArrayValue* array = new (std::nothrow) ArrayValue(capacity == 0 ? 8 : capacity);
    if (!array) {
        state->setError(MOBIUS_ERROR_MEMORY, "Failed to create array",
                        nullptr, 0, 0, nullptr);
        return;
    }
    stack_push(state, make_array_value(array));
}

// ============================================================================
// VARIABLE OPERATIONS
// These operate on the global/current environment, not the native call stack.
// ============================================================================

void mobius_stack_getVariable(MobiusState* state, const char* name) {
    if (!name) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_getVariable() - NULL variable name",
                        nullptr, 0, 0, nullptr);
        return;
    }

    int slot = state->findGlobalSlot(name);
    if (slot >= 0 && (state->globalSlot(slot).flags & VAL_FLAG_DEFINED)) {
        stack_push(state, state->globalSlot(slot));
    } else {
        stack_push(state, make_nil_value());
    }
}

void mobius_stack_getGlobal(MobiusState* state, const char* name) {
    if (!name) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_getGlobal() - NULL variable name",
                        nullptr, 0, 0, nullptr);
        return;
    }

    int slot = state->findGlobalSlot(name);
    if (slot >= 0 && (state->globalSlot(slot).flags & VAL_FLAG_DEFINED)) {
        stack_push(state, state->globalSlot(slot));
    } else {
        stack_push(state, make_nil_value());
    }
}

void mobius_stack_setVariable(MobiusState* state, const char* name) {
    if (!name) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_setVariable() - NULL variable name",
                        nullptr, 0, 0, nullptr);
        return;
    }

    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_setVariable() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value val = nctx->registers[--nctx->top];
    int slot = state->assignGlobalSlot(name);
    val.flags |= VAL_FLAG_DEFINED;
    state->globalSlot(slot) = val;
}

void mobius_stack_setGlobal(MobiusState* state, const char* name) {
    if (!name) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_setGlobal() - NULL variable name",
                        nullptr, 0, 0, nullptr);
        return;
    }

    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_setGlobal() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value val = nctx->registers[--nctx->top];
    int slot = state->assignGlobalSlot(name);
    val.flags |= VAL_FLAG_DEFINED;
    state->globalSlot(slot) = val;
}

// ============================================================================
// TABLE OPERATIONS
// ============================================================================

void mobius_stack_setTableField(MobiusState* state, int table_idx, const char* key) {
    if (!key) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_setTableField() - NULL key",
                        nullptr, 0, 0, nullptr);
        return;
    }

    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_setTableField() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value* table_val = get_value_at(state, table_idx);
    if (!table_val) return;
    if (table_val->type != VAL_TABLE) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_setTableField() - Expected table",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value key_val   = make_string_value_from_cstr(state, key);
    Value field_val = nctx->registers[--nctx->top];
    table_val->as.table->set(key_val, field_val);
}

void mobius_stack_getTableField(MobiusState* state, int table_idx, const char* key) {
    if (!key) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_getTableField() - NULL key",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value* table_val = get_value_at(state, table_idx);
    if (!table_val) return;
    if (table_val->type != VAL_TABLE) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_getTableField() - Expected table",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value key_val = make_string_value_from_cstr(state, key);
    stack_push(state, table_val->as.table->get(key_val));
}

size_t mobius_stack_getTableSize(MobiusState* state, int table_idx) {
    Value* table_val = get_value_at(state, table_idx);
    if (!table_val || table_val->type != VAL_TABLE) return 0;
    return table_val->as.table->size();
}

void mobius_stack_getTableKeys(MobiusState* state, int table_idx) {
    Value* table_val = get_value_at(state, table_idx);
    if (!table_val) return;
    if (table_val->type != VAL_TABLE) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_getTableKeys() - Expected table",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Table* tbl = table_val->as.table;
    mobius_stack_pushNewArray(state, tbl->size());
    int arr_idx = mobius_stack_size(state) - 1;

    tbl->forEach([&](const Value& key, const Value& /*value*/) {
        stack_push(state, key);
        mobius_stack_arrayPush(state, arr_idx);
    });
}

// ============================================================================
// ARRAY OPERATIONS
// ============================================================================

void mobius_stack_setArrayElement(MobiusState* state, int array_idx, size_t element_idx) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_setArrayElement() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value* array_val = get_value_at(state, array_idx);
    if (!array_val) return;
    if (array_val->type != VAL_ARRAY) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_setArrayElement() - Expected array",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value element_val = nctx->registers[--nctx->top];
    array_val->as.array->set(element_idx, element_val);
}

void mobius_stack_getArrayElement(MobiusState* state, int array_idx, size_t element_idx) {
    Value* array_val = get_value_at(state, array_idx);
    if (!array_val) return;
    if (array_val->type != VAL_ARRAY) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_getArrayElement() - Expected array",
                        nullptr, 0, 0, nullptr);
        return;
    }

    stack_push(state, array_val->as.array->get(element_idx));
}

size_t mobius_stack_getArrayLength(MobiusState* state, int array_idx) {
    Value* array_val = get_value_at(state, array_idx);
    if (!array_val || array_val->type != VAL_ARRAY) return 0;
    return array_val->as.array->length();
}

void mobius_stack_arrayPush(MobiusState* state, int array_idx) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_arrayPush() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value* array_val = get_value_at(state, array_idx);
    if (!array_val) return;
    if (array_val->type != VAL_ARRAY) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_arrayPush() - Expected array",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value element_val = nctx->registers[--nctx->top];
    array_val->as.array->push(element_val);
}

void mobius_stack_arrayPop(MobiusState* state, int array_idx) {
    Value* array_val = get_value_at(state, array_idx);
    if (!array_val) return;
    if (array_val->type != VAL_ARRAY) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_arrayPop() - Expected array",
                        nullptr, 0, 0, nullptr);
        return;
    }

    stack_push(state, array_val->as.array->pop());
}

void mobius_stack_arrayInsert(MobiusState* state, int array_idx, size_t element_idx) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_arrayInsert() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value* array_val = get_value_at(state, array_idx);
    if (!array_val) return;
    if (array_val->type != VAL_ARRAY) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_arrayInsert() - Expected array",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value element_val = nctx->registers[--nctx->top];
    array_val->as.array->insert(element_idx, element_val);
}

void mobius_stack_arrayRemove(MobiusState* state, int array_idx, size_t element_idx) {
    Value* array_val = get_value_at(state, array_idx);
    if (!array_val) return;
    if (array_val->type != VAL_ARRAY) {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_stack_arrayRemove() - Expected array",
                        nullptr, 0, 0, nullptr);
        return;
    }

    stack_push(state, array_val->as.array->remove(element_idx));
}

// ============================================================================
// STACK MANIPULATION
// ============================================================================

void mobius_stack_pop(MobiusState* state, int count) {
    if (count < 0) {
        state->setError(MOBIUS_ERROR_ARGUMENT,
                        "mobius_stack_pop() - Invalid negative count",
                        nullptr, 0, 0, nullptr);
        return;
    }

    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    int size = nctx->top - nctx->base;
    if (count > size) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_stack_pop() - Cannot pop more values than stack size",
                        nullptr, 0, 0, nullptr);
        return;
    }

    nctx->top -= count;
}

void mobius_stack_copy(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    if (!val) return;
    Value copy = *val;
    stack_push(state, copy);
}

// ============================================================================
// TYPE METATABLE OPERATIONS
// ============================================================================

static ValueType public_to_internal_type(MobiusValueType t) {
    return (ValueType)t;
}

void mobius_push_type_metatable(MobiusState* state, MobiusValueType type) {
    ValueType vt = public_to_internal_type(type);
    Table* mt = state->typeMetatable(vt);
    if (mt) {
        stack_push(state, make_table_value(mt));
    } else {
        stack_push(state, make_nil_value());
    }
}

void mobius_set_type_metatable(MobiusState* state, MobiusValueType type) {
    NativeCallContext* nctx = get_nctx(state);
    if (!nctx) return;
    if (nctx->top <= nctx->base) {
        state->setError(MOBIUS_ERROR_RUNTIME,
                        "mobius_set_type_metatable() - Stack is empty",
                        nullptr, 0, 0, nullptr);
        return;
    }

    Value val = nctx->registers[--nctx->top];
    ValueType vt = public_to_internal_type(type);

    if (val.type == VAL_TABLE) {
        state->setTypeMetatable(vt, val.as.table);
    } else if (val.type == VAL_NIL) {
        state->setTypeMetatable(vt, nullptr);
    } else {
        state->setError(MOBIUS_ERROR_TYPE,
                        "mobius_set_type_metatable() - Expected table or nil",
                        nullptr, 0, 0, nullptr);
    }
}

// ============================================================================
// PUBLIC API — Native function registration
// ============================================================================

void mobius_register_function(MobiusState* state, const char* name,
                              MobiusCFunction func) {
    if (!state || !name || !func) return;
    Value fval = make_native_function_value(func);
    int slot = state->assignGlobalSlot(name);
    fval.flags |= VAL_FLAG_DEFINED;
    state->globalSlot(slot) = fval;
}

// ============================================================================
// PUBLIC API — Global variable control (sandboxing)
// ============================================================================

void mobius_set_global_readonly(MobiusState* state, const char* name,
                                bool readonly) {
    if (!state || !name) return;
    state->setGlobalReadonly(name, readonly);
}

void mobius_remove_global(MobiusState* state, const char* name) {
    if (!state || !name) return;
    state->removeGlobal(name);
}

// ============================================================================
// PUBLIC API — Protected call (call a Mobius function from native code)
// ============================================================================

int mobius_pcall(MobiusState* state, int nargs, int nresults) {
    if (!state) return -1;
    MobiusVM* vm = state->activeVM();
    NativeCallContext* nctx = state->nativeContext();
    if (!vm || !nctx) return -1;

    int func_slot = nctx->top - nargs - 1;
    if (func_slot < nctx->base) return -1;

    Value func_val = nctx->registers[func_slot];

    // Use dynamic allocation only if nargs exceeds inline buffer
    static constexpr int INLINE_ARGS = 16;
    Value inline_args[INLINE_ARGS];
    Value* args = (nargs <= INLINE_ARGS) ? inline_args : new (std::nothrow) Value[nargs];
    if (!args) return -1;

    for (int i = 0; i < nargs; i++) {
        args[i] = nctx->registers[func_slot + 1 + i];
    }

    nctx->top = func_slot;

    int rc;
    if (func_val.type == VAL_NATIVE_FUNCTION) {
        for (int i = 0; i < nargs; i++) {
            nctx->registers[nctx->top++] = args[i];
        }
        if (args != inline_args) delete[] args;
        rc = func_val.as.native_function(state, nargs);
        if (rc < 0) return -1;
        return rc;
    }

    if (func_val.type != VAL_FUNCTION || !func_val.as.function) {
        if (args != inline_args) delete[] args;
        return state->error("pcall: value is not a function");
    }

    MobiusFunction* mf = func_val.as.function;
    if (!mf->proto || (int)mf->param_count != nargs) {
        if (args != inline_args) delete[] args;
        return state->error("pcall: argument count mismatch");
    }

    Prototype* child_proto = mf->proto;

    int caller_top = vm->callStackTop().base +
                     vm->callStackTop().proto->num_registers;
    int pcall_base = caller_top;
    int child_base = pcall_base + 1;
    int needed = child_base + child_proto->num_registers + 16;
    vm->ensureRegisters(needed);

    vm->registers_[pcall_base] = func_val;
    for (int i = 0; i < nargs; i++) {
        vm->registers_[child_base + i] = args[i];
    }
    if (args != inline_args) delete[] args;

    nctx->registers = vm->registers_.data();
    nctx->capacity = (int)vm->registers_.size();

    CallInfo& child_ci = vm->callStackPush(child_proto, child_proto->code.data(),
                                            child_base, nresults + 1);
    if (mf->upvalues && mf->upvalue_count > 0) {
        child_ci.setUpvaluesFrom(mf->upvalues, mf->upvalue_count);
    }

    int saved_base = vm->native_ctx_.base;
    int saved_top  = vm->native_ctx_.top;

    size_t depth = vm->callStackSize() - 1;
    rc = vm->run(depth);

    nctx->registers = vm->registers_.data();
    nctx->capacity  = (int)vm->registers_.size();
    nctx->base = saved_base;
    nctx->top  = saved_top;

    if (rc < 0) return -1;

    int actual_results = (nresults > 0) ? nresults : 1;
    for (int i = 0; i < actual_results; i++) {
        nctx->registers[nctx->top++] = vm->registers_[pcall_base + i];
    }

    return actual_results;
}

// ============================================================================
// PUBLIC API — Userdata
// ============================================================================

void mobius_stack_pushUserdata(MobiusState* state, void* ptr,
                               void (*destructor)(void*),
                               const char* type_name, size_t size) {
    stack_push(state, make_userdata_value(ptr, destructor, type_name, size));
}

bool mobius_stack_isUserdata(MobiusState* state, int idx) {
    Value* val = get_value_at(state, idx);
    return val && val->type == VAL_USERDATA;
}

void* mobius_stack_getUserdata(MobiusState* state, int idx, const char** out_type_name) {
    Value* val = get_value_at(state, idx);
    if (!val || val->type != VAL_USERDATA || !val->as.userdata) return NULL;
    if (out_type_name) *out_type_name = val->as.userdata->type_name;
    return val->as.userdata->ptr;
}

} // extern "C"
