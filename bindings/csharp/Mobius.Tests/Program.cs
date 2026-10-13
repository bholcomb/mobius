// Tests for the C# binding against the real native library. Run with
// bindings/csharp/test.sh (which points the loader at bin/).

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using Mobius;

int failures = 0;
void Check(string name, bool ok)
{
    if (!ok) { failures++; Console.WriteLine("FAIL: " + name); }
}
void Throws(string name, Action action, string needle)
{
    try { action(); failures++; Console.WriteLine($"FAIL: {name}: no exception"); }
    catch (MobiusException e) when (e.Message.Contains(needle)) { }
    catch (Exception e) { failures++; Console.WriteLine($"FAIL: {name}: {e.GetType().Name}: {e.Message}"); }
}

// Running code, globals and value conversion.
using (var m = new MobiusState())
{
    m.Execute("var answer = 6 * 7");
    Check("int global", m.GetGlobal("answer") is long n && n == 42);

    m.SetGlobal("config", new Dictionary<string, object?>
    {
        ["name"] = "hero", ["speed"] = 2.5, ["tags"] = new object?[] { "a", "b" }, ["alive"] = true,
    });
    m.Execute("var summary = config.name + \" \" + str(config.speed) + \" \" + config.tags[1]");
    Check("table in", (string?)m.GetGlobal("summary") == "hero 2.5 b");

    m.Execute("var out = {x: 1, list: [1, \"two\", nil], inner: {ok: true}}");
    var table = (Dictionary<string, object?>)m.GetGlobal("out")!;
    var list = (object?[])table["list"]!;
    Check("table out", (long)table["x"]! == 1 && (string?)list[1] == "two" && list[2] == null &&
                       (bool)((Dictionary<string, object?>)table["inner"]!)["ok"]!);

    m.SetGlobal("blob", new byte[] { 1, 2, 3 });
    Check("buffer round trip", m.GetGlobal("blob") is byte[] b && b.SequenceEqual(new byte[] { 1, 2, 3 }));
    m.SetGlobal("text", "héllo, wörld");
    Check("utf-8 round trip", (string?)m.GetGlobal("text") == "héllo, wörld");

    // Errors carry the chunk name and line.
    var e1 = Assert(() => m.Execute("var a = 1\nvar b = nil\nb()", "mods/test.mob"));
    Check("runtime error file/line", e1?.File == "mods/test.mob" && e1.Line == 3);
    var e2 = Assert(() => m.Execute("var c = {", "mods/broken.mob"));
    Check("syntax error", e2 != null && e2.Code == 1 && e2.Message.Contains("Expect"));

    // C# functions, called from the main script and from fibers.
    int calls = 0;
    m.Register("add", args => { Interlocked.Increment(ref calls); return (long)args[0]! + (long)args[1]!; });
    m.Execute("var s = add(2, 3)\nfunc twice(n) { return add(n, n) }\nvar fs = []\nfor (var i = 0; i < 20; i++) { fs:push(spawn twice(i)) }\nvar t = 0\nfor (var f in fs) { t = t + await f }");
    Check("host function", (long)m.GetGlobal("s")! == 5 && (long)m.GetGlobal("t")! == 380 && calls == 21);

    // A C# exception is a script error the script can catch.
    m.Register("fail", args => throw new InvalidOperationException("no ammo"));
    m.Execute("var caught = \"\"\ntry { fail() } catch e { caught = str(e) }");
    Check("exception to script", ((string?)m.GetGlobal("caught"))!.Contains("no ammo"));

    // Calling script functions, and script functions passed to C#.
    m.Execute("func greet(who) { return \"hi \" + who }");
    Check("call", (string?)m.Call("greet", "bob") == "hi bob");
    MobiusFunction? callback = null;
    m.Register("on_event", args => { callback = (MobiusFunction)args[0]!; return null; });
    m.Execute("on_event(func(x) { return x * 10 })");
    Check("callback", callback != null && callback.Call(4L) is long r && r == 40);
    callback?.Dispose();
    Throws("call error", () => m.Call("greet"), "expects 1");

    // A module of C# functions.
    m.RegisterModule("game", new Dictionary<string, object?>
    {
        ["version"] = 3L,
        ["spawn_enemy"] = (Func<object?[], object?>)(args => "enemy:" + args[0]),
    });
    m.Execute("import \"game\"\nvar e = game.spawn_enemy(\"orc\") + \"/\" + str(game.version)");
    Check("module", (string?)m.GetGlobal("e") == "enemy:orc/3");
}

