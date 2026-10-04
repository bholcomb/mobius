# Mobius for C#

A .NET 8 binding over the Mobius C API (`include/mobius/*.h`), in two layers:

- `Native`: the whole C API, one to one, under the C names (`Native.mobius_stack_pushInt64`,
  ...), with its structs (`MobiusConfig`, `MobiusError`, `MobiusFileSystem`, ...),
  `MobiusValueType` and every constant. Use it as you would from C; callbacks are
  `static [UnmanagedCallersOnly]` methods passed as function pointers.
- `MobiusState`: an optional convenience layer (below) built on `Native`.
  `state.Handle` is the `MobiusState*` for mixing the two.

It loads `libmobius-core.so` (`mobius-core.dll` on Windows, once supported), which
must be on the loader's search path.

Using the C API directly:

```csharp
[UnmanagedCallersOnly]
static int SpawnEnemy(IntPtr state, int argc, IntPtr userdata)
{
    var world = (World)GCHandle.FromIntPtr(userdata).Target!;
    long kind = Native.mobius_stack_getInt64(state, 0);
    Native.mobius_stack_pop(state, argc);
    Native.mobius_stack_pushInt64(state, world.Spawn(kind));
    return 1;
}

IntPtr s = Native.mobius_new_state(null);
Native.mobius_init_stdlib(s);
Native.mobius_register_function(s, "spawn_enemy", &SpawnEnemy, GCHandle.ToIntPtr(worldHandle));
Native.mobius_exec_string(s, "spawn_enemy(3)");
Native.mobius_free_state(s);
```

The convenience layer:

```csharp
using Mobius;

using var mobius = new MobiusState();

// Where script output and exit() go.
mobius.Output = (text, isError) => Log(text, isError);
mobius.ExitRequested = code => UnloadMod(code);

// For untrusted scripts: no native plugins, files only through the game.
mobius.Sandbox();
mobius.FileSystem = new ModFiles(modDirectory);   // implements IMobiusFileSystem
mobius.AddScriptDirectory("scripts");             // import "util" -> scripts/util.mob

// The game's API.
mobius.Register("spawn_enemy", args => world.Spawn((string)args[0]!));
mobius.RegisterModule("game", new Dictionary<string, object?>
{
    ["version"] = 3L,
    ["play_sound"] = (Func<object?[], object?>)(args => { audio.Play((string)args[0]!); return null; }),
});

// A runaway script pauses after 5 ms instead of hanging the frame.
mobius.TimeLimitMs = 5;
var result = mobius.Execute(source, "mods/foo/main.mob");
// ... next frame:
if (result == RunResult.Paused) result = mobius.Resume();

// Calling into scripts.
mobius.Call("on_update", deltaTime);
```

Values convert both ways:

| C# | Mobius |
|---|---|
| `null` | `nil` |
| `bool` | bool |
| integers | int64 (`ulong` as uint64); read back as `long` |
| `float`, `double` | float64; read back as `double` |
| `string` | string (UTF-8) |
| `byte[]` | buffer |
| `IDictionary` | table (string keys); read back as `Dictionary<string, object?>` |
| other `IEnumerable` | array; read back as `object?[]` |
| `Func<object?[], object?>` | function |
| `MobiusFunction` | a script function held by C#, callable with `.Call(...)` |

Errors (syntax, runtime, or a C# exception thrown by a registered function)
come back as `MobiusException` with `File` and `Line`. A C# exception thrown in
a registered function becomes a script error the script can catch.

Registered functions run on the interpreter's worker threads, possibly
concurrently; with `new MobiusState(workerThreads: 0)` everything runs on the
calling thread. A registered function that calls back into a script may not
wait for another fiber there (`await` and friends raise an error).

Tests: `./test.sh` builds and runs `Mobius.Tests` against `../../bin`.
