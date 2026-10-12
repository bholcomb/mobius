/* mobius_set_count_hook: the host stops runaway scripts and enforces
 * instruction budgets. It counts the VM instructions a script executes,
 * where control flow changes (jumps, calls, returns), so a stop may come up
 * to one straight-line run of instructions late; each fiber counts for
 * itself, from zero when the host starts a script, when the fiber is
 * spawned and after each wait.
 *
 * Checked here: every kind of endless loop stops with the hook's message and
 * the script's try/catch can't catch it; a budget per call from the host
 * (counted from the functions' bytecode) is never cut short and is overrun
 * by no more than the longest straight-line run, over many calls, and the
 * state still works after many stopped calls; spawned fibers inherit the
 * hook, each counted separately, on several workers; waits start the count
 * again; removing the hook restores normal running. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* what) {
    printf("FAIL: %s\n", what);
    exit(1);
}

/* The last error reported to the host. */
static pthread_mutex_t error_lock = PTHREAD_MUTEX_INITIALIZER;
static char last_error[512];

static void on_error(MobiusState* s, const MobiusError* e, void* ud) {
    (void)s; (void)ud;
    pthread_mutex_lock(&error_lock);
    snprintf(last_error, sizeof last_error, "%s", e->message ? e->message : "");
    pthread_mutex_unlock(&error_lock);
}

static void expect_error(const char* needle, const char* what) {
    pthread_mutex_lock(&error_lock);
    int found = strstr(last_error, needle) != NULL;
    if (!found) printf("  error was: '%s'\n", last_error);
    last_error[0] = 0;
    pthread_mutex_unlock(&error_lock);
    if (!found) fail(what);
}

/* Hooks. Each counts its calls and remembers the threads it ran on. */
static atomic_long hook_calls;
static pthread_mutex_t threads_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t threads[64];
static int thread_count;

static void note_thread(void) {
    pthread_mutex_lock(&threads_lock);
    pthread_t self = pthread_self();
    int known = 0;
    for (int i = 0; i < thread_count; i++) known |= pthread_equal(threads[i], self);
    if (!known && thread_count < 64) threads[thread_count++] = self;
    pthread_mutex_unlock(&threads_lock);
}

static const char* stop(MobiusState* s, void* ud) {
    (void)s;
    atomic_fetch_add(&hook_calls, 1);
    note_thread();
    return (const char*)ud;   /* the message to stop with */
}

static const char* keep_going(MobiusState* s, void* ud) {
    (void)s; (void)ud;
    atomic_fetch_add(&hook_calls, 1);
    return NULL;
}

static MobiusState* new_state(int workers) {
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = workers;
    config.max_call_depth = 1000;   /* frames a stopped call left behind would add up */
    MobiusState* s = mobius_new_state(&config);
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, on_error, NULL);
    return s;
}

static int64_t global_int(MobiusState* s, const char* name) {
    mobius_stack_getGlobal(s, name);
    int64_t v = mobius_stack_asInt64(s, -1);
    mobius_stack_pop(s, 1);
    return v;
}

/* Call the global function `name`(n) from the host; returns call_ref's result. */
static int call_with(MobiusState* s, const char* name, int64_t n) {
    mobius_stack_getGlobal(s, name);
    MobiusValueRef fn = mobius_ref_value(s, -1);
    mobius_stack_pushInt64(s, n);
    MobiusValueRef arg = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 2);
    int before = mobius_stack_size(s);
    int rc = mobius_call_ref(s, fn, &arg, 1, 1);
    mobius_stack_pop(s, mobius_stack_size(s) - before);
    mobius_unref_value(s, fn);
    mobius_unref_value(s, arg);
    return rc;
}

/* A host function that calls the script function forever() and ignores
 * its failure. */
static int swallow(MobiusState* s, int argc, void* ud) {
    (void)ud;
    mobius_stack_pop(s, argc);
    mobius_stack_getGlobal(s, "forever");
    MobiusValueRef fn = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    int before = mobius_stack_size(s);
    mobius_call_ref(s, fn, NULL, 0, 1);
    mobius_stack_pop(s, mobius_stack_size(s) - before);
    mobius_unref_value(s, fn);
    mobius_clear_error(s);
    return 0;
}

