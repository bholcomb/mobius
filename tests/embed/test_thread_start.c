/* mobius_set_thread_start_handler runs on each worker thread before it runs
 * any fiber, so a native function called on a worker always finds the
 * handler has run on its thread. (A .NET host attaches each worker there;
 * see the C# binding.) */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static pthread_t main_thread;
static _Thread_local int started_here;
static atomic_int starts;
static atomic_int worker_calls;
static atomic_int unstarted_calls;

static void on_thread_start(MobiusState* state, void* userdata) {
    (void)state;
    if (userdata != &starts) atomic_fetch_add(&unstarted_calls, 1000);
    started_here = 1;
    atomic_fetch_add(&starts, 1);
}

static int probe(MobiusState* state, int arg_count, void* userdata) {
    (void)userdata;
    if (!pthread_equal(pthread_self(), main_thread)) {
        atomic_fetch_add(&worker_calls, 1);
        if (!started_here) atomic_fetch_add(&unstarted_calls, 1);
    }
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNil(state);
    return 1;
}

int main(void) {
    main_thread = pthread_self();
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = 4;
    MobiusState* state = mobius_new_state(&config);
    if (!state) return 1;
    mobius_set_thread_start_handler(state, on_thread_start, &starts);
    mobius_init_stdlib(state);
    mobius_register_function(state, "probe", probe, NULL);

    int rc = mobius_exec_string(state,
        "func work(i) {\n"
        "    for (var k = 0; k < 5; k++) { probe()\n fiber.sleep(1) }\n"
        "    return i\n"
        "}\n"
        "var fs = []\n"
        "for (var i = 0; i < 40; i++) { fs:push(spawn work(i)) }\n"
        "for (var f in fs) { await f }\n");
    if (rc != MOBIUS_OK) { printf("run failed: %d\n", rc); return 1; }
    mobius_free_state(state);

    if (atomic_load(&starts) < 1) { printf("FAIL: the handler never ran\n"); return 1; }
    if (atomic_load(&worker_calls) < 1) { printf("FAIL: no native call ran on a worker\n"); return 1; }
    if (atomic_load(&unstarted_calls) != 0) {
        printf("FAIL: %d native calls on a worker the handler hadn't run on\n", atomic_load(&unstarted_calls));
        return 1;
    }
    printf("thread start handler: PASS (%d starts, %d worker calls)\n", atomic_load(&starts), atomic_load(&worker_calls));
    return 0;
}
