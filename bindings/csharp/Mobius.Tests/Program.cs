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
    Check("execute", m.Execute("var answer = 6 * 7") == RunResult.Completed);
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

// Time limits pause; Resume continues; Abort ends.
using (var m = new MobiusState())
{
    var warnings = new StringBuilder();
    m.Output = (text, isError) => { if (isError) lock (warnings) warnings.Append(text); };
    m.TimeLimitMs = 20;
    var result = m.Execute("var total = 0\nfor (var i = 0; i < 30000000; i++) { total = total + 1 }");
    int slices = 1;
    while (result == RunResult.Paused && slices < 1000) { result = m.Resume(); slices++; }
    Check("resumed to the end", result == RunResult.Completed && slices > 1 && (long)m.GetGlobal("total")! == 30000000);
    Check("pause warning", warnings.ToString().Contains("time limit"));

    Check("runaway pauses", m.Execute("while (true) { }") == RunResult.Paused && m.IsPaused);
    Throws("busy while paused", () => m.Execute("var x = 1"), "paused");
    m.Abort();
    Check("abort", !m.IsPaused && m.Execute("var y = 2") == RunResult.Completed);
}

// No worker threads: everything on the calling thread.
using (var m = new MobiusState(workerThreads: 0))
{
    int thread = Environment.CurrentManagedThreadId, other = 0;
    m.Register("where", args => { if (Environment.CurrentManagedThreadId != thread) other++; return null; });
    m.Execute("func w() { where() }\nvar fs = [spawn w(), spawn w()]\nfor (var f in fs) { await f }\nwhere()");
    Check("single-threaded", other == 0);
}

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