// Several modules of C# functions (importing the first of two crashed).
foreach (int? workers in new int?[] { 0, null })
{
    using var m = new MobiusState(workerThreads: workers);
    m.RegisterModule("one", new Dictionary<string, object?> { ["f"] = (Func<object?[], object?>)(_ => 1L) });
    m.RegisterModule("two", new Dictionary<string, object?> { ["f"] = (Func<object?[], object?>)(_ => 2L) });
    m.Execute("import \"one\"\nimport \"two\"\nvar both = one.f() * 10 + two.f()", "m.mob");
    Check($"two modules (workers {workers})", m.GetGlobal("both") is long both && both == 12);
}

// Output, exit, sandbox and a game file system.
using (var m = new MobiusState())
{
    var output = new StringBuilder();
    var errors = new StringBuilder();
    m.Output = (text, isError) => { lock (output) (isError ? errors : output).Append(text); };
    int exitCode = -1;
    m.ExitRequested = code => exitCode = code;

    var pak = new MemoryFileSystem();
    pak.Files["scripts/util.mob"] = "func twice(x) { return x * 2 }";
    pak.Files["data/level.txt"] = "level 1";
    m.Sandbox();
    m.FileSystem = pak;
    m.AddScriptDirectory("scripts");

    m.Execute("print(\"hello\", 1)\nimport \"util\"\nprint(util.twice(21))\nprint(readfile(\"data/level.txt\"))\nwritefile(\"save/slot\", \"hp=3\")\nexit(7)");
    Check("output", output.ToString() == "hello 1\n42\nlevel 1\n");
    Check("exit", exitCode == 7);
    Check("write", pak.Files.TryGetValue("save/slot", out var saved) && saved == "hp=3");

    m.Execute("var msg = \"\"\ntry { readfile(\"nope\") } catch e { msg = str(e) }");
    Check("file error", ((string?)m.GetGlobal("msg"))!.Contains("no such file"));
    m.Execute("var bad = false\ntry { import \"json\" } catch e { bad = true }");
    Check("no native plugins", (bool)m.GetGlobal("bad")!);
}

// Built-in modules: importable without a sandbox; in one, only once allowed,
// from the script, a module of the game's file system and spawned fibers.
const string usesMath =
    "import \"math\"\nimport \"math\" as m\nimport \"geo\"\n" +
    "func cos_on_fiber() {\n    import \"math\" as fm\n    return fm.cos(0)\n}\n" +
    "var fs = [spawn cos_on_fiber(), spawn geo.diagonal(6, 8)]\n" +
    "var results = [math.sin(0), m.factorial(5), geo.diagonal(3, 4), await fs[0], await fs[1]]";
static bool MathResults(object? results) =>
    results is object?[] r && r.Length == 5 && (double)r[0]! == 0.0 && (long)r[1]! == 120 &&
    (double)r[2]! == 5.0 && (double)r[3]! == 1.0 && (double)r[4]! == 10.0;
MobiusState WithGeo()
{
    var m = new MobiusState();
    var pak = new MemoryFileSystem();
    pak.Files["scripts/geo.mob"] = "import \"math\"\nfunc diagonal(x, y) { return math.hypot(x, y) }";
    m.FileSystem = pak;
    m.AddScriptDirectory("scripts");
    return m;
}
using (var m = WithGeo())
{
    m.Execute(usesMath);
    Check("built-in module without a sandbox", MathResults(m.GetGlobal("results")));
}
using (var m = WithGeo())
{
    m.Sandbox();
    m.Execute("var msg = \"\"\ntry { import \"math\" } catch e { msg = str(e) }");
    Check("built-in module refused in a sandbox", ((string?)m.GetGlobal("msg"))!.Contains("not allowed in this sandbox"));
    Throws("allow an unknown module", () => m.AllowModule("nosuchmodule"), "Not a built-in module");
    Throws("allow a plugin", () => m.AllowModule("json"), "Not a built-in module");
    m.AllowModule("math");
    m.Execute(usesMath);
    Check("built-in module allowed in a sandbox", MathResults(m.GetGlobal("results")));
}