static void endless_loops(void) {
    MobiusState* s = new_state(1);
    mobius_set_count_hook(s, stop, 1000, "too long");
    const char* loops[] = {
        "while (true) {}",
        "for (var i = 0; ; i++) {}",
        "for (var i = 0; i < 1000000000; i++) {}",
        "var t = [1, 2, 3]\nwhile (true) { for (var x in t) {} }",
        "func tail() { return tail() }\ntail()",                               /* tail calls */
        "func tree(n) { if (n == 0) { return 0 } return tree(n - 1) + tree(n - 1) }\ntree(60)",
        "func spin() { while (true) {} }\nvar ok = [1]:any(func(x) { spin() })",   /* through a native */
    };
    for (size_t i = 0; i < sizeof loops / sizeof loops[0]; i++) {
        if (mobius_exec_string(s, loops[i]) != MOBIUS_ERROR_RUNTIME) fail(loops[i]);
        expect_error("instruction limit: too long", loops[i]);
    }

    /* try/catch in the script can't catch it, however deep. */
    mobius_exec_string(s, "caught = false");
    if (mobius_exec_string(s,
            "func inner() { try { while (true) {} } catch e { caught = true } }\n"
            "try { inner() } catch e { caught = true }\n"
            "caught = true") != MOBIUS_ERROR_RUNTIME)
        fail("a loop inside try was not stopped");
    expect_error("instruction limit: too long", "the message inside try");
    mobius_stack_getGlobal(s, "caught");
    if (mobius_stack_asBool(s, -1)) fail("the script caught the stop");
    mobius_stack_pop(s, 1);

    /* A host function that drops the error doesn't free the script: it
     * stops again where it next counts, with the same message. */
    mobius_register_function(s, "swallow", swallow, NULL);
    mobius_exec_string(s, "func forever() { while (true) {} }");
    atomic_store(&hook_calls, 0);
    if (mobius_exec_string(s, "swallow()\nwhile (true) {}") != MOBIUS_ERROR_RUNTIME)
        fail("a dropped stop let the script run on");
    expect_error("instruction limit: too long", "the message after a dropped stop");
    if (atomic_load(&hook_calls) != 1) fail("a dropped stop asked the hook again");

    /* The state works after all that: try/catch catches ordinary errors. */
    if (mobius_exec_string(s,
            "var got = \"\"\n"
            "try { throw \"x\" } catch e { got = e }\n"
            "if (got != \"x\") { throw \"try/catch broken\" }") != MOBIUS_OK)
        fail("try/catch after a stop");

    /* Without the hook, a long loop runs to the end. */
    mobius_set_count_hook(s, NULL, 0, NULL);
    atomic_store(&hook_calls, 0);
    if (mobius_exec_string(s, "n = 0\nfor (var i = 0; i < 3000000; i++) { n = n + 1 }") != MOBIUS_OK)
        fail("a long loop without the hook");
    if (global_int(s, "n") != 3000000 || atomic_load(&hook_calls) != 0) fail("the hook still ran");
    mobius_free_state(s);
}

/* The smallest budget (hook count) the call name(n) from the host runs
 * within, trying budgets from `from` down until one stops it. */
static long smallest_budget(MobiusState* s, const char* name, int64_t n, long from) {
    long budget = from;
    for (;;) {
        if (budget <= 1) fail("no budget stopped the call");
        mobius_set_count_hook(s, stop, (uint32_t)(budget - 1), "over budget");
        if (call_with(s, name, n) < 0) break;
        budget--;
    }
    expect_error("instruction limit: over budget", "the budget's message");
    return budget;
}

static void budget_per_call(void) {
    MobiusState* s = new_state(1);
    mobius_exec_string(s,
        "func spin(n) { for (var i = 0; i < n; i++) {} return n }\n"
        "func step() { return 1 }\n"
        "func calls(n) { var k = 0\n while (k < n) { k = k + step() } return k }");
    /* From their bytecode (bin/mobius --debug), in code words (a fused
     * instruction takes two): spin(n) runs LOADINT, then per round
     * SHARED_LOAD LT MOVE INC JMP, and to leave SHARED_LOAD LT JMP RETURN:
     * 5n + 5. calls(n) runs LOADINT GETGLOBAL, per round LT
     * CALL_DIRECT_PLAIN (two words), step's LOADINT RETURN, ADD_II JMP,
     * and to leave LT JMP RETURN: 7n + 5. In both, the longest run of
     * instructions between two jumps, calls or returns is 3, so a call
     * overruns its budget by at most 3. */
    long spin_words = 5 * 99 + 5, calls_words = 7 * 49 + 5;

    /* A budget of the call's instructions always lets it finish. */
    mobius_set_count_hook(s, stop, (uint32_t)spin_words, "over budget");
    for (int i = 0; i < 1000; i++)
        if (call_with(s, "spin", 99) < 0) fail("spin(99) on a budget of its instructions failed");
    mobius_set_count_hook(s, stop, (uint32_t)calls_words, "over budget");
    for (int i = 0; i < 1000; i++)
        if (call_with(s, "calls", 49) < 0) fail("calls(49) on a budget of its instructions failed");

    /* A smaller one stops it, at most a straight-line run late. */
    long spin_least = smallest_budget(s, "spin", 99, spin_words);
    if (spin_least > spin_words || spin_words - spin_least > 3) {
        printf("  spin(99): %ld instructions, smallest budget %ld\n", spin_words, spin_least);
        fail("spin(99) overran its budget by more than a straight-line run");
    }
    long calls_least = smallest_budget(s, "calls", 49, calls_words);
    if (calls_least > calls_words || calls_words - calls_least > 3) {
        printf("  calls(49): %ld instructions, smallest budget %ld\n", calls_words, calls_least);
        fail("calls(49) overran its budget by more than a straight-line run");
    }
    mobius_set_count_hook(s, stop, 450, "over budget");
    if (call_with(s, "spin", 99) >= 0) fail("spin(99) on a budget of 450 passed");
    expect_error("instruction limit: over budget", "the budget's message");

    /* Many stopped calls leave nothing behind (frames, try blocks). */
    mobius_exec_string(s, "func guarded(n) { try { while (true) {} } catch e {} return n }");
    for (int i = 0; i < 5000; i++)
        if (call_with(s, "guarded", 1) >= 0) fail("guarded() was not stopped");
    if (call_with(s, "calls", 10) < 0) fail("calls(10) after many stopped calls");
    if (mobius_exec_string(s, "var r = spin(50)") != MOBIUS_OK) fail("exec after many stopped calls");

    /* A script called back from a host function counts on its caller:
     * spin(60) is 305 instructions, once called and once as a callback. */
    mobius_set_count_hook(s, NULL, 0, NULL);
    mobius_exec_string(s, "func both(n) { var a = spin(n)\n return [n]:any(spin) }");
    mobius_set_count_hook(s, stop, 500, "over budget");
    if (call_with(s, "both", 60) >= 0) fail("a callback did not count on its caller");
    expect_error("instruction limit", "the callback's message");
    mobius_free_state(s);
}

