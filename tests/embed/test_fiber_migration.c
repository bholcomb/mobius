/* Fibers move between worker threads: one that yields, sleeps, waits on a
 * channel or awaits another fiber may resume on a different thread. Here
 * many fibers do all of these over and over on several workers, and after
 * every resume check that they see their own state: their locals, the
 * strings and tables they built, and (through a host function, which reads
 * its arguments through the running fiber's VM) their own arguments. A
 * context switch or thread-local that isn't safe across the move shows up
 * as a wrong value or a crash. The test also fails if no fiber ever
 * changed threads, as it would then prove nothing. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_int next_thread_number = 1;
static _Thread_local int this_thread_number;   /* 0 until first asked */

/* where(id): id * 1000 + a number naming the calling OS thread. */
static int where(MobiusState* s, int argc, void* userdata) {
    (void)userdata;
    int64_t id = mobius_stack_getInt64(s, 0);
    mobius_stack_pop(s, argc);
    if (!this_thread_number) this_thread_number = atomic_fetch_add(&next_thread_number, 1);
    mobius_stack_pushInt64(s, id * 1000 + this_thread_number);
    return 1;
}

static const char* functions =
    "func square(x) {\n"
    "    yield\n"
    "    return x * x\n"
    "}\n"
    "\n"
    "// Returns how many times this fiber found itself on another thread.\n"
    "func worker(id, rounds, ch) {\n"
    "    var moves = 0\n"
    "    var thread = where(id) % 1000\n"
    "    var text = \"\"\n"
    "    var items = []\n"
    "    var sum = 0\n"
    "    for (var r = 0; r < rounds; r++) {\n"
    "        var kind = r % 4\n"
    "        if (kind == 0) { yield }\n"
    "        else if (kind == 1) { fiber.sleep(1) }\n"
    "        else if (kind == 2) { ch:send(id); ch:recv() }\n"
    "        else {\n"
    "            var sq = await spawn square(r)\n"
    "            if (sq != r * r) { throw \"await: wrong square\" }\n"
    "        }\n"
    "        var w = where(id)\n"
    "        if ((w - w % 1000) / 1000 != id) { throw \"host function saw another fiber's argument\" }\n"
    "        if (w % 1000 != thread) { moves = moves + 1; thread = w % 1000 }\n"
    "        text = text + str(r % 10)\n"
    "        items:push({id: id, round: r, label: \"f\" + str(id) + \"r\" + str(r)})\n"
    "        sum = sum + r\n"
    "        if (size(text) != r + 1) { throw \"lost part of a string\" }\n"
    "        for (var i = 0; i <= r; i++) {\n"
    "            var item = items[i]\n"
    "            if (item.id != id || item.round != i) { throw \"a table changed\" }\n"
    "            if (item.label != \"f\" + str(id) + \"r\" + str(i)) { throw \"a label changed\" }\n"
    "            // No switch since w: same fiber, same thread.\n"
    "            if (where(id) != w) { throw \"a host function saw another fiber, or the thread changed\" }\n"
    "        }\n"
    "    }\n"
    "    if (sum != rounds * (rounds - 1) / 2) { throw \"wrong sum\" }\n"
    "    return moves\n"
    "}\n"
    "\n"
    "func run(fibers, rounds) {\n"
    "    var ch = fiber.channel(fibers)\n"
    "    var fs = []\n"
    "    for (var i = 0; i < fibers; i++) { fs:push(spawn worker(i, rounds, ch)) }\n"
    "    var moves = 0\n"
    "    for (var f in fs) { moves = moves + await f }\n"
    "    print(\"fiber moves between threads: \" + str(moves))\n"
    "    if (moves == 0) { throw \"no fiber changed threads: the test proved nothing\" }\n"
    "}\n";

int main(void) {
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = 4;
    MobiusState* state = mobius_new_state(&config);
    if (!state) return 1;
    mobius_init_stdlib(state);
    mobius_register_function(state, "where", where, NULL);

    if (mobius_exec_string(state, functions) != MOBIUS_OK) { printf("functions failed\n"); return 1; }
    /* Several runs: the workers and the pooled fibers are reused. */
    for (int run = 0; run < 3; run++) {
        int rc = mobius_exec_string(state, "run(64, 60)");
        if (rc != MOBIUS_OK) { printf("run %d failed: %d\n", run, rc); return 1; }
    }

    mobius_free_state(state);
    printf("fiber migration: PASS\n");
    return 0;
}
