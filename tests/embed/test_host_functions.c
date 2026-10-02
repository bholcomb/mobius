/* Host functions carry userdata (one C dispatcher serves many functions,
 * as a C# binding needs), keep their identity per userdata, and stay on
 * their thread while they run: a fiber running a host function is never
 * moved to another thread. A script it calls back may sleep (blocking the
 * thread), but waiting for another fiber there is an error. */
#define _POSIX_C_SOURCE 200809L
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

/* One dispatcher: the userdata says which "managed" function this is. */
static int dispatcher(MobiusState* s, int argc) {
    long which = (long)(intptr_t)mobius_function_userdata(s);
    int64_t x = argc > 0 ? mobius_stack_getInt64(s, 0) : 0;
    mobius_stack_pop(s, argc);
    mobius_stack_pushInt64(s, which * 1000 + x);
    return 1;
}

static MobiusValueRef callback_ref;
static int moved;

/* Runs on a fiber; calls back into the script function in callback_ref. */
static int hold(MobiusState* s, int argc) {
    mobius_stack_pop(s, argc);
    pthread_t before = pthread_self();
    int rc = mobius_call_ref(s, callback_ref, NULL, 0, 1);
    if (!pthread_equal(before, pthread_self())) moved++;
    if (rc < 0) return rc;
    return 1;   /* the callback's result */
}

static MobiusValueRef ref_global(MobiusState* s, const char* name) {
    mobius_stack_getGlobal(s, name);
    MobiusValueRef ref = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    return ref;
}

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    CHECK(s, "create state");
    mobius_init_stdlib(s);

    mobius_register_function_ex(s, "one", dispatcher, (void*)(intptr_t)1, 0);
    mobius_register_function_ex(s, "two", dispatcher, (void*)(intptr_t)2, 0);
    CHECK(mobius_exec_string(s,
        "if (one(5) != 1005 || two(5) != 2005) { throw \"userdata\" }\n"
        "if (one == two) { throw \"different userdata compare equal\" }\n"
        "var t = {}\n"
        "t[one] = \"a\"\n"
        "t[two] = \"b\"\n"
        "if (t[one] != \"a\" || t[two] != \"b\") { throw \"table keys\" }\n") == MOBIUS_OK, "userdata dispatch");

    /* Pushed into a module table; the same triple is the same function. */
    mobius_stack_pushNewTable(s, 4);
    mobius_stack_pushFunction(s, dispatcher, (void*)(intptr_t)3, 0);
    mobius_stack_setTableField(s, -2, "three");
    mobius_stack_pushFunction(s, dispatcher, (void*)(intptr_t)1, 0);
    mobius_stack_setTableField(s, -2, "also_one");
    CHECK(mobius_register_module(s, "api") == MOBIUS_OK, "register module");
    CHECK(mobius_exec_string(s,
        "import \"api\"\n"
        "if (api.three(1) != 3001) { throw \"pushed function\" }\n"
        "if (api.also_one != one) { throw \"same function, same identity\" }\n"
        "func via_fiber(n) { return api.three(n) }\n"
        "if (await spawn via_fiber(2) != 3002) { throw \"from a fiber\" }\n") == MOBIUS_OK,
        "pushed functions");

    /* A callback may sleep: the thread blocks, the fiber doesn't move. */
    mobius_register_function_ex(s, "hold", hold, NULL, 0);
    CHECK(mobius_exec_string(s,
        "func napper() {\n"
        "    fiber.sleep(5)\n"
        "    return 7\n"
        "}\n"
        "func impatient() {\n"
        "    func inner() { fiber.sleep(100)\n return 1 }\n"
        "    return await spawn inner()\n"
        "}\n") == MOBIUS_OK, "define callbacks");
    callback_ref = ref_global(s, "napper");
    CHECK(mobius_exec_string(s,
        "func worker() { return hold() }\n"
        "var fs = []\n"
        "for (var i = 0; i < 16; i++) { fs:push(spawn worker()) }\n"
        "var sum = 0\n"
        "for (var f in fs) { sum = sum + await f }\n"
        "if (sum != 112) { throw \"callback results\" }\n") == MOBIUS_OK, "host functions in fibers");
    CHECK(moved == 0, "fiber stayed on its thread during the host call");

    /* Waiting for another fiber inside a host function is a clear error. */
    callback_ref = ref_global(s, "impatient");
    CHECK(mobius_exec_string(s,
        "func try_wait() {\n"
        "    var msg = \"\"\n"
        "    try { hold() } catch e { msg = str(e) }\n"
        "    return msg\n"
        "}\n"
        "var m = await spawn try_wait()\n"
        "if (!contains(m, \"cannot wait for another fiber inside a host function\")) { throw \"wrong: \" + m }\n") == MOBIUS_OK,
        "await inside a host function");

    mobius_free_state(s);
    printf("host functions: PASS\n");
    return 0;
}
