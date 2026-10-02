/* mobius_call_ref reports a failed call (here: wrong argument count) as a
 * negative result. callValue returned the positive error code, which
 * mobius_call_ref took for a count of results: the call "succeeded". */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>

static void quiet(MobiusState* s, const MobiusError* e, void* ud) { (void)s; (void)e; (void)ud; }

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, quiet, NULL);
    mobius_exec_string(s, "func greet(who) { return \"hi \" + who }");
    mobius_stack_getGlobal(s, "greet");
    MobiusValueRef fn = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    int before = mobius_stack_size(s);
    int rc = mobius_call_ref(s, fn, NULL, 0, 1);
    if (rc >= 0) { printf("FAIL: call with a missing argument returned %d\n", rc); return 1; }
    if (mobius_stack_size(s) != before) { printf("FAIL: results pushed for a failed call\n"); return 1; }
    mobius_free_state(s);
    printf("call_ref errors: PASS\n");
    return 0;
}
