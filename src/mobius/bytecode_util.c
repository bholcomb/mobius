#include "bytecode_util.h"
#include "bytecode.h"
#include "value.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// UTILITY BUILTIN FUNCTIONS FOR BYTECODE VM
// =============================================================================


// Utility functions
void builtin_random_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    
    if (arg_count == 0) {
        // Return random float between 0 and 1
        *result = make_float_value((double)rand() / RAND_MAX);
    } else if (arg_count == 1) {
        // Return random integer between 0 and n-1
        Value arg = vm_peek(vm, 0);
        if (arg.type != VAL_INTEGER) {
            vm_runtime_error(vm, "random expects an integer argument");
            *result = make_nil_value();
            return;
        }
        int64_t max_val = arg.as.integer.value.i64;
        if (max_val <= 0) {
            vm_runtime_error(vm, "random expects a positive integer");
            *result = make_nil_value();
            return;
        }
        *result = make_integer_value(NUM_INT64, rand() % max_val);
    } else {
        vm_runtime_error(vm, "random expects 0 or 1 arguments");
        *result = make_nil_value();
    }
}

void builtin_time_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 0) {
        vm_runtime_error(vm, "time expects no arguments");
        *result = make_nil_value();
        return;
    }

    *result = make_integer_value(NUM_INT64, (int64_t)time(NULL));
}

void builtin_clock_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 0) {
        vm_runtime_error(vm, "clock expects no arguments");
        *result = make_nil_value();
        return;
    }

    *result = make_float_value((double)clock() / CLOCKS_PER_SEC);
}
