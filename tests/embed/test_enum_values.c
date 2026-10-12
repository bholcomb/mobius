/* mobius_stack_getEnumValue pushes a value of an enum by its number: a
 * member's, or one no member has (a combination of flags), as plugins
 * return them. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>

static void quiet(MobiusState* s, const MobiusError* e, void* ud) { (void)s; (void)e; (void)ud; }

static int check_script(MobiusState* s, const char* code, const char* what) {
    if (mobius_exec_string(s, code) != MOBIUS_OK) { printf("FAIL: %s: script error\n", what); return 0; }
    mobius_stack_getGlobal(s, "ok");
    int ok = mobius_stack_asBool(s, -1);
    mobius_stack_pop(s, 1);
    if (!ok) printf("FAIL: %s\n", what);
    return ok;
}

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, quiet, NULL);
    mobius_exec_string(s, "enum Mod { SHIFT = 1, CONTROL = 2 }");

    mobius_stack_getGlobal(s, "Mod");
    if (!mobius_stack_getEnumValue(s, -1, 2)) { printf("FAIL: member value\n"); return 1; }
    mobius_stack_setGlobal(s, "control");
    if (!mobius_stack_getEnumValue(s, -1, 3)) { printf("FAIL: flags value\n"); return 1; }
    mobius_stack_setGlobal(s, "both");
    /* A value of the enum finds its definition too. */
    mobius_stack_getGlobal(s, "control");
    if (!mobius_stack_getEnumValue(s, -1, 1)) { printf("FAIL: value from a value\n"); return 1; }
    mobius_stack_setGlobal(s, "shift");
    mobius_stack_pop(s, 1);

    int before = mobius_stack_size(s);
    if (mobius_stack_getEnumValue(s, -1, (int64_t)1 << 40)) { printf("FAIL: 64-bit value accepted\n"); return 1; }
    mobius_stack_pushInt64(s, 7);
    if (mobius_stack_getEnumValue(s, -1, 1)) { printf("FAIL: non-enum accepted\n"); return 1; }
    mobius_stack_pop(s, 1);
    if (mobius_stack_size(s) != before) { printf("FAIL: a failed call changed the stack\n"); return 1; }
    mobius_stack_pop(s, before);

    int ok = check_script(s, "var ok = control == Mod.CONTROL && shift == Mod.SHIFT", "member values")
          && check_script(s, "var ok = typeof(both) == \"enum\" && both != Mod.SHIFT && both != Mod.CONTROL",
                          "flags value");
    mobius_free_state(s);
    if (!ok) return 1;
    printf("enum values: PASS\n");
    return 0;
}