// Many C# calls from many fibers on every worker, with exceptions thrown and
// caught and garbage collections (which walk every thread's stack) in them,
// so trouble with where host code runs shows up here rather than now and then.
using (var m = new MobiusState())
{
    long hits = 0;
    m.Register("hit", args =>
    {
        if (Interlocked.Increment(ref hits) % 500 == 0) GC.Collect();
        return (long)args[0]! + 1;
    });
    m.Register("boom", args => throw new InvalidOperationException("boom"));
    m.Execute("func work(n) {\n    var t = 0\n    for (var i = 0; i < n; i++) {\n        t = t + hit(i)\n        try { boom() } catch e { t = t + 1 }\n    }\n    return t\n}");
    const int rounds = 40, fibers = 64, calls = 20;
    bool totals = true;
    for (int round = 0; round < rounds; round++)
    {
        m.Execute($"var fs = []\nfor (var i = 0; i < {fibers}; i++) {{ fs:push(spawn work({calls})) }}\nvar total = 0\nfor (var f in fs) {{ total = total + await f }}");
        totals &= (long)m.GetGlobal("total")! == fibers * (calls * (calls + 1) / 2 + calls);
    }
    Check("host calls from many fibers", totals && hits == rounds * fibers * calls);
}

// No worker threads: everything on the calling thread.
using (var m = new MobiusState(workerThreads: 0))
{
    int thread = Environment.CurrentManagedThreadId, other = 0;
    m.Register("where", args => { if (Environment.CurrentManagedThreadId != thread) other++; return null; });
    m.Execute("func w() { where() }\nvar fs = [spawn w(), spawn w()]\nfor (var f in fs) { await f }\nwhere()");
    Check("single-threaded", other == 0);

    // A script function C# calls may spawn and wait (it used to wait forever).
    m.Execute("func h() { return 3 }\nfunc f() {\n    var x = spawn h()\n    return await x\n}", "s.mob");
    Check("spawn and await in a call", m.Call("f") is long r && r == 3);

    // An exception from C# reports the script line that called the function.
    m.Register("fail", _ => throw new InvalidOperationException("no"));
    m.Execute("func failing() {\n    var a = 1\n    fail()\n}", "script.mob");
    var e = Assert(() => m.Call("failing"));
    Check("host function error line", e?.File == "script.mob" && e.Line == 3 && e.Message == "script.mob:3: no");

    // An error in a metamethod: where it failed, and the statement that ran it.
    m.Execute("func g() {\n    var t = {}\n    setmetatable(t, { __newindex: func(s, k, v) { throw \"no \" + k } })\n    t.x = 1\n}", "mm.mob");
    e = Assert(() => m.Call("g"));
    Check("metamethod error traceback", e?.File == "mm.mob" && e.Line == 3 && e.Traceback == "called from mm.mob:4 in g" &&
                                        e.Message == "mm.mob:3: no x\n  called from mm.mob:4 in g");
}

