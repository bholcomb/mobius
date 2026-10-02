/* Host functions are read-only globals, like the built-ins. They used to
 * be plain writable globals, which spawned fibers may not use, so a fiber
 * could not call any function the host registered. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static int add_one(MobiusState* s, int argc) {
    int64_t v = mobius_stack_getInt64(s, 0);
    mobius_stack_pop(s, argc);
    mobius_stack_pushInt64(s, v + 1);
    return 1;
}

static int add_two(MobiusState* s, int argc) {
    int64_t v = mobius_stack_getInt64(s, 0);
    mobius_stack_pop(s, argc);
    mobius_stack_pushInt64(s, v + 2);
    return 1;
}

static void quiet(MobiusState* s, const MobiusError* e, void* ud) { (void)s; (void)e; (void)ud; }

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_register_function(s, "bump", add_one);

    CHECK(mobius_exec_string(s,
        "func work(n) { return bump(n) }\n"
        "var fs = []\n"
        "for (var i = 0; i < 8; i++) { fs:push(spawn work(i)) }\n"
        "var total = 0\n"
        "for (var f in fs) { total = total + await f }\n"
        "if (total != 36) { throw \"wrong total\" }\n") == MOBIUS_OK,
        "spawned fibers call a host function");

    mobius_set_error_handler(s, quiet, NULL);
    CHECK(mobius_exec_string(s, "bump = 5") != MOBIUS_OK, "scripts can't reassign it");
    mobius_set_error_handler(s, NULL, NULL);

    mobius_register_function(s, "bump", add_two);
    CHECK(mobius_exec_string(s, "if (bump(1) != 3) { throw \"not replaced\" }") == MOBIUS_OK,
          "registering again replaces it");

    mobius_free_state(s);
    printf("register function: PASS\n");
    return 0;
}
