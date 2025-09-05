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
#include "../src/mobius/mobius.h"

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
    MobiusState* state = mobius_new_state();
    if (!state) {
        printf("Failed to create Mobius state\n");
        return 1;
    }
    
    printf("1. Creating test objects...\n");
    
    // Create test objects
    TestObject* obj1 = malloc(sizeof(TestObject));
    obj1->id = 42;
    strcpy(obj1->name, "TestObject1");
    obj1->value = 3.14159;
    
    TestObject* obj2 = malloc(sizeof(TestObject));
    obj2->id = 100;
    strcpy(obj2->name, "TestObject2");
    obj2->value = 2.71828;
    
    printf("   Created obj1: id=%d, name='%s', value=%f\n", obj1->id, obj1->name, obj1->value);
    printf("   Created obj2: id=%d, name='%s', value=%f\n", obj2->id, obj2->name, obj2->value);
    
    printf("\n2. Creating userdata values...\n");
    
    // Create userdata values
    MobiusValue* val1 = mobius_create_userdata(state, obj1, test_object_destructor, "TestObject", sizeof(TestObject));
    MobiusValue* val2 = mobius_create_userdata(state, obj2, test_object_destructor, "TestObject", sizeof(TestObject));
    
    if (!val1 || !val2) {
        printf("Failed to create userdata values\n");
        free(obj1);
        free(obj2);
        mobius_free_state(state);
        return 1;
    }
    
    printf("   Created MobiusValue wrappers\n");
    
    printf("\n3. Testing type checking...\n");
    
    // Test type checking
    printf("   val1 is userdata: %s\n", mobius_is_userdata(val1) ? "true" : "false");
    printf("   val1 is TestObject: %s\n", mobius_is_userdata_type(val1, "TestObject") ? "true" : "false");
    printf("   val1 is SomeOtherType: %s\n", mobius_is_userdata_type(val1, "SomeOtherType") ? "true" : "false");
    
    printf("\n4. Testing value extraction...\n");
    
    // Test value extraction
    void* ptr1 = mobius_to_userdata(val1);
    const char* type1 = mobius_userdata_type(val1);
    size_t size1 = mobius_userdata_size(val1);
    
    printf("   val1 pointer: %p\n", ptr1);
    printf("   val1 type: %s\n", type1);
    printf("   val1 size: %zu bytes\n", size1);
    
    // Access the actual object
    TestObject* extracted_obj1 = (TestObject*)ptr1;
    printf("   Extracted obj1: id=%d, name='%s', value=%f\n", 
           extracted_obj1->id, extracted_obj1->name, extracted_obj1->value);
    
    printf("\n5. Testing copying...\n");
    
    // Test copying
    MobiusValue* val1_copy = mobius_copy_value(val1);
    if (val1_copy) {
        printf("   Copy successful\n");
        printf("   Copy is userdata: %s\n", mobius_is_userdata(val1_copy) ? "true" : "false");
        printf("   Copy points to same object: %s\n", 
               (mobius_to_userdata(val1_copy) == mobius_to_userdata(val1)) ? "true" : "false");
        
        mobius_free_value(val1_copy);
        printf("   Copy freed (original object should still exist)\n");
    }
    
    printf("\n6. Testing internal value system...\n");
    
    // Test with internal value system
    Value internal_val = val1->internal_value;
    printf("   Internal value type: %s\n", value_type_name(internal_val.type));
    printf("   Internal value pointer: %p\n", internal_val.as.userdata.ptr);
    printf("   Internal value type_name: %s\n", internal_val.as.userdata.type_name);
    
    // Test printing
    printf("   Print value output: ");
    print_value(internal_val);
    printf("\n");
    
    // Test string conversion
    char* str_repr = value_to_string(internal_val);
    if (str_repr) {
        printf("   String representation: %s\n", str_repr);
        free(str_repr);
    }
    
    printf("\n7. Testing equality...\n");
    
    // Test equality
    printf("   val1 == val1: %s\n", 
           values_equal(val1->internal_value, val1->internal_value) ? "true" : "false");
    printf("   val1 == val2: %s\n", 
           values_equal(val1->internal_value, val2->internal_value) ? "true" : "false");
    
    printf("\n8. Cleaning up...\n");
    
    // Cleanup
    mobius_free_value(val1);
    printf("   val1 freed\n");
    
    mobius_free_value(val2);
    printf("   val2 freed\n");
    
    mobius_free_state(state);
    printf("   Mobius state freed\n");
    
    printf("\n=== Test Completed Successfully ===\n");
    return 0;
}
