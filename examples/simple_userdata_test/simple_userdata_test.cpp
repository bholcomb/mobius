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

static TestObject* get_test_object(MobiusState* state, int idx) {
    const char* type_name = NULL;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, "TestObject") != 0) {
        return NULL;
    }
    return (TestObject*)ptr;
}

static void test_object_destructor(void* ptr) {
    if (ptr) {
        TestObject* obj = (TestObject*)ptr;
        printf("Destroying TestObject id=%d name='%s'\n", obj->id, obj->name);
        free(obj);
    }
}

static int test_object_describe(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "test_object_describe expects 1 argument");
    TestObject* obj = get_test_object(state, -1);
    if (!obj) return mobius_error(state, "test_object_describe expects a TestObject");
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "TestObject#%d(%s)", obj->id, obj->name);
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, buffer);
    return 1;
}

static int test_object_kind(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "test_object_kind expects 1 argument");
    TestObject* obj = get_test_object(state, -1);
    if (!obj) return mobius_error(state, "test_object_kind expects a TestObject");
    (void)obj;
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, "test-object");
    return 1;
}

static int test_object_id(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "test_object_id expects 1 argument");
    TestObject* obj = get_test_object(state, -1);
    if (!obj) return mobius_error(state, "test_object_id expects a TestObject");
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, obj->id);
    return 1;
}

static int generic_userdata_kind(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "generic_userdata_kind expects 1 argument");
    if (!mobius_stack_isUserdata(state, -1)) {
        return mobius_error(state, "generic_userdata_kind expects userdata");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, "userdata-fallback");
    return 1;
}

int main() {
    printf("=== Simple Userdata Test ===\n\n");

    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        printf("Failed to create Mobius state\n");
        return 1;
    }
    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        printf("Failed to initialize stdlib\n");
        mobius_free_state(state);
        return 1;
    }

    mobius_register_function(state, "test_object_describe", test_object_describe);
    mobius_register_function(state, "test_object_kind", test_object_kind);
    mobius_register_function(state, "test_object_id", test_object_id);
    mobius_register_function(state, "generic_userdata_kind", generic_userdata_kind);

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

    printf("\n5. Registering userdata prototypes and running script checks...\n");
    const char* setup_code =
        "var UserdataFallback = { fallback_kind: generic_userdata_kind }\n"
        "var TestObjectBase = { kind: test_object_kind }\n"
        "var TestObjectProto = { describe: test_object_describe, id: test_object_id }\n"
        "setmetatable(TestObjectProto, { __index: TestObjectBase })\n";
    if (mobius_exec_string(state, setup_code) != MOBIUS_OK) {
        printf("Failed to set up userdata prototype tables\n");
        mobius_free_state(state);
        return 1;
    }

    mobius_stack_getGlobal(state, "UserdataFallback");
    mobius_set_type_metatable(state, MOBIUS_VAL_USERDATA);
    mobius_stack_getGlobal(state, "TestObjectProto");
    mobius_set_userdata_type_metatable(state, "TestObject");

    const char* verify_code =
        "if (test_obj1:describe() != \"TestObject#42(TestObject1)\") { exit(10) }\n"
        "if (test_obj1:kind() != \"test-object\") { exit(11) }\n"
        "if (test_obj1:fallback_kind() != \"userdata-fallback\") { exit(12) }\n"
        "var describe = test_obj1.describe\n"
        "if (typeof(describe) != \"function\") { exit(13) }\n"
        "if (describe(test_obj1) != \"TestObject#42(TestObject1)\") { exit(14) }\n"
        "if (test_obj2:id() != 100) { exit(15) }\n";
    if (mobius_exec_string(state, verify_code) != MOBIUS_OK) {
        printf("Userdata prototype verification failed\n");
        mobius_free_state(state);
        return 1;
    }
    printf("   ✅ test_obj1:describe() resolved from TestObjectProto\n");
    printf("   ✅ test_obj1:kind() resolved through chained prototype lookup\n");
    printf("   ✅ test_obj1:fallback_kind() resolved from generic userdata fallback\n");
    printf("   ✅ test_obj1.describe resolved through normal field lookup\n");
    printf("   ✅ test_obj2:id() resolved from the userdata-specific prototype\n");

    printf("\n6. Cleaning up...\n");
    printf("   Freeing state (userdata destructors should run for both objects)\n");

    mobius_free_state(state);

    printf("\n=== Test Completed Successfully ===\n");
    printf("This demonstrates:\n");
    printf("  ✅ Userdata push with custom destructors\n");
    printf("  ✅ Storing userdata as globals via the stack API\n");
    printf("  ✅ Retrieval with mobius_stack_getUserdata\n");
    printf("  ✅ Type checks with mobius_stack_isUserdata and mobius_stack_type\n");
    printf("  ✅ Per-userdata prototype registration by type name\n");
    printf("  ✅ Chained prototype lookup for userdata methods\n");
    printf("  ✅ Generic userdata metatable fallback\n");
    printf("  ✅ Automatic cleanup via destructors when the state is freed\n");

    return 0;
}
