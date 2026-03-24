#include "state/mobius_state.h"
#include <mobius/mobius_plugin.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>

int main() {
    printf("=== Mobius Stack API Test ===\n\n");
    
    // Create state
    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        fprintf(stderr, "Failed to create MobiusState\n");
        return 1;
    }
    mobius_init_stdlib(state);
    
    // Test 1: Basic push/pop operations
    printf("Test 1: Basic push/pop\n");
    mobius_stack_pushInt64(state, 42);
    mobius_stack_pushFloat64(state, 3.14);
    mobius_stack_pushString(state, "Hello, World!");
    mobius_stack_pushBool(state, true);
    
    printf("  Stack size: %d\n", mobius_stack_size(state));
    assert(mobius_stack_size(state) == 4);
    
    printf("  Top (bool): %s\n", mobius_stack_asBool(state, -1) ? "true" : "false");
    printf("  String: %s\n", mobius_stack_asString(state, -2));
    printf("  Float: %g\n", mobius_stack_asFloat64(state, -3));
    printf("  Int: %" PRId64 "\n", mobius_stack_asInt64(state, -4));
    
    mobius_stack_pop(state, 4);
    assert(mobius_stack_size(state) == 0);
    printf("  After pop, stack size: %d\n\n", mobius_stack_size(state));
    
    // Test 2: Variable operations
    printf("Test 2: Variable operations\n");
    mobius_stack_pushInt64(state, 100);
    mobius_stack_setGlobal(state, "test_var");
    
    mobius_stack_getGlobal(state, "test_var");
    int64_t value = mobius_stack_asInt64(state, -1);
    printf("  Retrieved global 'test_var': %" PRId64 "\n", value);
    assert(value == 100);
    mobius_stack_pop(state, 1);
    printf("\n");
    
    // Test 3: Table operations
    printf("Test 3: Table operations\n");
    mobius_stack_pushNewTable(state, 0);
    mobius_stack_pushInt64(state, 800);
    mobius_stack_setTableField(state, 0, "width");
    mobius_stack_pushInt64(state, 600);
    mobius_stack_setTableField(state, 0, "height");
    mobius_stack_pushString(state, "My Window");
    mobius_stack_setTableField(state, 0, "title");
    
    mobius_stack_getTableField(state, 0, "width");
    printf("  width: %" PRId64 "\n", mobius_stack_asInt64(state, -1));
    mobius_stack_pop(state, 1);
    
    mobius_stack_getTableField(state, 0, "height");
    printf("  height: %" PRId64 "\n", mobius_stack_asInt64(state, -1));
    mobius_stack_pop(state, 1);
    
    mobius_stack_getTableField(state, 0, "title");
    printf("  title: %s\n", mobius_stack_asString(state, -1));
    mobius_stack_pop(state, 1);
    
    mobius_stack_setGlobal(state, "config");
    printf("  Created global 'config' table\n\n");
    
    // Test 4: Array operations
    printf("Test 4: Array operations\n");
    mobius_stack_pushNewArray(state, 0);
    
    mobius_stack_pushInt64(state, 10);
    mobius_stack_setArrayElement(state, 0, 0);
    mobius_stack_pushInt64(state, 20);
    mobius_stack_setArrayElement(state, 0, 1);
    mobius_stack_pushInt64(state, 30);
    mobius_stack_setArrayElement(state, 0, 2);
    
    size_t len = mobius_stack_getArrayLength(state, 0);
    printf("  Array length: %zu\n", len);
    
    for (size_t i = 0; i < len; i++) {
        mobius_stack_getArrayElement(state, 0, i);
        printf("  array[%zu] = %" PRId64 "\n", i, mobius_stack_asInt64(state, -1));
        mobius_stack_pop(state, 1);
    }
    
    mobius_stack_setGlobal(state, "numbers");
    printf("  Created global 'numbers' array\n\n");
    
    // Test 5: Type checking
    printf("Test 5: Type checking\n");
    mobius_stack_pushInt64(state, 42);
    printf("  Is integer: %s\n", mobius_stack_isInteger(state, -1) ? "yes" : "no");
    printf("  Is float: %s\n", mobius_stack_isFloat(state, -1) ? "yes" : "no");
    printf("  Is string: %s\n", mobius_stack_isString(state, -1) ? "yes" : "no");
    mobius_stack_pop(state, 1);
    
    mobius_stack_pushString(state, "test");
    printf("  Is string: %s\n", mobius_stack_isString(state, -1) ? "yes" : "no");
    mobius_stack_pop(state, 1);
    printf("\n");
    
    // Test 6: Copy operation
    printf("Test 6: Copy operation\n");
    mobius_stack_pushInt64(state, 123);
    mobius_stack_copy(state, 0);
    printf("  Stack size after copy: %d\n", mobius_stack_size(state));
    assert(mobius_stack_size(state) == 2);
    printf("  Original: %" PRId64 "\n", mobius_stack_asInt64(state, 0));
    printf("  Copy: %" PRId64 "\n", mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    printf("\n");
    
    // Cleanup
    mobius_free_state(state);
    
    printf("=== All tests passed! ===\n");
    return 0;
}

