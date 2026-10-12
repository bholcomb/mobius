/* A script function the host calls (mobius_call_ref) may spawn fibers and
 * wait: await, yield, fiber.sleep, channels. The call ran on the host's
 * thread outside any fiber, where waiting could only spin, so with
 * max_worker_threads = 0, where nothing else runs the spawned fibers, it
 * never returned. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL (%d workers): %s\n", workers, what); return 1; } } while (0)

static void quiet(MobiusState* s, const MobiusError* e, void* ud) { (void)s; (void)e; (void)ud; }

/* Calls the global `name` with no arguments; returns the call's result
 * code and its integer result in *out. */
static int call(MobiusState* s, const char* name, int64_t* out) {
    mobius_stack_getGlobal(s, name);
    MobiusValueRef fn = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    int before = mobius_stack_size(s);
    int rc = mobius_call_ref(s, fn, NULL, 0, 1);
    if (rc > 0) {
        *out = mobius_stack_asInt64(s, -1);
        mobius_stack_pop(s, mobius_stack_size(s) - before);
    }
    mobius_unref_value(s, fn);
    return rc;
}

static int run(int workers) {
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = workers;
    MobiusState* s = mobius_new_state(&config);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, quiet, NULL);

    CHECK(mobius_exec_string(s,
        "func h() { return 3 }\n"
        "func spawn_await() {\n"
        "    var x = spawn h()\n"
        "    return await x\n"
        "}\n"
        "func slow(n) {\n"
        "    fiber.sleep(2)\n"
        "    yield\n"
        "    return n\n"
        "}\n"
        "func many() {\n"
        "    var fs = []\n"
        "    for (var i = 0; i < 20; i++) { fs:push(spawn slow(i)) }\n"
        "    var total = 0\n"
        "    for (var f in fs) { total = total + await f }\n"
        "    var both = fiber.all([spawn slow(1), spawn slow(2)])\n"
        "    return total + both[0] + both[1]\n"
        "}\n"
        "func send(ch) { for (var i = 1; i <= 4; i++) { ch:send(i) } }\n"
        "func channel() {\n"
        "    var ch = fiber.channel()\n"
        "    spawn send(ch)\n"
        "    var total = 0\n"
        "    for (var i = 0; i < 4; i++) { total = total + ch:recv() }\n"
        "    return total\n"
        "}\n"
        "func fail() { throw \"spawned failure\" }\n"
        "func await_failure() { return await spawn fail() }\n"
        "func sleep() { fiber.sleep(1)\n return 7 }\n") == MOBIUS_OK, "define functions");

    int64_t r = 0;
    for (int i = 0; i < 3; i++) {
        CHECK(call(s, "spawn_await", &r) == 1 && r == 3, "spawn and await");
    }
    CHECK(call(s, "many", &r) == 1 && r == 193, "many fibers, sleep, yield, fiber.all");
    CHECK(call(s, "channel", &r) == 1 && r == 10, "channel");
    CHECK(call(s, "sleep", &r) == 1 && r == 7, "fiber.sleep");
    CHECK(call(s, "await_failure", &r) < 0, "a spawned fiber's error fails the call");
    CHECK(call(s, "spawn_await", &r) == 1 && r == 3, "calls work after a failed call");

    /* exec from the host still waits as before, after calls. */
    CHECK(mobius_exec_string(s, "if (await spawn h() != 3) { throw \"exec\" }\n") == MOBIUS_OK,
          "exec with spawn and await");

    mobius_free_state(s);
    return 0;
}

int main(void) {
    if (run(0)) return 1;
    if (run(2)) return 1;
    printf("host call fibers: PASS\n");
    return 0;
}
