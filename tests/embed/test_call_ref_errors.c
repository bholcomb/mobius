/* mobius_call_ref reports a failed call (here: wrong argument count) as a
 * negative result. callValue returned the positive error code, which
 * mobius_call_ref took for a count of results: the call "succeeded".
 *
 * A call that fails while running leaves nothing behind: each failed call
 * from the host used to keep its frame, until calls hit max_call_depth and
 * every script call failed with "Stack overflow". */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>

static void quiet(MobiusState* s, const MobiusError* e, void* ud) { (void)s; (void)e; (void)ud; }

static int call(MobiusState* s, const char* name, int nargs) {
    mobius_stack_getGlobal(s, name);
    MobiusValueRef fn = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    mobius_stack_pushInt64(s, 1);
    MobiusValueRef arg = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    int before = mobius_stack_size(s);
    int rc = mobius_call_ref(s, fn, &arg, (size_t)nargs, 1);
    if (rc < 0 && mobius_stack_size(s) != before) { printf("FAIL: results pushed for a failed call\n"); return -2; }
    mobius_stack_pop(s, mobius_stack_size(s) - before);
    mobius_unref_value(s, fn);
    mobius_unref_value(s, arg);
    return rc;
}

int main(void) {
    MobiusConfig config = mobius_default_config();
    config.max_call_depth = 100;
    MobiusState* s = mobius_new_state(&config);
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, quiet, NULL);
    mobius_exec_string(s,
        "func greet(who) { return \"hi \" + who }\n"
        "func fails(n) { throw \"no\" }\n"
        "func inner(n) { return n }\n"
        "func outer(n) { var r = inner(n)\n return r }");
    int rc = call(s, "greet", 0);
    if (rc >= 0) { printf("FAIL: call with a missing argument returned %d\n", rc); return 1; }
    if (rc == -2) return 1;
    for (int i = 0; i < 1000; i++) {
        if (call(s, "fails", 1) >= 0) { printf("FAIL: a throwing call succeeded\n"); return 1; }
    }
    if (call(s, "outer", 1) != 1) { printf("FAIL: calls fail after many failed calls\n"); return 1; }
    mobius_free_state(s);
    printf("call_ref errors: PASS\n");
    return 0;
}
