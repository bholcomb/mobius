#include <cctype>
#include <cerrno>
#include "library/core.h"
#include "internal/gc.h"
#include "vm/vm.h"
#include "data/value.h"
#include "data/shared_cell.h"
#include "data/table.h"
#include "data/metamethods.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// =============================================================================
// UNIFIED CORE FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_print(MobiusState* state, int arg_count) {
    for (int i = 0; i < arg_count; i++) {
        Value arg = state->npeek(arg_count - 1 - i);
        
        if (arg.type == VAL_STRING && arg.as.string) {
            const char* str_data = arg.as.string->data;
            if (str_data) {
                fwrite(str_data, 1, arg.as.string->length, stdout);
            }
        } else if (arg.type == VAL_TABLE && arg.as.table) {
            Value tostr = arg.as.table->getMetamethod(state->metamethods()->tostring());
            if (tostr.type != VAL_NIL) {
                state->npush(tostr);
                state->npush(arg);
                int rc = mobius_pcall(state, 1, 1);
                if (rc < 0) {
                    // Propagate the error, as str() does. Printing the raw
                    // table and returning success left the VM holding the
                    // error, and the next call failed ("Attempt to call a
                    // non-function value").
                    fflush(stdout);
                    return -1;
                }
                Value s = state->npop();
                if (s.type == VAL_STRING && s.as.string)
                    fwrite(s.as.string->data, 1, s.as.string->length, stdout);
                else
                    print_value(s);
            } else {
                print_value(arg);
            }
        } else {
            print_value(arg);
        }
        
        if (i < arg_count - 1) printf(" ");
    }
    printf("\n");
    
    for (int i = 0; i < arg_count; i++) {
        state->npop();
    }
    
    return 0;
}

int lib_typeof(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("typeof expects 1 argument");
    }

    Value arg = state->npeek(0);
    // A shared value reports what it holds: sharing is a property of the
    // binding, and `typeof(x) == "array"` should not depend on whether x
    // came from a shared structure.
    if (arg.type == VAL_SHARED_CELL && arg.as.shared_cell) {
        arg = arg.as.shared_cell->load();
    }
    const char* type_name = value_type_name(arg.type);
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    Value result = make_string_value_from_cstr(state, type_name);
    state->npush(result);
    
    return 1;
}

// The string's full text must be the number: no leading whitespace (strtoll
// and strtod skip it), nothing after it (including past an embedded NUL),
// and not empty (int("") used to give 0).
static bool whole_string_number(const MobiusString* s, const char* end) {
    return s->length > 0 && !isspace((unsigned char)s->data[0]) &&
           end == s->data + s->length;
}

int lib_int(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("int expects 1 argument");
    }

    Value arg = state->npop();
    Value result;
    char msg[128];

    switch (arg.type) {
        case VAL_INT64:
            result = arg;  // Already an integer
            break;
        case VAL_UINT64:
            // Converts when the value fits (it used to be refused for every
            // uint64).
            if (arg.as.u64 > (uint64_t)INT64_MAX) {
                snprintf(msg, sizeof(msg), "Cannot convert %llu to int64: out of range",
                         (unsigned long long)arg.as.u64);
                return state->error(msg);
            }
            result = make_int64_value((int64_t)arg.as.u64);
            break;
        case VAL_FLOAT64: {
            // Truncates toward zero. Out-of-range values, NaN and infinity
            // are errors: the C++ conversion is undefined there, and x86 gave
            // INT64_MIN for int(1e300).
            double d = arg.as.double_val;
            if (d != d) return state->error("Cannot convert NaN to int64");
            if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0)) {   // [-2^63, 2^63)
                snprintf(msg, sizeof(msg), "Cannot convert %g to int64: out of range", d);
                return state->error(msg);
            }
            result = make_int64_value((int64_t)d);
            break;
        }
        case VAL_STRING: {
            if (!arg.as.string) return state->error("Cannot convert null string to integer");
            const MobiusString* str = arg.as.string;
            char* endptr;
            errno = 0;
            long long val = strtoll(str->data, &endptr, 10);
            if (!whole_string_number(str, endptr)) {
                return state->error("Cannot convert string to integer");
            }
            if (errno == ERANGE) {   // used to clamp silently to INT64_MIN/MAX
                return state->error("Cannot convert string to integer: out of int64 range");
            }
            result = make_int64_value((int64_t)val);
            break;
        }
        case VAL_BOOL:
            result = make_int64_value(arg.as.boolean ? 1 : 0);
            break;
        default:
            return state->error("Cannot convert value to integer");
    }

    state->npush(result);
    return 1;
}

