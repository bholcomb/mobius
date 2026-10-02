/* The host receives the interpreter's output and decides what exit()
 * means. print() used to write to the process's stdout, and exit() called
 * the C library's exit(), ending the host program. */
#include <mobius/mobius.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static char out_buf[8192], err_buf[8192];
static size_t out_len, err_len;
static int out_calls;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static void on_output(MobiusState* s, int stream, const char* data, size_t len, void* ud) {
    (void)s; (void)ud;
    pthread_mutex_lock(&mu);
    char* buf = stream == MOBIUS_STDERR ? err_buf : out_buf;
    size_t* n = stream == MOBIUS_STDERR ? &err_len : &out_len;
    if (*n + len < sizeof(out_buf) - 1) { memcpy(buf + *n, data, len); *n += len; buf[*n] = 0; }
    if (stream == MOBIUS_STDOUT) out_calls++;
    pthread_mutex_unlock(&mu);
}

static int exit_code_seen = -100;
static void on_exit_called(MobiusState* s, int code, void* ud) {
    (void)s;
    exit_code_seen = code;
    *(int*)ud += 1;
}

static void reset(void) { out_len = err_len = 0; out_buf[0] = err_buf[0] = 0; out_calls = 0; }

int main(void) {
    MobiusState* s = mobius_new_state(NULL);
    CHECK(s, "create state");
    mobius_init_stdlib(s);
    mobius_set_output_handler(s, on_output, NULL);

    reset();
    CHECK(mobius_exec_string(s,
        "var t = setmetatable({}, {__tostring: func(self) { return \"T!\" }})\n"
        "print(\"a\", 1, 2.5, [1, \"x\"], t)\n") == MOBIUS_OK, "print");
    CHECK(strcmp(out_buf, "a 1 2.5 [1, x] T!\n") == 0, "print text");
    CHECK(out_calls == 1, "one call per print");

    /* Lines from many fibers arrive whole. */
    reset();
    CHECK(mobius_exec_string(s,
        "func say(i) { print(\"line\", i, \"end\") }\n"
        "var fs = []\n"
        "for (var i = 0; i < 50; i++) { fs:push(spawn say(i)) }\n"
        "for (var f in fs) { await f }\n") == MOBIUS_OK, "fiber prints");
    CHECK(out_calls == 50, "fifty lines");
    int whole = 1;
    for (char* line = strtok(out_buf, "\n"); line; line = strtok(NULL, "\n"))
        if (strncmp(line, "line ", 5) != 0 || strcmp(line + strlen(line) - 4, " end") != 0) whole = 0;
    CHECK(whole, "lines not interleaved");

    /* Errors from the default error handler go to the stderr stream. */
    reset();
    CHECK(mobius_exec_string(s, "var boom = nil\nboom()\n") != MOBIUS_OK, "runtime error");
    CHECK(strstr(err_buf, "Error") && strstr(err_buf, "non-function"), "error text on stderr stream");

    /* exit() without a handler: a warning, and the script goes on. */
    reset();
    CHECK(mobius_exec_string(s, "exit(4)\nprint(\"after\")\n") == MOBIUS_OK, "exit without handler");
    CHECK(strstr(err_buf, "exit(4) ignored") != NULL, "warning");
    CHECK(strcmp(out_buf, "after\n") == 0, "script continued");

    /* With a handler, the host gets the code. */
    int calls = 0;
    mobius_set_exit_handler(s, on_exit_called, &calls);
    reset();
    CHECK(mobius_exec_string(s, "exit(7)\nexit()\n") == MOBIUS_OK, "exit with handler");
    CHECK(calls == 2 && exit_code_seen == 0, "handler called with codes");
    CHECK(err_len == 0, "no warning with a handler");

    mobius_free_state(s);
    printf("output and exit: PASS\n");
    return 0;
}
