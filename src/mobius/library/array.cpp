#include "library/array.h"
#include "data/array.h"
#include "data/shared_cell.h"
#include "data/table.h"
#include "data/value.h"
#include "state/mobius_state.h"
#include <mobius/mobius_plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

// =============================================================================
// HELPER: extract ArrayValue* from self (first argument via : syntax)
// =============================================================================

struct ArraySelfAccess {
    Value self_value;
    ArrayValue* array = nullptr;
    SharedCell* cell = nullptr;
    std::unique_lock<std::recursive_mutex> lock;
};

static ArrayValue* extract_array_self(MobiusState* state, const char* err_msg, ArraySelfAccess* access = nullptr) {
    const Value& self = state->npeek_self();
    if (access) access->self_value = self;
    if (self.type == VAL_ARRAY && self.as.array) {
        if (access) access->array = self.as.array;
        return self.as.array;
    }
    if (self.type == VAL_SHARED_CELL && self.as.shared_cell) {
        if (access) {
            access->cell = self.as.shared_cell;
            access->lock = std::unique_lock<std::recursive_mutex>(self.as.shared_cell->mutex());
            Value& inner = self.as.shared_cell->unsafeValue();
            if (inner.type == VAL_ARRAY && inner.as.array) {
                access->array = inner.as.array;
                return inner.as.array;
            }
        } else {
            std::lock_guard<std::recursive_mutex> lock(self.as.shared_cell->mutex());
            Value& inner = self.as.shared_cell->unsafeValue();
            if (inner.type == VAL_ARRAY && inner.as.array) {
                return inner.as.array;
            }
        }
    }
    state->error(err_msg);
    return nullptr;
}

// =============================================================================
// GLOBAL: array_create(capacity [, fill_value])
// =============================================================================

int lib_array_create(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) {
        return state->error("array_create expects 1 or 2 arguments (capacity [, fill_value])");
    }

    Value fill_val;
    bool has_fill = false;
    if (arg_count == 2) {
        fill_val = state->npop();
        has_fill = true;
    }

    Value cap_arg = state->npop();
    if (cap_arg.type != VAL_INT64) {
        return state->error("array_create capacity must be an integer");
    }
    if (cap_arg.as.i64 < 0) {
        return state->error("array_create capacity must be non-negative");
    }

    size_t capacity = (size_t)cap_arg.as.i64;
    if (capacity == 0) capacity = 8;

    ArrayValue* array = new (std::nothrow) ArrayValue(capacity);
    if (!array) {
        return state->error("Failed to create array");
    }

    if (has_fill) {
        for (size_t i = 0; i < capacity; i++) {
            array->push(fill_val);
        }
    }

    state->npush(make_array_value(array));
    return 1;
}

// =============================================================================
// METHOD-STYLE ARRAY FUNCTIONS (called via arr:method() with self at base)
// =============================================================================

int array_method_push(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:push expects 1 argument (value)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:push: self is not an array", &access);
    if (!arr) return -1;

    Value val = state->npop();
    state->npop();

    if (arr->hasActiveSlices()) {
        return state->error("cannot resize array while slices are alive");
    }
    arr->push(val);
    return 0;
}

int array_method_pop(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("arr:pop expects 0 arguments");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:pop: self is not an array", &access);
    if (!arr) return -1;

    state->npop();

    if (arr->hasActiveSlices()) {
        return state->error("cannot resize array while slices are alive");
    }
    if (arr->length() == 0) {
        state->npush(make_nil_value());
    } else {
        state->npush(arr->pop());
    }
    return 1;
}

int array_method_get(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:get expects 1 argument (index)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:get: self is not an array", &access);
    if (!arr) return -1;

    Value index_val = state->npop();
    state->npop();

    if (index_val.type != VAL_INT64) {
        return state->error("arr:get index must be an integer");
    }

    int64_t index = index_val.as.i64;
    if (index < 0 || index >= (int64_t)arr->length()) {
        state->npush(make_nil_value());
    } else {
        state->npush(arr->get((size_t)index));
    }
    return 1;
}

int array_method_set(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("arr:set expects 2 arguments (index, value)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:set: self is not an array", &access);
    if (!arr) return -1;

    Value value = state->npop();
    Value index_val = state->npop();
    state->npop();

    if (index_val.type != VAL_INT64) {
        return state->error("arr:set index must be an integer");
    }

    int64_t index = index_val.as.i64;
    if (index < 0 || index >= (int64_t)arr->length()) {
        return state->error("arr:set index out of bounds");
    }

    arr->set((size_t)index, value);
    return 0;
}

int array_method_length(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("arr:length expects 0 arguments");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:length: self is not an array", &access);
    if (!arr) return -1;

    state->npop();

    state->npush(make_int64_value((int64_t)arr->length()));
    return 1;
}

