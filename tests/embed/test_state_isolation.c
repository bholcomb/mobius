/* States are isolated: each has its own GC heap. A collection in one state
 * used to sweep every state's unreachable-from-it objects (freeing another
 * state's live tables), and freeing a state freed every state's objects. */
#include <mobius/mobius.h>
#include <pthread.h>
#include <stdio.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static const char* KEEP =
    "var keep = []\n"
    "for (var i = 0; i < 2000; i++) { keep:push({n: i, name: \"item\" + str(i), arr: [i]}) }\n"
    "func check() {\n"
    "    var s = 0\n"
    "    for (var x in keep) { s = s + x.n + x.arr[0] }\n"
    "    if (s != 3998000 || keep[1999].name != \"item1999\") { throw \"keep damaged\" }\n"
    "}\n";

/* Cyclic garbage forces collections; fibers make it run on workers too. */
static const char* CHURN_DEF =
    "func churn(n) {\n"
    "    for (var i = 0; i < n; i++) { var t = {}\n t.self = t\n var a = [t]\n t.a = a }\n"
    "    return n\n"
    "}\n";
static const char* CHURN =
    "var fs = []\n"
    "for (var k = 0; k < 4; k++) { fs:push(spawn churn(20000)) }\n"
    "for (var f in fs) { await f }\n"
    "churn(50000)\n";

static MobiusState* make_state(void) {
    MobiusState* s = mobius_new_state(NULL);
    if (s) {
        mobius_init_stdlib(s);
        mobius_exec_string(s, CHURN_DEF);
    }
    return s;
}

static void* thread_main(void* arg) {
    (void)arg;
    MobiusState* s = make_state();
    long ok = s && mobius_exec_string(s, KEEP) == MOBIUS_OK;
    for (int round = 0; ok && round < 3; round++) {
        ok = mobius_exec_string(s, CHURN) == MOBIUS_OK &&
             mobius_exec_string(s, "check()") == MOBIUS_OK;
    }
    if (s) mobius_free_state(s);
    return (void*)ok;
}

int main(void) {
    /* Sequential: b's collections and b's teardown leave a alone. */
    MobiusState* a = make_state();
    MobiusState* b = make_state();
    CHECK(a && b, "create states");
    CHECK(mobius_exec_string(a, KEEP) == MOBIUS_OK, "a setup");
    CHECK(mobius_exec_string(b, CHURN) == MOBIUS_OK, "b churn");
    CHECK(mobius_exec_string(a, "check()") == MOBIUS_OK, "a intact after b collected");
    mobius_free_state(b);
    CHECK(mobius_exec_string(a, "check()") == MOBIUS_OK, "a intact after b freed");
    MobiusState* c = make_state();
    CHECK(c && mobius_exec_string(c, CHURN) == MOBIUS_OK, "c churn");
    CHECK(mobius_exec_string(a, "check()") == MOBIUS_OK, "a intact after c collected");
    mobius_free_state(a);
    CHECK(mobius_exec_string(c, "var z = {x: [1, 2]}\nif (z.x[1] != 2) { throw \"c broken\" }") == MOBIUS_OK,
          "c usable after a freed");
    mobius_free_state(c);

    /* Concurrent: one state per host thread, all collecting at once. */
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) pthread_create(&threads[i], NULL, thread_main, NULL);
    int all_ok = 1;
    for (int i = 0; i < 4; i++) {
        void* r = NULL;
        pthread_join(threads[i], &r);
        if (!r) all_ok = 0;
    }
    CHECK(all_ok, "concurrent states");

    printf("state isolation: PASS\n");
    return 0;
}
