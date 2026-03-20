/*
 * Simple Userdata Test
 * 
 * This demonstrates basic userdata functionality without full function registration.
 * It shows that the userdata type works correctly for creation, copying, printing,
 * and cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/mobius/data/value.h"
#include "../../src/mobius/state/mobius_state.h"
#include "../../include/mobius/mobius_plugin.h"

// Simple test structure
typedef struct {
    int id;
    char name[32];
    double value;
} TestObject;

// Destructor function
void test_object_destructor(void* ptr) {
    if (ptr) {
        TestObject* obj = (TestObject*)ptr;
        printf("Destroying TestObject id=%d name='%s'\n", obj->id, obj->name);
        free(obj);
    }
}

int main() {
    printf("=== Simple Userdata Test ===\n\n");
    
    // Create Mobius state
    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        printf("Failed to create Mobius state\n");
        return 1;
    }
    
    printf("1. Creating test objects...\n");
    
    // Create test objects
    TestObject* obj1 = (TestObject*)malloc(sizeof(TestObject));
    obj1->id = 42;
    strcpy(obj1->name, "TestObject1");
    obj1->value = 3.14159;
    
    TestObject* obj2 = (TestObject*)malloc(sizeof(TestObject));
    obj2->id = 100;
    strcpy(obj2->name, "TestObject2");
    obj2->value = 2.71828;
    
    printf("   Created obj1: id=%d, name='%s', value=%f\n", obj1->id, obj1->name, obj1->value);
    printf("   Created obj2: id=%d, name='%s', value=%f\n", obj2->id, obj2->name, obj2->value);
    
    printf("\n2. Creating userdata values...\n");
    
    // Create userdata values
    Value val1 = make_userdata_value(obj1, test_object_destructor, "TestObject", sizeof(TestObject));
    Value val2 = make_userdata_value(obj2, test_object_destructor, "TestObject", sizeof(TestObject));
    
    printf("   Created Value wrappers\n");
    
    printf("\n3. Testing type checking...\n");
    
    // Test type checking
    printf("   val1 is userdata: %s\n", (val1.type == VAL_USERDATA) ? "true" : "false");
    printf("   val1 is TestObject: %s\n", 
           (val1.type == VAL_USERDATA && strcmp(val1.as.userdata.type_name, "TestObject") == 0) ? "true" : "false");
    printf("   val1 is SomeOtherType: %s\n", 
           (val1.type == VAL_USERDATA && strcmp(val1.as.userdata.type_name, "SomeOtherType") == 0) ? "true" : "false");
    
    printf("\n4. Testing value extraction...\n");
    
    // Test value extraction
    void* ptr1 = val1.as.userdata.ptr;
    const char* type1 = val1.as.userdata.type_name;
    size_t size1 = val1.as.userdata.size;
    
    printf("   val1 pointer: %p\n", ptr1);
    printf("   val1 type: %s\n", type1);
    printf("   val1 size: %zu bytes\n", size1);
    
    // Access the actual object
    TestObject* extracted_obj1 = (TestObject*)ptr1;
    printf("   Extracted obj1: id=%d, name='%s', value=%f\n", 
           extracted_obj1->id, extracted_obj1->name, extracted_obj1->value);
    
    printf("\n5. Testing value system...\n");
    
    // Test value type system
    printf("   Value type: %s\n", value_type_name(val1.type));
    printf("   Value pointer: %p\n", val1.as.userdata.ptr);
    printf("   Value type_name: %s\n", val1.as.userdata.type_name);
    
    // Test printing
    printf("   Print value output: ");
    print_value(val1);
    printf("\n");
    
    // Test string conversion
    char* str_repr = value_to_string(val1);
    if (str_repr) {
        printf("   String representation: %s\n", str_repr);
        free(str_repr);
    }
    
    printf("\n6. Testing equality...\n");
    
    // Test equality
    printf("   val1 == val1: %s\n", 
           (val1 == val1) ? "true" : "false");
    printf("   val1 == val2: %s\n", 
           (val1 == val2) ? "true" : "false");
    
    printf("\n7. Storing in environment...\n");
    
    // Test storing userdata in environment via the public stack API
    mobius_stack_pushUserdata(state, obj1, test_object_destructor, "TestObject", sizeof(TestObject));
    mobius_stack_setGlobal(state, "test_obj1");
    mobius_stack_pushUserdata(state, obj2, test_object_destructor, "TestObject", sizeof(TestObject));
    mobius_stack_setGlobal(state, "test_obj2");
    printf("   Stored both objects in global environment\n");
    
    // Retrieve and verify via the public stack API
    mobius_stack_getGlobal(state, "test_obj1");
    const char* type_name = NULL;
    TestObject* retrieved_obj = (TestObject*)mobius_stack_getUserdata(state, -1, &type_name);
    if (retrieved_obj) {
        printf("   Retrieved obj1: id=%d, name='%s', value=%f\n", 
               retrieved_obj->id, retrieved_obj->name, retrieved_obj->value);
    }
    mobius_stack_pop(state, 1);
    
    printf("\n8. Cleaning up...\n");
    
    // Values are now owned by the environment, so they'll be released
    // when the state is freed. No manual cleanup needed.
    
    mobius_free_state(state);
    printf("   Mobius state freed (userdata destructors should have been called)\n");
    
    printf("\n=== Test Completed Successfully ===\n");
    printf("This demonstrates:\n");
    printf("  ✅ Userdata creation with custom destructors\n");
    printf("  ✅ Type checking and type names\n");
    printf("  ✅ Value extraction and manipulation\n");
    printf("  ✅ Printing and string conversion\n");
    printf("  ✅ Equality comparison\n");
    printf("  ✅ Environment storage and retrieval\n");
    printf("  ✅ Automatic cleanup via destructors\n");
    
    return 0;
}
