/* Imported modules belong to their state. The module cache used to be
 * process-wide: a state got another state's module tables, and once any
 * state was freed, every other (and every new) state failed to import the
 * modules it had cached ("module 'json' not found"), and freeing the next
 * state crashed. */
#include <mobius/mobius.h>
#include <stdio.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static const char* USE_MODULES =
    "import \"json\"\n"
    "import \"process\"\n"
    "if (json.stringify([1, 2]) != \"[1,2]\") { throw \"json\" }\n"
    "var child = process.start([\"echo\", \"hi\"], {stdout: \"pipe\"})\n"
    "if (child.stdout:read_line() != \"hi\") { throw \"child output\" }\n"
    "if (child:wait() != 0) { throw \"child exit\" }\n"
    "child.stdout:close()\n";

static MobiusState* make_state(void) {
    MobiusState* s = mobius_new_state(NULL);
    if (!s) return NULL;
    mobius_init_stdlib(s);
    mobius_add_plugin_directory(s, "bin/modules");
    return s;
}

int main(void) {
    MobiusState* a = make_state();
    MobiusState* b = make_state();
    CHECK(a && b, "create states");
    CHECK(mobius_exec_string(a, USE_MODULES) == MOBIUS_OK, "modules in a");
    CHECK(mobius_exec_string(b, USE_MODULES) == MOBIUS_OK, "modules in b");
    /* Each state has its own module table. */
    CHECK(mobius_exec_string(a, "json.marker = 1") == MOBIUS_OK, "mark a's json");
    CHECK(mobius_exec_string(b, "if (json.marker != nil) { throw \"b sees a's module table\" }") == MOBIUS_OK,
          "module tables are per state");
    mobius_free_state(a);
    CHECK(mobius_exec_string(b, USE_MODULES) == MOBIUS_OK, "modules in b after a freed");
    MobiusState* c = make_state();
    CHECK(c && mobius_exec_string(c, USE_MODULES) == MOBIUS_OK, "modules in a new state");
    mobius_free_state(b);
    CHECK(mobius_exec_string(c, USE_MODULES) == MOBIUS_OK, "modules in c after b freed");
    mobius_free_state(c);
    printf("state modules: PASS\n");
    return 0;
}