int array_method_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("arr:slice expects 2 arguments (start, end)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:slice: self is not an array", &access);
    if (!arr) return -1;

    Value end_val = state->npop();
    Value start_val = state->npop();
    state->npop();

    if (start_val.type != VAL_INT64 || end_val.type != VAL_INT64) {
        return state->error("arr:slice start and end must be integers");
    }

    int64_t start = start_val.as.i64;
    int64_t end = end_val.as.i64;

    if (start < 0) start = 0;
    if (end > (int64_t)arr->length()) end = (int64_t)arr->length();
    if (start >= end) {
        state->npush(make_array_value(new ArrayValue(8)));
        return 1;
    }

    size_t slice_length = (size_t)(end - start);
    ArrayValue* slice_array = new ArrayValue(slice_length);
    for (size_t i = 0; i < slice_length; i++) {
        slice_array->push((*arr)[start + i]);
    }

    state->npush(make_array_value(slice_array));
    return 1;
}

int array_method_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) return state->error("arr:concat expects at least 1 argument");

    ArraySelfAccess access;
    ArrayValue* self_arr = extract_array_self(state, "arr:concat: self is not an array", &access);
    if (!self_arr) return -1;

    int other_count = arg_count - 1;
    size_t total_length = self_arr->length();

    for (int i = 0; i < other_count; i++) {
        Value arg = state->npeek(i);
        if (arg.type != VAL_ARRAY) {
            for (int j = 0; j < arg_count; j++) state->npop();
            return state->error("arr:concat expects all arguments to be arrays");
        }
        total_length += arg.as.array->length();
    }

    ArrayValue* result = new ArrayValue(total_length);

    for (size_t j = 0; j < self_arr->length(); j++) {
        result->push((*self_arr)[j]);
    }

    for (int i = other_count - 1; i >= 0; i--) {
        Value arg = state->npeek(i);
        ArrayValue* a = arg.as.array;
        for (size_t j = 0; j < a->length(); j++) {
            result->push((*a)[j]);
        }
    }

    for (int i = 0; i < arg_count; i++) state->npop();

    state->npush(make_array_value(result));
    return 1;
}

int array_method_reverse(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("arr:reverse expects 0 arguments");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:reverse: self is not an array", &access);
    if (!arr) return -1;

    Value self_val = state->npeek_self();
    arr->reverse();

    state->npop();
    state->npush(self_val);
    return 1;
}

int array_method_find(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:find expects 1 argument (value)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:find: self is not an array", &access);
    if (!arr) return -1;

    Value search_val = state->npop();
    state->npop();

    bool strict = state->config().strict_mode;
    for (size_t i = 0; i < arr->length(); i++) {
        if (strict ? (*arr)[i].exactlyEqual(search_val) : (*arr)[i] == search_val) {
            state->npush(make_int64_value((int64_t)i));
            return 1;
        }
    }

    state->npush(make_int64_value(-1));
    return 1;
}

static bool value_less_than(const Value& a, const Value& b) {
    if (a.type == VAL_INT64 && b.type == VAL_INT64) return a.as.i64 < b.as.i64;
    if (a.type == VAL_FLOAT64 && b.type == VAL_FLOAT64) return a.as.double_val < b.as.double_val;
    if (a.type == VAL_INT64 && b.type == VAL_FLOAT64) return (double)a.as.i64 < b.as.double_val;
    if (a.type == VAL_FLOAT64 && b.type == VAL_INT64) return a.as.double_val < (double)b.as.i64;
    if (a.type == VAL_STRING && b.type == VAL_STRING && a.as.string && b.as.string)
        return strcmp(a.as.string->data, b.as.string->data) < 0;
    return false;
}

int array_method_sort(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2)
        return state->error("arr:sort expects 0 or 1 arguments ([comparator])");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:sort: self is not an array", &access);
    if (!arr) return -1;
    Value self_val = state->npeek_self();
    size_t len = arr->length();

    Value comp_val;
    bool has_comp = (arg_count == 2);
    if (has_comp) {
        comp_val = state->npop();
    }
    state->npop();

    if (!has_comp) {
        std::sort(arr->data(), arr->data() + len, value_less_than);
    } else {
        if (comp_val.type != VAL_FUNCTION && comp_val.type != VAL_NATIVE_FUNCTION) {
            return state->error("arr:sort comparator must be a function");
        }
        bool sort_error = false;
        std::sort(arr->data(), arr->data() + len, [&](const Value& a, const Value& b) -> bool {
            if (sort_error) return false;
            mobius_stack_pushNil(state);
            state->npeek(0) = comp_val;
            state->npush(a);
            state->npush(b);
            int rc = mobius_pcall(state, 2, 1);
            if (rc < 0) { sort_error = true; return false; }
            Value result = state->npop();
            return is_truthy(result);
        });
        if (sort_error) return -1;
    }

    state->npush(self_val);
    return 1;
}

