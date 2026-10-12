/* An error a host function raises (mobius_error) reports where the script
 * called it. It had no file and line 0: native code gives no location, and
 * nothing filled in the calling script's. */
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static char last_file[256];
static char last_message[256];
static int last_line;
static int reports;

static void on_error(MobiusState* s, const MobiusError* e, void* ud) {
    (void)s; (void)ud;
    snprintf(last_file, sizeof(last_file), "%s", e->filename ? e->filename : "");
    snprintf(last_message, sizeof(last_message), "%s", e->message ? e->message : "");
    last_line = e->line;
    reports++;
}

static int fail(MobiusState* s, int argc, void* ud) {
    (void)ud;
    mobius_stack_pop(s, argc);
    return mobius_error(s, "no");
}

static void reset(void) { last_file[0] = '\0'; last_message[0] = '\0'; last_line = 0; reports = 0; }

int main(void) {
    MobiusConfig config = mobius_default_config();
    config.max_worker_threads = 0;
    MobiusState* s = mobius_new_state(&config);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, on_error, NULL);
    mobius_register_function(s, "fail", fail, NULL);

    /* Called from the top level of a script. */
    reset();
    CHECK(mobius_exec_string_named(s, "var a = 1\nfail()\n", "top.mob") != MOBIUS_OK, "top-level call fails");
    CHECK(reports == 1 && strcmp(last_file, "top.mob") == 0 && last_line == 2 && strcmp(last_message, "no") == 0,
          "top-level call reports top.mob:2");

    /* Called from a function the host calls. */
    CHECK(mobius_exec_string_named(s, "func f() {\n    var a = 1\n    fail()\n}\n", "script.mob") == MOBIUS_OK,
          "define functions");
    reset();
    mobius_stack_getGlobal(s, "f");
    MobiusValueRef f = mobius_ref_value(s, -1);
    mobius_stack_pop(s, 1);
    CHECK(mobius_call_ref(s, f, NULL, 0, 1) < 0, "call fails");
    CHECK(reports == 1 && strcmp(last_file, "script.mob") == 0 && last_line == 3, "call reports script.mob:3");
    mobius_unref_value(s, f);

    /* The same for the library's functions. */
    reset();
    CHECK(mobius_exec_string_named(s, "var a = 1\n\nreadfile(\"/no/such/file\")\n", "lib.mob") != MOBIUS_OK,
          "library call fails");
    CHECK(reports == 1 && strcmp(last_file, "lib.mob") == 0 && last_line == 3, "library call reports lib.mob:3");

    /* Inside try the script catches it, unreported. */
    reset();
    CHECK(mobius_exec_string_named(s, "var caught = false\ntry { fail() } catch e { caught = true }\n"
                                      "if (!caught) { throw \"not caught\" }\n", "try.mob") == MOBIUS_OK,
          "caught");
    CHECK(reports == 0, "a caught error is not reported");

    mobius_free_state(s);
    printf("host function errors: PASS\n");
    return 0;
}