int lib_float(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("float expects 1 argument");
    }

    Value arg = state->npeek(0);
    Value result;

    switch (arg.type) {
        case VAL_FLOAT64:
            result = arg;  // Already a float
            break;
        case VAL_INT64:
            result = make_float_value((double)arg.as.i64);
            break;
        case VAL_UINT64:
            result = make_float_value((double)arg.as.u64);
            break;
        case VAL_STRING: {
            if (!arg.as.string) return state->error("Cannot convert null string to float");
            const MobiusString* str = arg.as.string;
            char* endptr;
            double val = strtod(str->data, &endptr);
            if (!whole_string_number(str, endptr)) {   // float("") used to give 0.0
                return state->error("Cannot convert string to float");
            }
            result = make_float_value(val);
            break;
        }
        default:
            return state->error("Cannot convert value to float");
    }

    state->npop();
    state->npush(result);
    return 1;
}

int lib_exit(MobiusState* state, int arg_count) {
    int exit_code = 0;
    if (arg_count > 1) {
        return state->error("exit expects 0 or 1 arguments");
    }
    if (arg_count == 1) {
        Value arg = state->npop();
        if (arg.type == VAL_INT64) {
            exit_code = (int)arg.as.i64;
        } else if (arg.type == VAL_FLOAT64) {
            exit_code = (int)arg.as.double_val;
        } else {
            exit_code = 1;
        }
    }
    ::exit(exit_code);
    return 0;
}

int lib_str(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("str expects 1 argument");
    }

    const Value& arg = state->npeek(0);

    if (arg.type == VAL_TABLE && arg.as.table) {
        Value tostr = arg.as.table->getMetamethod(state->metamethods()->tostring());
        if (tostr.type != VAL_NIL) {
            Value self = arg;
            state->npop();
            state->npush(tostr);
            state->npush(self);
            int rc = mobius_pcall(state, 1, 1);
            if (rc < 0) return -1;
            return 1;
        }
    }

    Value result = value_to_string_value(state, arg);

    state->npop();
    state->npush(result);

    return 1;
}

// Introspection for the tracing-GC work: number of tracked heap objects
// (tables, arrays, closures, upvalues). Used by tests to verify object
// lifetimes; not a public API.
int lib_gc_objects(MobiusState* state, int arg_count) {
    (void)arg_count;
    state->npush(make_int64_value((int64_t)gc_tracked_count(state->gcHeap())));
    return 1;
}

// Force a shadow-GC verification pass (test hook; requires MOBIUS_GC_SHADOW).
int lib_gc_verify(MobiusState* state, int arg_count) {
    (void)arg_count;
    MobiusVM* vm = state->activeVM();
    // Called from a native, so the caller's native frame is in flight; the
    // verifier's own gate would refuse. Drop below it for the forced pass,
    // but only when this call is the sole native in flight: from inside a
    // callback (e.g. under arr:map) the outer native holds values only on
    // the C++ stack, invisible to root enumeration.
    if (vm && g_gc_shadow_mode && vm->native_depth_ == 1) {
        vm->native_depth_--;
        gc_shadow_verify_now(vm);
        vm->native_depth_++;
    }
    state->npush(make_int64_value(0));
    return 1;
}

// Force a full collection; returns objects freed (test hook).
int lib_gc_collect(MobiusState* state, int arg_count) {
    (void)arg_count;
    MobiusVM* vm = state->activeVM();
    int64_t freed = 0;
    // Same native-frame dance as lib_gc_verify, and only when this call is
    // the sole in-flight native. Collecting from inside a callback freed the
    // outer native's unrooted temporaries (e.g. arr:map's result array);
    // there the request is skipped and 0 is returned.
    if (vm && vm->native_depth_ == 1) {
        vm->native_depth_--;
        freed = (int64_t)gc_collect(vm);
        vm->native_depth_++;
    }
    state->npush(make_int64_value(freed));
    return 1;
}