// The count hook: endless loops stop, a budget per call, fibers on several
// workers each counted, a throwing hook stops the script cleanly.
using (var m = new MobiusState(workerThreads: 3))
{
    m.SetCountHook(1000, () => "too long");
    Throws("endless loop", () => m.Execute("while (true) {}"), "instruction limit: too long");
    Throws("endless loop inside try", () => m.Execute("try { while (true) {} } catch e {}"), "instruction limit: too long");

    // spin(99) runs 500 instructions (see tests/embed/test_count_hook.c): a budget of 500
    // always lets it finish, and one well under stops it.
    m.Execute("func spin(n) { for (var i = 0; i < n; i++) {} return n }");
    m.SetCountHook(500, () => "over budget");
    bool under = true;
    for (int i = 0; i < 200; i++) under &= m.Call("spin", 99L) is long n && n == 99;
    Check("within the budget", under);
    m.SetCountHook(450, () => "over budget");
    Throws("over the budget", () => m.Call("spin", 99L), "instruction limit: over budget");

    m.Execute("func forever() { while (true) {} }");
    m.SetCountHook(100000, () => "fiber too long");
    m.Execute("var fs = []\nfor (var i = 0; i < 8; i++) { fs:push(spawn forever()) }\nvar stopped = 0\n" +
              "for (var f in fs) { try { await f } catch e { if (contains(e, \"fiber too long\")) { stopped = stopped + 1 } } }");
    Check("fibers stopped", (long)m.GetGlobal("stopped")! == 8);

    long hooked = 0;
    m.SetCountHook(1000, () => { Interlocked.Increment(ref hooked); throw new InvalidOperationException("hook broke"); });
    Throws("throwing hook", () => m.Execute("while (true) {}"), "instruction limit: hook broke");
    Check("throwing hook ran", Interlocked.Read(ref hooked) == 1);

    m.SetCountHook(0, null);
    m.Execute("var n = 0\nfor (var i = 0; i < 1000000; i++) { n = n + 1 }");
    Check("hook removed", (long)m.GetGlobal("n")! == 1000000);
}

// The C API directly, through Native.
RawApi.Run(Check);

Console.WriteLine(failures == 0 ? "C# binding: PASS" : $"C# binding: {failures} failures");
return failures == 0 ? 0 : 1;

static MobiusException? Assert(Action action)
{
    try { action(); return null; }
    catch (MobiusException e) { return e; }
}

class MemoryFileSystem : IMobiusFileSystem
{
    public readonly Dictionary<string, string> Files = new();
    public byte[] Read(string path)
    {
        lock (Files)
            return Files.TryGetValue(path, out var text) ? Encoding.UTF8.GetBytes(text)
                : throw new System.IO.FileNotFoundException("no such file: " + path);
    }
    public void Write(string path, ReadOnlySpan<byte> data, bool append)
    {
        string text = Encoding.UTF8.GetString(data);
        lock (Files) Files[path] = append && Files.TryGetValue(path, out var old) ? old + text : text;
    }
    public bool Exists(string path) { lock (Files) return Files.ContainsKey(path); }
}

