/* max_worker_threads = 0: the calling thread runs every fiber itself.
 * It used to wait forever for a worker that was never started. */
#include <mobius/mobius.h>
#include <stdio.h>

int main(void) {
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = 0;
    MobiusState* state = mobius_new_state(&config);
    if (!state) return 1;
    mobius_init_stdlib(state);

    int rc = mobius_exec_string(state,
        "func twice(x) {\n"
        "    fiber.sleep(5)\n"
        "    yield\n"
        "    return x * 2\n"
        "}\n"
        "var fs = []\n"
        "for (var i = 0; i < 20; i++) { fs:push(spawn twice(i)) }\n"
        "var total = 0\n"
        "for (var f in fs) { total = total + await f }\n"
        "if (total != 380) { throw \"wrong total \" + str(total) }\n"
        "var all = fiber.all([spawn twice(1), spawn twice(2)])\n"
        "if (all[0] + all[1] != 6) { throw \"wrong fiber.all\" }\n");
    if (rc != MOBIUS_OK) { printf("first run failed: %d\n", rc); return 1; }

    /* The state stays usable for a second run. */
    rc = mobius_exec_string(state, "var g = spawn twice(21)\nif (await g != 42) { throw \"second run\" }\n");
    if (rc != MOBIUS_OK) { printf("second run failed: %d\n", rc); return 1; }

    mobius_free_state(state);
    printf("zero workers: PASS\n");
    return 0;
}
