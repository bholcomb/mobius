/* An error comes with a traceback of the script calls that led to it, so
 * an error in a metamethod names the statement that ran the metamethod.
 * The error said only where the metamethod failed: the line of its throw,
 * not the store (or index, or operator) that called it. Errors in ordinary
 * nested calls get the same traceback.
 *
 * A host function used as a metamethod keeps its own error: its message
 * was replaced with "Metamethod '...' failed", reported as a second error. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s (got %s:%d: %s | %s)\n", what, \
    last_file, last_line, last_message, last_traceback); return 1; } } while (0)

static char last_file[256];
static char last_message[256];
static char last_traceback[1024];
static int last_line;
static int reports;

static void on_error(MobiusState* s, const MobiusError* e, void* ud) {
    (void)s; (void)ud;
    snprintf(last_file, sizeof(last_file), "%s", e->filename ? e->filename : "");
    snprintf(last_message, sizeof(last_message), "%s", e->message ? e->message : "");
    snprintf(last_traceback, sizeof(last_traceback), "%s", e->traceback ? e->traceback : "(none)");
    last_line = e->line;
    reports++;
}

static int deny(MobiusState* s, int argc, void* ud) {
    (void)ud;
    mobius_stack_pop(s, argc);
    return mobius_error(s, "denied");
}

static void reset(void) {
    last_file[0] = last_message[0] = '\0';
    strcpy(last_traceback, "(not reported)");
    last_line = 0;
    reports = 0;
}

static int run(MobiusState* s, const char* code) {
    reset();
    return mobius_exec_string_named(s, code, "script.mob");
}

int main(void) {
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = 0;
    MobiusState* s = mobius_new_state(&config);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, on_error, NULL);
    mobius_register_function(s, "deny", deny, NULL);

    /* A __newindex that throws: the throw's line, called from the store. */
    CHECK(run(s,
        "func g() {\n"
        "    var t = {}\n"
        "    setmetatable(t, { __newindex: func(s, k, v) { throw \"no \" + k } })\n"
        "    t.x = 1\n"
        "}\n"
        "g()\n") != MOBIUS_OK, "__newindex fails");
    CHECK(reports == 1 && strcmp(last_file, "script.mob") == 0 && last_line == 3 &&
          strcmp(last_message, "no x") == 0 &&
          strcmp(last_traceback, "called from script.mob:4 in g\ncalled from script.mob:6") == 0,
          "__newindex error and traceback");

    /* A runtime error in __index, and in __add. */
    CHECK(run(s,
        "var t = setmetatable({}, { __index: func(s, k) {\n"
        "    var bad = nil\n"
        "    return bad.field\n"
        "} })\n"
        "var y = t.missing\n") != MOBIUS_OK, "__index fails");
    CHECK(reports == 1 && last_line == 3 && strcmp(last_traceback, "called from script.mob:5") == 0,
          "__index error and traceback");
    CHECK(run(s,
        "var v = setmetatable({}, { __add: func(a, b) { throw \"no add\" } })\n"
        "var w = 1\n"
        "var sum = v + w\n") != MOBIUS_OK, "__add fails");
    CHECK(reports == 1 && last_line == 1 && strcmp(last_traceback, "called from script.mob:3") == 0,
          "__add error and traceback");

    /* An ordinary nested call reports the same way. */
    CHECK(run(s,
        "func inner(k) {\n"
        "    throw \"no \" + k\n"
        "}\n"
        "func outer() {\n"
        "    var a = 1\n"
        "    inner(\"y\")\n"
        "}\n"
        "outer()\n") != MOBIUS_OK, "nested call fails");
    CHECK(reports == 1 && last_line == 2 &&
          strcmp(last_traceback, "called from script.mob:6 in outer\ncalled from script.mob:8") == 0,
          "nested call error and traceback");

    /* No callers: no traceback. */
    CHECK(run(s, "var a = 1\nthrow \"top\"\n") != MOBIUS_OK, "top-level throw fails");
    CHECK(reports == 1 && last_line == 2 && strcmp(last_traceback, "(none)") == 0, "no traceback at the top");

    /* A host function as a metamethod: its own error, reported once, at the store. */
    CHECK(run(s,
        "var guarded = setmetatable({}, { __newindex: deny })\n"
        "func set() {\n"
        "    guarded.z = 1\n"
        "}\n"
        "set()\n") != MOBIUS_OK, "native __newindex fails");
    CHECK(reports == 1 && strcmp(last_message, "denied") == 0 && last_line == 3 &&
          strcmp(last_traceback, "called from script.mob:5") == 0, "native __newindex error");
    CHECK(run(s,
        "var guarded = setmetatable({}, { __index: deny })\n"
        "var v = guarded.z\n") != MOBIUS_OK, "native __index fails");
    CHECK(reports == 1 && strcmp(last_message, "denied") == 0 && last_line == 2, "native __index error");

    /* Caught errors aren't reported, and catch sees the message alone. */
    CHECK(run(s,
        "func thrower() { throw \"inner\" }\n"
        "func catcher() {\n"
        "    try { thrower() } catch e { return e }\n"
        "}\n"
        "if (catcher() != \"inner\") { throw \"wrong catch\" }\n") == MOBIUS_OK, "caught");
    CHECK(reports == 0, "caught error not reported");

    mobius_free_state(s);
    printf("error traceback: PASS\n");
    return 0;
}