int array_method_map(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:map expects 1 argument (function)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:map: self is not an array", &access);
    if (!arr) return -1;

    Value func_val = state->npop();
    state->npop();

    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("arr:map argument must be a function");
    }

    size_t len = arr->length();
    ArrayValue* result = new ArrayValue(len);

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*arr)[i]);
        state->npush(make_int64_value((int64_t)i));
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) { delete result; return -1; }
        result->push(state->npop());
    }

    state->npush(make_array_value(result));
    return 1;
}

int array_method_filter(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:filter expects 1 argument (function)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:filter: self is not an array", &access);
    if (!arr) return -1;

    Value func_val = state->npop();
    state->npop();

    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("arr:filter argument must be a function");
    }

    size_t len = arr->length();
    ArrayValue* result = new ArrayValue(len);

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*arr)[i]);
        state->npush(make_int64_value((int64_t)i));
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) { delete result; return -1; }
        Value pred = state->npop();
        if (is_truthy(pred)) {
            result->push((*arr)[i]);
        }
    }

    state->npush(make_array_value(result));
    return 1;
}

int array_method_reduce(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("arr:reduce expects 2 arguments (function, initial)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:reduce: self is not an array", &access);
    if (!arr) return -1;

    Value initial = state->npop();
    Value func_val = state->npop();
    state->npop();

    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("arr:reduce first argument must be a function");
    }

    size_t len = arr->length();
    Value accumulator = initial;

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush(accumulator);
        state->npush((*arr)[i]);
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) return -1;
        accumulator = state->npop();
    }

    state->npush(accumulator);
    return 1;
}

int array_method_foreach(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:foreach expects 1 argument (function)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:foreach: self is not an array", &access);
    if (!arr) return -1;

    Value func_val = state->npop();
    state->npop();

    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("arr:foreach argument must be a function");
    }

    size_t len = arr->length();
    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*arr)[i]);
        state->npush(make_int64_value((int64_t)i));
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) return -1;
        state->npop();
    }

    return 0;
}

int array_method_any(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:any expects 1 argument (function)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:any: self is not an array", &access);
    if (!arr) return -1;

    Value func_val = state->npop();
    state->npop();

    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("arr:any argument must be a function");
    }

    size_t len = arr->length();
    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*arr)[i]);
        int rc = mobius_pcall(state, 1, 1);
        if (rc < 0) return -1;
        Value result = state->npop();
        if (is_truthy(result)) {
            state->npush(make_bool_value(true));
            return 1;
        }
    }

    state->npush(make_bool_value(false));
    return 1;
}

int array_method_all(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("arr:all expects 1 argument (function)");

    ArraySelfAccess access;
    ArrayValue* arr = extract_array_self(state, "arr:all: self is not an array", &access);
    if (!arr) return -1;

    Value func_val = state->npop();
    state->npop();

    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("arr:all argument must be a function");
    }

    size_t len = arr->length();
    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*arr)[i]);
        int rc = mobius_pcall(state, 1, 1);
        if (rc < 0) return -1;
        Value result = state->npop();
        if (!is_truthy(result)) {
            state->npush(make_bool_value(false));
            return 1;
        }
    }

    state->npush(make_bool_value(true));
    return 1;
}

// =============================================================================
// ATOMIC OPERATIONS (for shared arrays)
// =============================================================================

// =============================================================================
// TYPE METATABLE BUILDER
// =============================================================================

Table* create_array_type_metatable(MobiusState* state) {
    Table* mt = new Table(state, 16);
    mt->setByString(state->stringPool()->intern("push"),    make_native_function_value(array_method_push));
    mt->setByString(state->stringPool()->intern("pop"),     make_native_function_value(array_method_pop));
    mt->setByString(state->stringPool()->intern("get"),     make_native_function_value(array_method_get));
    mt->setByString(state->stringPool()->intern("set"),     make_native_function_value(array_method_set));
    mt->setByString(state->stringPool()->intern("length"),  make_native_function_value(array_method_length));
    mt->setByString(state->stringPool()->intern("slice"),   make_native_function_value(array_method_slice));
    mt->setByString(state->stringPool()->intern("concat"),  make_native_function_value(array_method_concat));
    mt->setByString(state->stringPool()->intern("reverse"), make_native_function_value(array_method_reverse));
    mt->setByString(state->stringPool()->intern("find"),    make_native_function_value(array_method_find));
    mt->setByString(state->stringPool()->intern("sort"),    make_native_function_value(array_method_sort));
    mt->setByString(state->stringPool()->intern("map"),     make_native_function_value(array_method_map));
    mt->setByString(state->stringPool()->intern("filter"),  make_native_function_value(array_method_filter));
    mt->setByString(state->stringPool()->intern("reduce"),  make_native_function_value(array_method_reduce));
    mt->setByString(state->stringPool()->intern("foreach"), make_native_function_value(array_method_foreach));
    mt->setByString(state->stringPool()->intern("any"),     make_native_function_value(array_method_any));
    mt->setByString(state->stringPool()->intern("all"),     make_native_function_value(array_method_all));
    return mt;
}
