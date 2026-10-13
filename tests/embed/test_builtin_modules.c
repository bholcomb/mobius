/* Built-in modules (math): every state can import them, a sandboxed one
 * only those the host allowed (mobius_allow_module), wherever the import
 * runs: the main script, a module served by the host's file system, a
 * spawned fiber. Allowing a name that isn't a built-in module fails. */
#include <mobius/mobius.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

static int run(MobiusState* s, const char* code) { return mobius_exec_string(s, code); }

/* Uses math every way a script can import it, on fibers too. */
static const char* uses_math =
    "import \"math\"\n"
    "if (math.sin(0) != 0.0 || math.pi != 3.141592653589793) { throw \"import math\" }\n"
    "import \"math\" as m\n"
    "if (m.factorial(5) != 120 || m.tau != 2 * m.pi) { throw \"import math as m\" }\n"
    "func cos_on_fiber() {\n"
    "    import \"math\" as fm\n"
    "    return fm.cos(0)\n"
    "}\n"
    "func gcd_on_fiber() { return math.gcd(42, 30) }\n"
    "var fs = [spawn cos_on_fiber(), spawn gcd_on_fiber()]\n"
    "if (await fs[0] != 1.0 || await fs[1] != 6) { throw \"math on fibers\" }\n";

/* The import fails with a catchable error that says how to allow it. */
static const char* math_not_allowed =
    "var msg = \"\"\n"
    "try { import \"math\" } catch e { msg = str(e) }\n"
    "if (!contains(msg, \"not allowed in this sandbox\") || !contains(msg, \"mobius_allow_module\")) {\n"
    "    throw \"math not refused: \" + msg\n"
    "}\n";

/* --- a host file system holding one module ------------------------------ */
static const char* geo_path = "scripts/geo.mob";
static const char* geo =
    "import \"math\"\n"
    "func diagonal(x, y) { return math.hypot(x, y) }\n";

static int fs_read(MobiusState* s, const char* path, MobiusFileRequest* req, void* ud) {
    (void)s; (void)ud;
    if (strcmp(path, geo_path) != 0) { mobius_file_set_error(req, "no such file"); return 1; }
    mobius_file_set_data(req, geo, strlen(geo));
    return MOBIUS_OK;
}
static int fs_exists(MobiusState* s, const char* path, void* ud) {
    (void)s; (void)ud;
    return strcmp(path, geo_path) == 0;
}

static MobiusState* new_state(void) {
    MobiusState* s = mobius_new_state(NULL);
    mobius_init_stdlib(s);
    return s;
}

static MobiusState* with_geo(MobiusState* s) {
    MobiusFileSystem fs = { fs_read, NULL, fs_exists };
    mobius_set_file_system(s, &fs, NULL);
    mobius_clear_plugin_directories(s);
    mobius_add_plugin_directory(s, "scripts");
    return s;
}

int main(void) {
    /* Without a sandbox: every built-in module, nothing to allow. */
    MobiusState* s = new_state();
    CHECK(run(s, uses_math) == MOBIUS_OK, "unsandboxed: math");
    mobius_free_state(s);

    /* Sandboxed: not until allowed. Uncaught, the import fails the script. */
    s = new_state();
    mobius_sandbox(s, 0);
    CHECK(run(s, math_not_allowed) == MOBIUS_OK, "sandboxed: math refused");
    CHECK(run(s, "import \"math\" as m\n") != MOBIUS_OK, "sandboxed: uncaught refusal fails");
    CHECK(run(s, "func f() { import \"math\"\n return 1 }\n"
                 "var ok = false\n"
                 "try { await spawn f() } catch e { ok = contains(str(e), \"not allowed\") }\n"
                 "if (!ok) { throw \"fiber imported math\" }\n") == MOBIUS_OK,
          "sandboxed: math refused on a fiber");

    /* Only built-in module names can be allowed: fail loudly otherwise. */
    CHECK(mobius_allow_module(s, "nosuchmodule") == MOBIUS_ERROR_ARGUMENT, "allow unknown fails");
    CHECK(mobius_allow_module(s, "json") == MOBIUS_ERROR_ARGUMENT, "allow plugin fails");
    CHECK(mobius_allow_module(s, "") == MOBIUS_ERROR_ARGUMENT, "allow empty fails");
    CHECK(mobius_allow_module(s, NULL) == MOBIUS_ERROR_ARGUMENT, "allow NULL fails");
    CHECK(mobius_allow_module(NULL, "math") == MOBIUS_ERROR_ARGUMENT, "allow without state fails");
    CHECK(mobius_allow_module(s, "fiber") == MOBIUS_OK, "fiber is always allowed");
    CHECK(run(s, math_not_allowed) == MOBIUS_OK, "failed allows allow nothing");

    CHECK(mobius_allow_module(s, "math") == MOBIUS_OK, "allow math");
    CHECK(run(s, uses_math) == MOBIUS_OK, "sandboxed, allowed: math");
    mobius_free_state(s);

    /* Allowed before the sandbox: stays allowed. */
    s = new_state();
    CHECK(mobius_allow_module(s, "math") == MOBIUS_OK, "allow math before the sandbox");
    mobius_sandbox(s, 0);
    mobius_sandbox(s, MOBIUS_CAP_OUTPUT);   /* changing `allow` keeps it */
    CHECK(run(s, uses_math) == MOBIUS_OK, "allowed before the sandbox: math");
    mobius_free_state(s);

    /* Imported before the sandbox: still refused after it. */
    s = new_state();
    CHECK(run(s, "import \"math\"\n") == MOBIUS_OK, "import before the sandbox");
    mobius_sandbox(s, 0);
    CHECK(run(s, math_not_allowed) == MOBIUS_OK, "imported before the sandbox: refused after");
    mobius_free_state(s);

    /* A module from the host's file system imports it like the script. */
    s = with_geo(new_state());
    mobius_sandbox(s, 0);
    CHECK(run(s, "var msg = \"\"\n"
                 "try { import \"geo\" } catch e { msg = str(e) }\n"
                 "if (!contains(msg, \"not allowed in this sandbox\")) { throw \"geo imported math: \" + msg }\n")
          == MOBIUS_OK, "host file system module: math refused");
    mobius_free_state(s);

    s = with_geo(new_state());
    mobius_sandbox(s, 0);
    CHECK(mobius_allow_module(s, "math") == MOBIUS_OK, "allow math for geo");
    CHECK(run(s, "import \"geo\"\n"
                 "if (geo.diagonal(3, 4) != 5.0) { throw \"geo.diagonal\" }\n"
                 "if (await spawn geo.diagonal(6, 8) != 10.0) { throw \"geo.diagonal on a fiber\" }\n")
          == MOBIUS_OK, "host file system module: math allowed");
    mobius_free_state(s);

    printf("builtin modules: PASS\n");
    return 0;
}