static unsafe class RawApi
{
    static int destroyed;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    static int Scale(IntPtr s, int argc, IntPtr userdata)
    {
        long factor = (long)userdata;
        long x = Native.mobius_stack_getInt64(s, 0);
        Native.mobius_stack_pop(s, argc);
        Native.mobius_stack_pushInt64(s, x * factor);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    static void Destroy(void* ptr) => Interlocked.Increment(ref destroyed);

    // A method for "probe" userdata: returns the pointer's value.
    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    static int ProbeValue(IntPtr s, int argc, IntPtr userdata)
    {
        byte* type;
        void* ptr = Native.mobius_stack_getUserdata(s, 0, &type);
        Native.mobius_stack_pop(s, argc);
        Native.mobius_stack_pushInt64(s, (long)ptr);
        return 1;
    }

    public static void Run(Action<string, bool> check)
    {
        // Config layout matches C: known defaults read back through the struct.
        MobiusConfig cfg;
        Native.mobius_default_config_into(&cfg);
        MobiusConfig byValue = Native.mobius_default_config();
        check("config layout", cfg.max_call_depth == 200000 && cfg.global_slot_capacity == 16384 &&
                               cfg.string_pool_buckets == 65536 && cfg.override_behavior == MobiusOverrideBehavior.Error &&
                               byValue.max_call_depth == cfg.max_call_depth && byValue.global_slot_capacity == cfg.global_slot_capacity);

        cfg.max_worker_threads = 1;
        IntPtr s = Native.mobius_new_state(&cfg);
        Native.mobius_init_stdlib(s);

        // A raw native with userdata.
        Native.mobius_register_function(s, "triple", &Scale, (IntPtr)3);
        Native.mobius_stack_pushFunction(s, &Scale, (IntPtr)10);
        Native.mobius_stack_setGlobal(s, "tenfold");
        check("raw exec", Native.mobius_exec_string(s, "var r = triple(5) + tenfold(2)") == Native.MOBIUS_OK);
        Native.mobius_stack_getGlobal(s, "r");
        check("raw native userdata", Native.mobius_stack_type(s, -1) == MobiusValueType.Int64 && Native.mobius_stack_getInt64(s, -1) == 35);
        Native.mobius_stack_pop(s, 1);

        // Userdata values with a destructor, and methods for their type (the
        // type's table holds the methods directly).
        Native.mobius_stack_pushNewTable(s, 4);
        Native.mobius_stack_pushFunction(s, &ProbeValue, IntPtr.Zero);
        Native.mobius_stack_setTableField(s, -2, "value");
        Native.mobius_set_userdata_type_metatable(s, "probe");   // consumes the table
        Native.mobius_stack_pushUserdata(s, (void*)1234, &Destroy, "probe", 0);
        Native.mobius_stack_setGlobal(s, "p");
        check("userdata method", Native.mobius_exec_string(s, "var pv = p:value()") == Native.MOBIUS_OK);
        Native.mobius_stack_getGlobal(s, "pv");
        check("userdata pointer", Native.mobius_stack_getInt64(s, -1) == 1234);
        Native.mobius_stack_pop(s, 1);

        // Enums.
        Native.mobius_stack_pushNewEnum(s, "Color");
        check("enum members", Native.mobius_stack_enumAddMember(s, -1, "red", 1) && Native.mobius_stack_enumAddAutoMember(s, -1, "green"));
        Native.mobius_stack_setGlobal(s, "Color");
        check("enum in script", Native.mobius_exec_string(s, "var c = Color.green") == Native.MOBIUS_OK);
        Native.mobius_stack_getGlobal(s, "Color");
        check("enum value by number", Native.mobius_stack_getEnumValue(s, -1, 1));
        Native.mobius_stack_setGlobal(s, "red");
        Native.mobius_stack_pop(s, 1);
        Native.mobius_exec_string(s, "var is_red = red == Color.red");
        Native.mobius_stack_getGlobal(s, "is_red");
        check("enum value is the member", Native.mobius_stack_asBool(s, -1));
        Native.mobius_stack_pop(s, 1);

        // A buffer over C# memory.
        byte[] bytes = { 7, 8, 9 };
        fixed (byte* b = bytes)
        {
            Native.mobius_stack_pushBufferExternal(s, b, 3, null, IntPtr.Zero, true);
            Native.mobius_stack_setGlobal(s, "ext");
            Native.mobius_exec_string(s, "var e1 = ext[1]");
            Native.mobius_stack_getGlobal(s, "e1");
            check("external buffer", Native.mobius_stack_asInt64(s, -1) == 8);
            Native.mobius_stack_pop(s, 1);
            Native.mobius_remove_global(s, "ext");
        }

        // Refs, strings with their length, metrics.
        Native.mobius_stack_pushString(s, "héllo");
        ulong r = Native.mobius_ref_value(s, -1);
        Native.mobius_stack_pop(s, 1);
        check("push ref", Native.mobius_push_ref(s, r));
        nuint len;
        byte* data = Native.mobius_stack_getStringData(s, -1, &len);
        check("string data", Native.Utf8(data, len) == "héllo" && len == 6);
        Native.mobius_stack_pop(s, 1);
        check("unref", Native.mobius_unref_value(s, r));
        MobiusMetrics m;
        Native.mobius_get_metrics(s, &m);
        check("metrics", m.peak_globals > 0 && m.total_fibers_spawned > 0 && m.total_execution_time_ns > 0);

        Native.mobius_remove_global(s, "p");
        Native.mobius_free_state(s);
        check("userdata destructor ran", destroyed == 1);
    }
}
