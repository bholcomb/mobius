#include "library/math.h"
#include "data/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>


// =============================================================================
// UNIFIED MATH FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_abs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("abs expects 1 argument");
    }

    Value arg = state->npeek(0);
    Value result;
    
    if (arg.type == VAL_INT64) {
        int64_t val = arg.as.i64;
        result = make_int64_value(val < 0 ? -val : val);
    } else if (arg.type == VAL_FLOAT64) {
        result = make_float_value(fabs(arg.as.double_val));
    } else {
        return state->error("abs expects a numeric argument");
    }
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    state->npush(result);
    
    return 1;
}

int lib_min(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("min expects at least 2 arguments");
    }

    Value min_val = state->npeek(arg_count - 1);  // First argument
    
    for (int i = arg_count - 2; i >= 0; i--) {
        Value current = state->npeek(i);
        
        // Compare numeric values
        bool is_less = false;
        if (min_val.type == VAL_INT64 && current.type == VAL_INT64) {
            int64_t min_int = min_val.as.i64;
            int64_t current_int = current.as.i64;
            is_less = current_int < min_int;
        } else if (min_val.type == VAL_FLOAT64 && current.type == VAL_FLOAT64) {
            is_less = current.as.double_val < min_val.as.double_val;
        } else {
            return state->error("min expects numeric arguments of compatible types");
        }
        
        if (is_less) {
            min_val = current;
        }
    }
    
    // Pop all arguments from stack
    for (int i = 0; i < arg_count; i++) {
        state->npop();
    }
    
    // Push result onto stack
    state->npush(min_val);
    
    return 1;
}

int lib_max(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("max expects at least 2 arguments");
    }

    Value max_val = state->npeek(arg_count - 1);  // First argument
    
    for (int i = arg_count - 2; i >= 0; i--) {
        Value current = state->npeek(i);
        
        // Compare numeric values (similar logic to min but reversed)
        bool is_greater = false;
        if (max_val.type == VAL_INT64 && current.type == VAL_INT64) {
            int64_t max_int = max_val.as.i64;
            int64_t current_int = current.as.i64;
            is_greater = current_int > max_int;
        } else if (max_val.type == VAL_FLOAT64 && current.type == VAL_FLOAT64) {
            is_greater = current.as.double_val > max_val.as.double_val;
        } else {
            return state->error("max expects numeric arguments of compatible types");
        }
        
        if (is_greater) {
            max_val = current;
        }
    }
    
    // Pop all arguments from stack
    for (int i = 0; i < arg_count; i++) {
        state->npop();
    }
    
    // Push result onto stack
    state->npush(max_val);
    
    return 1;
}

int lib_pow(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("pow expects 2 arguments");
    }

    Value base = state->npeek(1);
    Value exponent = state->npeek(0);
    
    double base_val, exp_val;
    
    // Convert base to double
    if (base.type == VAL_FLOAT64) {
        base_val = base.as.double_val;
    } else if (base.type == VAL_INT64) {
        base_val = (double)base.as.i64;
    } else {
        return state->error("pow expects numeric arguments");
    }
    
    // Convert exponent to double (similar logic)
    if (exponent.type == VAL_FLOAT64) {
        exp_val = exponent.as.double_val;
    } else if (exponent.type == VAL_INT64) {
        exp_val = (double)exponent.as.i64;
    } else {
        return state->error("pow expects numeric arguments");
    }
    
    double result_val = pow(base_val, exp_val);
    
    // Pop arguments from stack
    state->npop();
    state->npop();
    
    // Push result onto stack
    state->npush(make_float_value(result_val));
    
    return 1;
}

int lib_sqrt(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("sqrt expects 1 argument");
    }

    Value arg = state->npeek(0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.double_val;
    } else if (arg.type == VAL_INT64) {
        val = (double)arg.as.i64;
    } else {
        return state->error("sqrt expects a numeric argument");
    }
    
    if (val < 0) {
        return state->error("sqrt of negative number");
    }
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    state->npush(make_float_value(sqrt(val)));
    
    return 1;
}

int lib_floor(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("floor expects 1 argument");
    }

    Value arg = state->npeek(0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.double_val;
    } else if (arg.type == VAL_INT64) {
        // Integer floor is just the integer itself
        state->npop();
        state->npush(arg);
        return 1;
    } else {
        return state->error("floor expects a numeric argument");
    }
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    state->npush(make_float_value(floor(val)));
    
    return 1;
}

int lib_ceil(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("ceil expects 1 argument");
    }

    Value arg = state->npeek(0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.double_val;
    } else if (arg.type == VAL_INT64) {
        // Integer ceil is just the integer itself
        state->npop();
        state->npush(arg);
        return 1;
    } else {
        return state->error("ceil expects a numeric argument");
    }
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    state->npush(make_float_value(ceil(val)));
    
    return 1;
}

int lib_round(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("round expects 1 argument");
    }

    Value arg = state->npeek(0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.double_val;
    } else if (arg.type == VAL_INT64) {
        // Integer round is just the integer itself
        state->npop();
        state->npush(arg);
        return 1;
    } else {
        return state->error("round expects a numeric argument");
    }
    
    // Pop argument from stack
    state->npop();
    
    // Push result onto stack
    state->npush(make_float_value(round(val)));
    
    return 1;
}
