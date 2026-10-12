# Mobius for C#

A .NET 10 binding over the Mobius C API (`include/mobius/*.h`). It needs .NET 9 or later: on
Linux and macOS, host functions and callbacks run on Mobius fiber stacks, and .NET 8 crashes
when a managed exception is thrown there, even one the callback catches. (On Windows, Mobius
runs them on the thread's own stack: .NET doesn't support managed code on Win32 fibers.) It
comes in two layers:

- `Native`: the whole C API, one to one, under the C names (`Native.mobius_stack_pushInt64`,
  ...), with its structs (`MobiusConfig`, `MobiusError`, `MobiusFileSystem`, ...),
  `MobiusValueType` and every constant. Use it as you would from C; callbacks are
  `static [UnmanagedCallersOnly]` methods passed as function pointers.
- `MobiusState`: an optional convenience layer (below) built on `Native`.
  `state.Handle` is the `MobiusState*` for mixing the two.

It loads `libmobius-core.so` (`mobius-core.dll` on Windows, `libmobius-core.dylib`
on macOS), which must be on the loader's search path.

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
// And a budget: each call from C# may run 10000 loop iterations and calls.
mobius.SetCountHook(10000, () => "over budget");

// The game's API.
mobius.Register("spawn_enemy", args => world.Spawn((string)args[0]!));
mobius.RegisterModule("game", new Dictionary<string, object?>
{
    ["version"] = 3L,
    ["play_sound"] = (Func<object?[], object?>)(args => { audio.Play((string)args[0]!); return null; }),
});

// Load the mod (runs its top level; throws MobiusException on errors).
mobius.Execute(source, "mods/foo/main.mob");

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

`SetCountHook(count, hook)` is `mobius_set_count_hook` (see the
[Embedding Guide](../../docs/embedding/embedding-guide.md#instruction-budgets)):
the hook runs after every `count` loop iterations and calls a script makes
without waiting, counted per fiber from each call from C#, and returns null to
let the script go on or a message to stop it with an `instruction limit:`
error the script can't catch. A hook that throws stops the script with the
exception's message.

Threading: a registered function runs on whichever thread is running the
script fiber that called it. That can be any of the interpreter's worker
threads (spawned fibers run there in parallel, and the main script may move to
one after it waits), and several calls can run at once. Mobius does not move
calls to a particular thread: functions you register must be safe to call from
any thread, which is the host's responsibility. (With
`new MobiusState(workerThreads: 0)`, everything runs on the thread that calls
`Execute`/`Resume`.) A registered function that calls back into a script may
not wait for another fiber there (`await` and friends raise an error).

Tests: `bin/mobius bindings/csharp/test.mob` (from the project root) builds and
runs `Mobius.Tests` against the core library in `bin/`.
