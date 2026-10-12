/* Several modules registered by the host (mobius_register_module). The
 * registry kept the module table without a reference of its own, so the
 * next value pushed freed it, and importing the first of two modules
 * crashed. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

/* Returns its userdata as an integer. */
static int which(MobiusState* s, int argc, void* userdata) {
    mobius_stack_pop(s, argc);
    mobius_stack_pushInt64(s, (int64_t)(intptr_t)userdata);
    return 1;
}

static void push_module(MobiusState* s, intptr_t id) {
    mobius_stack_pushNewTable(s, 2);
    mobius_stack_pushFunction(s, which, (void*)id);
    mobius_stack_setTableField(s, -2, "f");
    mobius_stack_pushInt64(s, id * 10);
    mobius_stack_setTableField(s, -2, "n");
}

static int run(int workers) {
    MobiusConfig config = mobius_default_config();
    if (workers >= 0) config.max_worker_threads = workers;
    MobiusState* s = mobius_new_state(&config);
    CHECK(s, "create state");
    mobius_init_stdlib(s);

    push_module(s, 1);
    CHECK(mobius_register_module(s, "one") == MOBIUS_OK, "register one");
    push_module(s, 2);
    CHECK(mobius_register_module(s, "two") == MOBIUS_OK, "register two");
    push_module(s, 3);
    CHECK(mobius_register_module(s, "three") == MOBIUS_OK, "register three");
    /* More values over the popped slots. */
    for (int i = 0; i < 8; i++) mobius_stack_pushNewTable(s, 1);
    mobius_stack_pop(s, 8);

    CHECK(mobius_exec_string(s,
        "import \"one\"\n"
        "import \"two\"\n"
        "import \"three\"\n"
        "if (one.f() != 1 || two.f() != 2 || three.f() != 3) { throw \"wrong functions\" }\n"
        "if (one.n + two.n + three.n != 60) { throw \"wrong values\" }\n"
        "func get(n) { return n == 1 ? one.f() : two.f() }\n"
        "var fs = [spawn get(1), spawn get(2)]\n"
        "if (await fs[0] + await fs[1] != 3) { throw \"wrong from fibers\" }\n") == MOBIUS_OK,
        "import several host modules");

    /* Registering a name again replaces the module for later imports. */
    push_module(s, 4);
    CHECK(mobius_register_module(s, "two") == MOBIUS_OK, "register two again");
    mobius_stack_pushNewTable(s, 1);
    mobius_stack_pop(s, 1);
    CHECK(mobius_exec_string(s,
        "import \"two\"\n"
        "if (two.f() != 4) { throw \"not replaced\" }\n"
        "import \"one\"\n"
        "if (one.f() != 1) { throw \"one lost\" }\n") == MOBIUS_OK,
        "replaced module");

    mobius_free_state(s);
    return 0;
}

int main(void) {
    if (run(0)) return 1;
    if (run(-1)) return 1;
    printf("host modules: PASS\n");
    return 0;
}
