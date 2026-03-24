/*
 * Simple Userdata Test
 *
 * Demonstrates userdata using only the public embedding and stack APIs:
 * push, globals, getUserdata, type checks, and destructor cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>

typedef struct {
    int id;
    char name[32];
    double value;
} TestObject;

static void test_object_destructor(void* ptr) {
    if (ptr) {
        TestObject* obj = (TestObject*)ptr;
        printf("Destroying TestObject id=%d name='%s'\n", obj->id, obj->name);
        free(obj);
    }
}

int main() {
    printf("=== Simple Userdata Test ===\n\n");

    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        printf("Failed to create Mobius state\n");
        return 1;
    }

    printf("1. Creating test objects...\n");

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

    printf("\n2. Pushing userdata and storing as globals...\n");

    mobius_stack_pushUserdata(state, obj1, test_object_destructor, "TestObject", sizeof(TestObject));
    mobius_stack_setGlobal(state, "test_obj1");
    mobius_stack_pushUserdata(state, obj2, test_object_destructor, "TestObject", sizeof(TestObject));
    mobius_stack_setGlobal(state, "test_obj2");
    printf("   Stored test_obj1 and test_obj2 in the global environment\n");

    printf("\n3. Retrieving and verifying via the stack API...\n");

    mobius_stack_getGlobal(state, "test_obj1");
    assert(mobius_stack_isUserdata(state, -1));
    assert(mobius_stack_type(state, -1) == MOBIUS_VAL_USERDATA);

    const char* type_name = NULL;
    TestObject* retrieved1 = (TestObject*)mobius_stack_getUserdata(state, -1, &type_name);
    assert(retrieved1 == obj1);
    assert(type_name != NULL && strcmp(type_name, "TestObject") == 0);
    printf("   Retrieved test_obj1: id=%d, name='%s', value=%f (type_name='%s')\n",
           retrieved1->id, retrieved1->name, retrieved1->value, type_name);
    mobius_stack_pop(state, 1);

    mobius_stack_getGlobal(state, "test_obj2");
    assert(mobius_stack_isUserdata(state, -1));
    assert(mobius_stack_type(state, -1) == MOBIUS_VAL_USERDATA);

    type_name = NULL;
    TestObject* retrieved2 = (TestObject*)mobius_stack_getUserdata(state, -1, &type_name);
    assert(retrieved2 == obj2);
    assert(type_name != NULL && strcmp(type_name, "TestObject") == 0);
    printf("   Retrieved test_obj2: id=%d, name='%s', value=%f (type_name='%s')\n",
           retrieved2->id, retrieved2->name, retrieved2->value, type_name);
    mobius_stack_pop(state, 1);

    printf("\n4. Type checking (mobius_stack_isUserdata / mobius_stack_type)...\n");

    mobius_stack_getGlobal(state, "test_obj1");
    printf("   test_obj1 is userdata: %s\n", mobius_stack_isUserdata(state, -1) ? "true" : "false");
    printf("   test_obj1 stack type is MOBIUS_VAL_USERDATA: %s\n",
           (mobius_stack_type(state, -1) == MOBIUS_VAL_USERDATA) ? "true" : "false");
    mobius_stack_pop(state, 1);

    printf("\n5. Cleaning up...\n");
    printf("   Freeing state (userdata destructors should run for both objects)\n");

    mobius_free_state(state);

    printf("\n=== Test Completed Successfully ===\n");
    printf("This demonstrates:\n");
    printf("  ✅ Userdata push with custom destructors\n");
    printf("  ✅ Storing userdata as globals via the stack API\n");
    printf("  ✅ Retrieval with mobius_stack_getUserdata\n");
    printf("  ✅ Type checks with mobius_stack_isUserdata and mobius_stack_type\n");
    printf("  ✅ Automatic cleanup via destructors when the state is freed\n");

    return 0;
}
