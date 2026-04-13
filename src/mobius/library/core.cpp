#include "library/core.h"
#include "data/value.h"
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
                if (rc >= 0) {
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
    const char* type_name = value_type_name(arg.type);
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    Value result = make_string_value_from_cstr(state, type_name);
    state->npush(result);
    
    return 1;
}

int lib_int(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("int expects 1 argument");
    }

    Value arg = state->npop();
    Value result;
    
    switch (arg.type) {
        case VAL_INT64:
            result = arg;  // Already an integer
            break;
        case VAL_FLOAT64:
            result = make_int64_value((int64_t)arg.as.double_val);
            break;
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = arg.as.string->data;
                char* endptr;
                long long val = strtoll(str, &endptr, 10);
                if (*endptr == '\0') {
                    result = make_int64_value((int64_t)val);
                } else {
                    return state->error("Cannot convert string to integer");
                }
            } else {
                return state->error("Cannot convert null string to integer");
            }
            break;
        }
        case VAL_BOOL:
            result = make_int64_value(arg.as.boolean ? 1 : 0);
            break;
        default:
            return state->error("Cannot convert value to integer");
    }
       
    // Push result onto stack
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
        case VAL_INT64: {
            result = make_float_value((double)arg.as.i64);
            break;
        }
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = arg.as.string->data;
                char* endptr;
                double val = strtod(str, &endptr);
                if (*endptr == '\0') {
                    result = make_float_value(val);
                } else {
                    return state->error("Cannot convert string to float");
                }
            } else {
                return state->error("Cannot convert null string to float");
            }
            break;
        }
        default:
            return state->error("Cannot convert value to float");
    }
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
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

    Value arg = state->npeek(0);

    if (arg.type == VAL_TABLE && arg.as.table) {
        Value tostr = arg.as.table->getMetamethod(state->metamethods()->tostring());
        if (tostr.type != VAL_NIL) {
            state->npop();
            state->npush(tostr);
            state->npush(arg);
            int rc = mobius_pcall(state, 1, 1);
            if (rc < 0) return -1;
            return 1;
        }
    }

    MobiusString* str = value_to_interned_string(state, arg);
    Value result = str ? make_string_value(str) : make_nil_value();
    
    state->npop();
    state->npush(result);
    
    return 1;
}
