/* A sandboxed state reaches nothing outside unless the host provides it:
 * files, load and imports go to the host's file system or fail with a
 * catchable error, native plugins never load, output is discarded
 * without a handler. */
#define _POSIX_C_SOURCE 200809L
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

/* --- a tiny in-memory file system ------------------------------------- */
#define MAX_FILES 16
static char* names[MAX_FILES];
static char* contents[MAX_FILES];
static int nfiles;

static int find(const char* path) {
    for (int i = 0; i < nfiles; i++) if (strcmp(names[i], path) == 0) return i;
    return -1;
}
static void put(const char* path, const char* data, size_t len, int append) {
    int i = find(path);
    if (i < 0) { i = nfiles++; names[i] = strdup(path); contents[i] = strdup(""); }
    size_t old = append ? strlen(contents[i]) : 0;
    char* buf = malloc(old + len + 1);
    memcpy(buf, contents[i], old);
    memcpy(buf + old, data, len);
    buf[old + len] = 0;
    free(contents[i]);
    contents[i] = buf;
}
static int fs_read(MobiusState* s, const char* path, MobiusFileRequest* req, void* ud) {
    (void)s; (void)ud;
    int i = find(path);
    if (i < 0) { mobius_file_set_error(req, "no such file in the pak"); return 1; }
    mobius_file_set_data(req, contents[i], strlen(contents[i]));
    return MOBIUS_OK;
}
static int fs_write(MobiusState* s, const char* path, const char* data, size_t len, int append,
                    MobiusFileRequest* req, void* ud) {
    (void)s; (void)ud;
    if (strncmp(path, "save/", 5) != 0) { mobius_file_set_error(req, "read-only location"); return 1; }
    put(path, data, len, append);
    return MOBIUS_OK;
}
static int fs_exists(MobiusState* s, const char* path, void* ud) { (void)s; (void)ud; return find(path) >= 0; }

static char out[4096];
static void capture(MobiusState* s, int stream, const char* data, size_t len, void* ud) {
    (void)s; (void)ud;
    if (stream == MOBIUS_STDOUT && strlen(out) + len < sizeof(out)) strncat(out, data, len);
}

static int run(MobiusState* s, const char* code) { return mobius_exec_string(s, code); }

static MobiusState* sandboxed(unsigned allow) {
    MobiusState* s = mobius_new_state(NULL);
    mobius_init_stdlib(s);
    mobius_add_plugin_directory(s, "bin/modules");   /* natives must still not load */
    mobius_sandbox(s, allow);
    return s;
}

int main(void) {
    /* Nothing provided: everything outside is unavailable, and catchable. */
    MobiusState* s = sandboxed(0);
    CHECK(run(s,
        "func blocked(f) {\n"
        "    var msg = \"\"\n"
        "    try { f() } catch e { msg = str(e) }\n"
        "    if (!contains(msg, \"not available\")) { throw \"not blocked: \" + msg }\n"
        "}\n"
        "blocked(func() { return readfile(\"/etc/hostname\") })\n"
        "blocked(func() { return readlines(\"/etc/hostname\") })\n"
        "blocked(func() { return writefile(\"/tmp/mobius_sandbox_x\", \"x\") })\n"
        "blocked(func() { return appendfile(\"/tmp/mobius_sandbox_x\", \"x\") })\n"
        "blocked(func() { return file_exists(\"/etc/hostname\") })\n"
        "blocked(func() { return load(\"/etc/hostname\") })\n") == MOBIUS_OK, "file access blocked");
    CHECK(access("/tmp/mobius_sandbox_x", F_OK) != 0, "nothing written");
    CHECK(run(s, "var ok = false\ntry { import \"json\" } catch e { ok = true }\nif (!ok) { throw \"json imported\" }") == MOBIUS_OK,
          "native plugin not loaded");
    CHECK(run(s, "import \"fiber\" as f\nf.sleep(1)") == MOBIUS_OK, "fiber importable");

    /* Host modules import in the sandbox. */
    mobius_stack_pushNewTable(s, 4);
    mobius_stack_pushInt64(s, 42);
    mobius_stack_setTableField(s, -2, "answer");
    CHECK(mobius_register_module(s, "game") == MOBIUS_OK, "register module");
    CHECK(run(s, "import \"game\"\nif (game.answer != 42) { throw \"game module\" }") == MOBIUS_OK,
          "host module importable");

    /* Without an output handler, output is discarded (stdout to a file). */
    fflush(stdout);
    int saved = dup(1);
    FILE* tmp = tmpfile();
    dup2(fileno(tmp), 1);
    run(s, "print(\"should not appear\")");
    fflush(stdout);
    dup2(saved, 1);
    close(saved);
    CHECK(ftell(tmp) == 0 && lseek(fileno(tmp), 0, SEEK_END) == 0, "output discarded");
    fclose(tmp);

    mobius_set_output_handler(s, capture, NULL);
    run(s, "print(\"to the host\")");
    CHECK(strcmp(out, "to the host\n") == 0, "output to handler");
    mobius_free_state(s);

    /* A host file system: files, load and imports come from it. */
    s = sandboxed(0);
    MobiusFileSystem fs = { fs_read, fs_write, fs_exists };
    mobius_set_file_system(s, &fs, NULL);
    mobius_clear_plugin_directories(s);
    mobius_add_plugin_directory(s, "scripts");
    const char* util = "func twice(x) { return x * 2 }\n";
    put("scripts/util.mob", util, strlen(util), 0);
    const char* cfg = "a\nb\n";
    put("data/config.txt", cfg, strlen(cfg), 0);
    const char* extra = "var loaded_value = 7\n";
    put("scripts/extra.mob", extra, strlen(extra), 0);
    CHECK(run(s,
        "if (readfile(\"data/config.txt\") != \"a\\nb\\n\") { throw \"readfile\" }\n"
        "if (size(readlines(\"data/config.txt\")) != 2) { throw \"readlines\" }\n"
        "if (!file_exists(\"data/config.txt\") || file_exists(\"nope\")) { throw \"file_exists\" }\n"
        "writefile(\"save/slot1\", \"hp=3\")\n"
        "appendfile(\"save/slot1\", \";mp=1\")\n"
        "var denied = \"\"\n"
        "try { writefile(\"data/config.txt\", \"x\") } catch e { denied = str(e) }\n"
        "if (!contains(denied, \"read-only location\")) { throw \"host error text: \" + denied }\n"
        "import \"util\"\n"
        "if (util.twice(4) != 8) { throw \"import from host fs\" }\n"
        "load(\"scripts/extra.mob\")\n"
        "if (loaded_value != 7) { throw \"load from host fs\" }\n"
        "var bad = false\n"
        "try { import \"../secret\" } catch e { bad = true }\n"
        "if (!bad) { throw \"escaping import allowed\" }\n") == MOBIUS_OK, "host file system");
    CHECK(find("save/slot1") >= 0 && strcmp(contents[find("save/slot1")], "hp=3;mp=1") == 0, "host got writes");
    mobius_free_state(s);

    /* allow FILES: the real file system. */
    s = sandboxed(MOBIUS_CAP_FILES);
    CHECK(run(s,
        "writefile(\"/tmp/mobius_sandbox_allowed\", \"ok\")\n"
        "if (readfile(\"/tmp/mobius_sandbox_allowed\") != \"ok\") { throw \"allowed files\" }\n") == MOBIUS_OK,
          "allowed real files");
    unlink("/tmp/mobius_sandbox_allowed");
    mobius_free_state(s);

    printf("sandbox: PASS\n");
    return 0;
}
