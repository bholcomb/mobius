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

// No worker threads: everything on the calling thread.
using (var m = new MobiusState(workerThreads: 0))
{
    int thread = Environment.CurrentManagedThreadId, other = 0;
    m.Register("where", args => { if (Environment.CurrentManagedThreadId != thread) other++; return null; });
    m.Execute("func w() { where() }\nvar fs = [spawn w(), spawn w()]\nfor (var f in fs) { await f }\nwhere()");
    Check("single-threaded", other == 0);
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