static void spawned_fibers(void) {
    MobiusState* s = new_state(3);

    /* Each fiber counts for itself: 8 fibers of 905 instructions (spin(180))
     * never reach 1000, which a shared counter would pass. */
    mobius_set_count_hook(s, keep_going, 1000, NULL);
    atomic_store(&hook_calls, 0);
    if (mobius_exec_string(s,
            "func spin(n) { for (var i = 0; i < n; i++) {} return n }\n"
            "var fs = []\n"
            "for (var i = 0; i < 8; i++) { fs:push(spawn spin(180)) }\n"
            "for (var f in fs) { await f }") != MOBIUS_OK)
        fail("fibers under the count");
    if (atomic_load(&hook_calls) != 0) fail("fibers shared a count");

    /* Endless loops in fibers on several workers stop, each with the
     * message; awaiting fails as for any failed fiber. */
    mobius_set_count_hook(s, stop, 2000000, "fiber too long");
    atomic_store(&hook_calls, 0);
    thread_count = 0;
    if (mobius_exec_string(s,
            "func forever() { while (true) {} }\n"
            "var fs = []\n"
            "for (var i = 0; i < 8; i++) { fs:push(spawn forever()) }\n"
            "stopped = 0\n"
            "for (var f in fs) {\n"
            "    try { await f } catch e { if (contains(e, \"instruction limit: fiber too long\")) { stopped = stopped + 1 } }\n"
            "}") != MOBIUS_OK)
        fail("awaiting stopped fibers");
    if (global_int(s, "stopped") != 8) fail("not every fiber was stopped");
    if (atomic_load(&hook_calls) != 8) fail("the hook did not run once per fiber");
    if (thread_count < 2) fail("the fibers did not run on several workers");

    /* A fiber keeps the hook it was spawned with: the host removing it
     * later doesn't free a fiber already running. */
    mobius_set_count_hook(s, stop, 100000, "inherited");
    if (mobius_exec_string(s, "var later = spawn forever()") != MOBIUS_OK) fail("spawn later");
    mobius_set_count_hook(s, NULL, 0, NULL);
    if (mobius_exec_string(s,
            "message = \"\"\n"
            "try { await later } catch e { message = e }") != MOBIUS_OK)
        fail("awaiting the inherited stop");
    mobius_stack_getGlobal(s, "message");
    const char* message = mobius_stack_asString(s, -1);
    if (!message || !strstr(message, "instruction limit: inherited")) fail("the fiber lost its hook");
    mobius_stack_pop(s, 1);
    mobius_free_state(s);
}

static void waits_restart_the_count(void) {
    MobiusState* s = new_state(2);
    mobius_set_count_hook(s, stop, 300, "no wait");
    /* Many rounds, each well under 300 instructions between two waits (an
     * integer for loop with an empty body is one IFORLOOP per round, so the
     * inner loop is under 50); 400 rounds between waits are too many. */
    if (mobius_exec_string(s,
            "import \"fiber\"\n"
            "func waiter() {\n"
            "    for (var i = 0; i < 200; i++) {\n"
            "        for (var j = 0; j < 40; j++) {}\n"
            "        if (i % 2 == 0) { yield } else { fiber.sleep(1) }\n"
            "    }\n"
            "    return 1\n"
            "}\n"
            "func main() {\n"
            "    for (var i = 0; i < 300; i++) { for (var j = 0; j < 40; j++) {}\n yield }\n"
            "    return await spawn waiter()\n"
            "}\n"
            "var r = main()") != MOBIUS_OK)
        fail("a script that waits was stopped");
    if (mobius_exec_string(s,
            "func greedy() { for (var i = 0; i < 1000; i++) { for (var j = 0; j < 400; j++) {}\n yield } }\n"
            "greedy()") != MOBIUS_ERROR_RUNTIME)
        fail("too much between waits was not stopped");
    expect_error("instruction limit: no wait", "the message between waits");
    mobius_free_state(s);
}

int main(void) {
    endless_loops();
    budget_per_call();
    spawned_fibers();
    waits_restart_the_count();
    printf("count hook: PASS\n");
    return 0;
}
