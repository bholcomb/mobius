/* mobius_exec_string_named labels errors with the chunk's name, and
 * mobius_default_config_into fills a config through a pointer (for
 * bindings that can't take a struct returned by value). */
#include <mobius/mobius.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static char file[256];
static int line;
static void on_error(MobiusState* s, const MobiusError* e, void* ud) {
    (void)s; (void)ud;
    snprintf(file, sizeof(file), "%s", e->filename ? e->filename : "");
    line = e->line;
}

int main(void) {
    MobiusConfig cfg;
    memset(&cfg, 0xAB, sizeof(cfg));
    mobius_default_config_into(&cfg);
    MobiusConfig expected = mobius_default_config();
    CHECK(memcmp(&cfg, &expected, sizeof(cfg)) == 0 || cfg.max_call_depth == expected.max_call_depth,
          "config filled");
    CHECK(cfg.max_call_depth == expected.max_call_depth && cfg.fiber_stack_size == expected.fiber_stack_size,
          "config values");

    MobiusState* s = mobius_new_state(&cfg);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, on_error, NULL);

    CHECK(mobius_exec_string_named(s, "var a = 1\nvar b = nil\nb()\n", "mods/foo/init.mob") != MOBIUS_OK,
          "runtime error");
    CHECK(strcmp(file, "mods/foo/init.mob") == 0 && line == 3, "runtime error names the chunk");

    CHECK(mobius_exec_string_named(s, "var c = {\n", "mods/foo/broken.mob") == MOBIUS_ERROR_SYNTAX,
          "syntax error");
    CHECK(strcmp(file, "mods/foo/broken.mob") == 0, "syntax error names the chunk");

    /* A function defined in a named chunk keeps its name for later errors. */
    CHECK(mobius_exec_string_named(s, "func later() {\n    var z = nil\n    z()\n}\n", "mods/foo/lib.mob") == MOBIUS_OK,
          "define");
    file[0] = 0;
    CHECK(mobius_exec_string(s, "later()") != MOBIUS_OK, "call later");
    CHECK(strcmp(file, "mods/foo/lib.mob") == 0 && line == 3, "error inside the named chunk's function");

    mobius_free_state(s);
    printf("named chunks: PASS\n");
    return 0;
}
