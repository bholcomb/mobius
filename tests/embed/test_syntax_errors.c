/* Parse and compile errors reach the error handler with their real
 * message and line. They used to be printed to stderr directly, and the
 * handler only got "Parse error" or "Bytecode compilation failed". */
#include <mobius/mobius.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static char last_message[1024];
static int last_line, last_code, calls;

static void on_error(MobiusState* s, const MobiusError* e, void* ud) {
    (void)s; (void)ud;
    snprintf(last_message, sizeof(last_message), "%s", e->message ? e->message : "");
    last_line = e->line;
    last_code = e->code;
    calls++;
}

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_error_handler(s, on_error, NULL);

    calls = 0;
    int rc = mobius_exec_string(s, "var ok = 1\nvar t = {a: 1, \"b\": 2}\n");
    CHECK(rc == MOBIUS_ERROR_SYNTAX, "syntax error code");
    CHECK(calls == 1, "reported once");
    CHECK(strstr(last_message, "Expect '}' after table literal") != NULL, "parse message");
    CHECK(last_line == 2, "parse line");
    CHECK(last_code == MOBIUS_ERROR_SYNTAX, "error code in handler");

    calls = 0;
    rc = mobius_exec_string(s, "var y = 1\nbreak\n");
    CHECK(rc == MOBIUS_ERROR_SYNTAX, "compile error code");
    CHECK(calls == 1 && strstr(last_message, "'break' outside of loop") != NULL, "compile message");
    CHECK(last_line == 2, "compile line");

    /* The state still works afterwards. */
    CHECK(mobius_exec_string(s, "var z = 3") == MOBIUS_OK, "state usable");

    mobius_free_state(s);
    printf("syntax errors: PASS\n");
    return 0;
}
