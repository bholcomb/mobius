/* Time limits pause a script instead of letting it hang the host; the host
 * resumes it later or aborts it. Pauses and aborts reach every fiber. */
#define _POSIX_C_SOURCE 200809L
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static char err_text[2048];
static void on_output(MobiusState* s, int stream, const char* data, size_t len, void* ud) {
    (void)s; (void)ud;
    if (stream == MOBIUS_STDERR && strlen(err_text) + len < sizeof(err_text)) strncat(err_text, data, len);
}
static int errors_reported;
static void on_error(MobiusState* s, const MobiusError* e, void* ud) { (void)s; (void)e; (void)ud; errors_reported++; }

static atomic_long ticks;
static int tick(MobiusState* s, int argc, void* userdata) {
    mobius_stack_pop(s, argc);
    atomic_fetch_add(&ticks, 1);
    return 0;
}

static int run_int(MobiusState* s, const char* expr, long long* out) {
    char code[256];
    snprintf(code, sizeof(code), "__probe = %s", expr);
    if (mobius_exec_string(s, code) != MOBIUS_OK) return 0;
    mobius_stack_getGlobal(s, "__probe");
    *out = mobius_stack_getInt64(s, -1);
    mobius_stack_pop(s, 1);
    return 1;
}

static void* pause_later(void* arg) { sleep_ms(30); mobius_pause((MobiusState*)arg); return NULL; }
static void* abort_later(void* arg) { sleep_ms(30); mobius_abort((MobiusState*)arg); return NULL; }

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_output_handler(s, on_output, NULL);
    mobius_set_error_handler(s, on_error, NULL);
    mobius_register_function(s, "tick", tick, NULL);
    mobius_exec_string(s, "var __probe = 0\nvar progress = 0\n");

    /* A runaway loop pauses at the time limit, with a warning. */
    mobius_set_time_limit(s, 50);
    long t0 = now_ms();
    int rc = mobius_exec_string(s, "while (true) { progress++ }");
    long took = now_ms() - t0;
    CHECK(rc == MOBIUS_PAUSED, "time limit pauses");
    CHECK(took >= 40 && took < 1000, "paused near the limit");
    CHECK(strstr(err_text, "50 ms time limit") != NULL, "warning written");
    CHECK(mobius_is_paused(s) == 1, "is paused");
    CHECK(mobius_exec_string(s, "var x = 1") == MOBIUS_ERROR_BUSY, "busy while paused");
    CHECK(mobius_resume(s) == MOBIUS_PAUSED, "pauses again after resume");

    /* Abort: uncatchable, the state stays usable, globals keep progress. */
    errors_reported = 0;
    CHECK(mobius_abort(s) == MOBIUS_OK, "abort paused execution");
    CHECK(mobius_is_paused(s) == 0, "not paused after abort");
    CHECK(errors_reported == 0, "abort not reported as an error");
    long long progress = 0;
    CHECK(run_int(s, "progress", &progress) && progress > 0, "globals kept after abort");

    /* Work longer than one slice finishes over several resumes. */
    mobius_set_time_limit(s, 20);
    rc = mobius_exec_string(s,
        "var total = 0\n"
        "for (var i = 0; i < 30000000; i++) { total = total + 1 }\n");
    int slices = 1;
    while (rc == MOBIUS_PAUSED && slices < 1000) { rc = mobius_resume(s); slices++; }
    CHECK(rc == MOBIUS_OK, "resumed to completion");
    CHECK(slices > 1, "needed several slices");
    long long total = 0;
    CHECK(run_int(s, "total", &total) && total == 30000000, "result intact across pauses");

    /* try/catch and finally can't stop an abort. */
    mobius_set_time_limit(s, 30);
    mobius_exec_string(s, "var caught = false\nvar cleaned = false\n");
    rc = mobius_exec_string(s,
        "try { while (true) { progress++ } } catch e { caught = true } finally { cleaned = true }\n"
        "caught = true\n");
    CHECK(rc == MOBIUS_PAUSED, "paused inside try");
    mobius_abort(s);
    long long caught = 1, cleaned = 1;
    CHECK(run_int(s, "caught ? 1 : 0", &caught) && caught == 0, "catch did not run");
    CHECK(run_int(s, "cleaned ? 1 : 0", &cleaned) && cleaned == 0, "finally did not run");

    /* Fibers pause with the main script, and abort ends them all, also
     * one sleeping in the reactor. */
    mobius_set_time_limit(s, 50);
    rc = mobius_exec_string(s,
        "func spin() { while (true) { tick() } }\n"
        "func nap() { fiber.sleep(100000) }\n"
        "var spinners = [spawn spin(), spawn spin(), spawn spin(), spawn nap()]\n"
        "for (var f in spinners) { await f }\n");
    CHECK(rc == MOBIUS_PAUSED, "fibers paused");
    long before = atomic_load(&ticks);
    sleep_ms(60);
    CHECK(atomic_load(&ticks) == before, "no fiber runs while paused");
    CHECK(before > 0, "fibers had run");
    t0 = now_ms();
    CHECK(mobius_abort(s) == MOBIUS_OK, "abort with fibers");
    CHECK(now_ms() - t0 < 2000, "abort did not wait for the sleeper");
    before = atomic_load(&ticks);
    sleep_ms(30);
    CHECK(atomic_load(&ticks) == before, "fibers ended by abort");

    /* Pause from another thread, with no time limit. */
    mobius_set_time_limit(s, 0);
    pthread_t th;
    pthread_create(&th, NULL, pause_later, s);
    rc = mobius_exec_string(s, "var n = 0\nwhile (n >= 0) { n++ }");
    pthread_join(th, NULL);
    CHECK(rc == MOBIUS_PAUSED, "pause from another thread");
    mobius_abort(s);

    /* Abort from another thread while running. */
    pthread_create(&th, NULL, abort_later, s);
    rc = mobius_exec_string(s, "var m = 0\nwhile (m >= 0) { m++ }");
    pthread_join(th, NULL);
    CHECK(rc == MOBIUS_ERROR_ABORTED, "abort from another thread");
    CHECK(mobius_exec_string(s, "var after = 1") == MOBIUS_OK, "usable after abort");

    /* A script that finishes in time is unaffected. */
    mobius_set_time_limit(s, 1000);
    CHECK(mobius_exec_string(s, "var quick = 0\nfor (var i = 0; i < 1000; i++) { quick++ }") == MOBIUS_OK,
          "finishes within the limit");

    mobius_free_state(s);
    printf("pause and resume: PASS\n");
    return 0;
}
