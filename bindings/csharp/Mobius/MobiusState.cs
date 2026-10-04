// A convenience layer over the C API mirror in Native.cs: one interpreter
// with C#-friendly execution, errors, hooks and value conversion. Code that
// wants the C API itself can use Native directly (MobiusState.Handle gives
// the state pointer).

using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Mobius;

/// <summary>A script error: syntax, runtime, or a C# exception raised in a host function.</summary>
public sealed class MobiusException : Exception
{
    public int Code { get; }
    public string? File { get; }
    public int Line { get; }

    public MobiusException(int code, string message, string? file = null, int line = 0)
        : base(file != null && line > 0 ? $"{file}:{line}: {message}" : message)
    {
        Code = code;
        File = file;
        Line = line;
    }
}

/// <summary>
/// Files for scripts: readfile, writefile, file_exists, load, and import of
/// .mob modules all come here once set with <see cref="MobiusState.FileSystem"/>.
/// Throw to fail an operation (the message reaches the script). Called on
/// interpreter worker threads, possibly concurrently.
/// </summary>
public interface IMobiusFileSystem
{
    byte[] Read(string path);
    void Write(string path, ReadOnlySpan<byte> data, bool append);
    bool Exists(string path);
}

/// <summary>A script function held by C#. Valid until disposed or the state is disposed.</summary>
public sealed class MobiusFunction : IDisposable
{
    internal readonly MobiusState Owner;
    internal ulong Ref;

    internal MobiusFunction(MobiusState owner, ulong reference) { Owner = owner; Ref = reference; }

    public object? Call(params object?[] args) => Owner.CallFunction(this, args);

    public void Dispose() => Owner.ReleaseRef(ref Ref);
}

/// <summary>
/// One interpreter. Not thread-safe: use it from one thread at a time
/// (host functions run on interpreter threads and receive their arguments;
/// they must not call into the state except through MobiusFunction.Call).
/// </summary>
public sealed unsafe class MobiusState : IDisposable
{
    private IntPtr _state;
    private GCHandle _self;
    private readonly List<GCHandle> _functions = new();
    private readonly object _errorLock = new();
    private MobiusException? _lastError;

    private Action<string, bool>? _output;
    private Action<int>? _exit;
    private IMobiusFileSystem? _fileSystem;

    /// <param name="workerThreads">Extra worker threads for fibers; null keeps the default, 0 runs everything on the calling thread.</param>
    public MobiusState(int? workerThreads = null)
    {
        MobiusConfig config;
        Native.mobius_default_config_into(&config);
        if (workerThreads.HasValue) config.max_worker_threads = workerThreads.Value;
        _state = Native.mobius_new_state(&config);
        if (_state == IntPtr.Zero) throw new InvalidOperationException("Could not create a Mobius state");
        _self = GCHandle.Alloc(this);
        Native.mobius_init_stdlib(_state);
        Native.mobius_set_error_handler(_state, &OnError, GCHandle.ToIntPtr(_self));
    }

    public void Dispose()
    {
        if (_state == IntPtr.Zero) return;
        Native.mobius_free_state(_state);
        _state = IntPtr.Zero;
        lock (_functions)
        {
            foreach (var h in _functions) h.Free();
            _functions.Clear();
        }
        _self.Free();
    }

    /// <summary>The MobiusState* for calling the C API (Native) directly.</summary>
    public IntPtr Handle => S;

    private IntPtr S => _state != IntPtr.Zero ? _state : throw new ObjectDisposedException(nameof(MobiusState));

    // ------------------------------------------------------------------
    // Running scripts
    // ------------------------------------------------------------------

    /// <summary>Run source code. <paramref name="name"/> labels errors (e.g. the script's path).</summary>
    public void Execute(string code, string? name = null) =>
        Check(Native.mobius_exec_string_named(S, code, name));

    /// <summary>Run a script file (read through <see cref="FileSystem"/> if set).</summary>
    public void ExecuteFile(string path) => Check(Native.mobius_exec_file(S, path));

    private void Check(int rc)
    {
        var error = TakeError();
        if (rc == Native.MOBIUS_OK) return;
        throw error ?? new MobiusException(rc, $"Mobius error {rc}");
    }

    private MobiusException? TakeError()
    {
        lock (_errorLock) { var e = _lastError; _lastError = null; return e; }
    }

    [UnmanagedCallersOnly]
    private static void OnError(IntPtr state, MobiusError* error, IntPtr userdata)
    {
        var self = From(userdata);
        var e = new MobiusException(error->code, Native.Utf8(error->message) ?? "Unknown error",
                                    Native.Utf8(error->filename), error->line);
        lock (self._errorLock) self._lastError = e;
    }

    // ------------------------------------------------------------------
    // Host hooks: output, exit, files, sandbox
    // ------------------------------------------------------------------

    /// <summary>Receives print output (isError false) and error/warning text (isError true). Called on interpreter threads.</summary>
    public Action<string, bool>? Output
    {
        get => _output;
        set
        {
            _output = value;
            if (value != null) Native.mobius_set_output_handler(S, &OnOutput, GCHandle.ToIntPtr(_self));
            else Native.mobius_set_output_handler(S, null, IntPtr.Zero);
        }
    }

    /// <summary>Called when a script calls exit(code). Without it, exit only warns.</summary>
    public Action<int>? ExitRequested
    {
        get => _exit;
        set
        {
            _exit = value;
            if (value != null) Native.mobius_set_exit_handler(S, &OnExit, GCHandle.ToIntPtr(_self));
            else Native.mobius_set_exit_handler(S, null, IntPtr.Zero);
        }
    }

    /// <summary>Serve the script-visible file system from the game.</summary>
    public IMobiusFileSystem? FileSystem
    {
        get => _fileSystem;
        set
        {
            _fileSystem = value;
            if (value == null) { Native.mobius_set_file_system(S, null, IntPtr.Zero); return; }
            var fs = new MobiusFileSystem { read = &OnRead, write = &OnWrite, exists = &OnExists };
            Native.mobius_set_file_system(S, &fs, GCHandle.ToIntPtr(_self));
        }
    }

    /// <summary>
    /// Sandbox the state: no native plugins; output, files and exit only through
    /// the handlers set here, unless the built-in behavior is allowed.
    /// </summary>
    public void Sandbox(bool allowConsoleOutput = false, bool allowRealFiles = false)
    {
        uint allow = (allowConsoleOutput ? Native.MOBIUS_CAP_OUTPUT : 0) | (allowRealFiles ? Native.MOBIUS_CAP_FILES : 0);
        Native.mobius_sandbox(S, allow);
    }

    /// <summary>Where `import "name"` looks for name.mob (through the file system).</summary>
    public void AddScriptDirectory(string path) => Native.mobius_add_plugin_directory(S, path);

    private static MobiusState From(IntPtr userdata) => (MobiusState)GCHandle.FromIntPtr(userdata).Target!;

    [UnmanagedCallersOnly]
    private static void OnOutput(IntPtr state, int stream, byte* data, nuint length, IntPtr userdata)
    {
        try { From(userdata)._output?.Invoke(Native.Utf8(data, length), stream == Native.MOBIUS_STDERR); }
        catch { /* a throwing handler must not take down the interpreter */ }
    }

    [UnmanagedCallersOnly]
    private static void OnExit(IntPtr state, int code, IntPtr userdata)
    {
        try { From(userdata)._exit?.Invoke(code); }
        catch { }
    }

    [UnmanagedCallersOnly]
    private static int OnRead(IntPtr state, byte* path, IntPtr request, IntPtr userdata)
    {
        try
        {
            byte[] data = From(userdata)._fileSystem!.Read(Native.Utf8(path)!);
            fixed (byte* d = data) Native.mobius_file_set_data(request, d, (nuint)data.Length);
            return Native.MOBIUS_OK;
        }
        catch (Exception e) { Native.mobius_file_set_error(request, e.Message); return 1; }
    }

    [UnmanagedCallersOnly]
    private static int OnWrite(IntPtr state, byte* path, byte* data, nuint length, int append, IntPtr request, IntPtr userdata)
    {
        try
        {
            From(userdata)._fileSystem!.Write(Native.Utf8(path)!, new ReadOnlySpan<byte>(data, checked((int)length)), append != 0);
            return Native.MOBIUS_OK;
        }
        catch (Exception e) { Native.mobius_file_set_error(request, e.Message); return 1; }
    }

    [UnmanagedCallersOnly]
    private static int OnExists(IntPtr state, byte* path, IntPtr userdata)
    {
        try { return From(userdata)._fileSystem!.Exists(Native.Utf8(path)!) ? 1 : 0; }
        catch { return 0; }
    }

    // ------------------------------------------------------------------
    // Globals, host functions and modules
    // ------------------------------------------------------------------

    public void SetGlobal(string name, object? value)
    {
        Push(value);
        Native.mobius_stack_setGlobal(S, name);
    }

    public object? GetGlobal(string name)
    {
        Native.mobius_stack_getGlobal(S, name);
        object? value = Read(-1);
        Native.mobius_stack_pop(S, 1);
        return value;
    }

    /// <summary>
    /// Make a C# function callable from scripts as a read-only global. It
    /// receives the arguments converted to C# (see <see cref="Read"/>) and
    /// its result is converted back. An exception becomes a script error.
    /// It may run on any interpreter thread, concurrently.
    /// </summary>
    public void Register(string name, Func<object?[], object?> function) =>
        Native.mobius_register_function(S, name, &Dispatch, Keep(function));

    public void Register(string name, Action<object?[]> action) =>
        Register(name, args => { action(args); return null; });

    /// <summary>
    /// Make a table importable as `import "name"` (also in a sandbox). Values
    /// may be anything Push accepts, including Func&lt;object?[], object?&gt;.
    /// </summary>
    public void RegisterModule(string name, IDictionary<string, object?> members)
    {
        Push(members);
        int rc = Native.mobius_register_module(S, name);
        if (rc != Native.MOBIUS_OK) throw new MobiusException(rc, "Could not register module " + name);
    }

    /// <summary>Call a global script function.</summary>
    public object? Call(string functionName, params object?[] args)
    {
        Native.mobius_stack_getGlobal(S, functionName);
        return CallOnStack(args);
    }

    internal object? CallFunction(MobiusFunction function, object?[] args)
    {
        if (!Native.mobius_push_ref(S, function.Ref)) throw new ObjectDisposedException(nameof(MobiusFunction));
        return CallOnStack(args);
    }

    // Calls the function on top of the stack (popping it) through value refs:
    // mobius_call_ref works on the host thread outside any script, where
    // mobius_pcall has no frame to work in.
    private object? CallOnStack(object?[] args)
    {
        IntPtr s = S;
        ulong fn = Native.mobius_ref_value(s, -1);
        Native.mobius_stack_pop(s, 1);
        var refs = new ulong[args.Length];
        try
        {
            for (int i = 0; i < args.Length; i++)
            {
                Push(args[i]);
                refs[i] = Native.mobius_ref_value(s, -1);
                Native.mobius_stack_pop(s, 1);
            }
            int before = Native.mobius_stack_size(s);
            int rc;
            fixed (ulong* r = refs) rc = Native.mobius_call_ref(s, fn, r, (nuint)args.Length, 1);
            if (rc < 0) throw TakeError() ?? new MobiusException(Native.MOBIUS_ERROR_RUNTIME, "Script function call failed");
            int pushed = Native.mobius_stack_size(s) - before;
            object? result = pushed > 0 ? Read(-pushed) : null;
            if (pushed > 0) Native.mobius_stack_pop(s, pushed);
            TakeError();
            return result;
        }
        finally
        {
            Native.mobius_unref_value(s, fn);
            foreach (ulong r in refs) if (r != 0) Native.mobius_unref_value(s, r);
        }
    }

    internal void ReleaseRef(ref ulong reference)
    {
        if (reference != 0 && _state != IntPtr.Zero) Native.mobius_unref_value(_state, reference);
        reference = 0;
    }

    private IntPtr Keep(Func<object?[], object?> function)
    {
        var h = GCHandle.Alloc(new HostFunction(this, function));
        lock (_functions) _functions.Add(h);   // host functions may push functions on worker threads
        return GCHandle.ToIntPtr(h);
    }

    private sealed class HostFunction
    {
        public readonly MobiusState Owner;
        public readonly Func<object?[], object?> Function;
        public HostFunction(MobiusState owner, Func<object?[], object?> function) { Owner = owner; Function = function; }
    }

    // One entry point for every registered C# function: the userdata (a
    // GCHandle to the C# function) says which.
    [UnmanagedCallersOnly]
    private static int Dispatch(IntPtr state, int argc, IntPtr userdata)
    {
        try
        {
            var fn = (HostFunction)GCHandle.FromIntPtr(userdata).Target!;
            var args = new object?[argc];
            for (int i = 0; i < argc; i++) args[i] = Read(fn.Owner, state, i);
            Native.mobius_stack_pop(state, argc);
            object? result = fn.Function(args);
            Push(fn.Owner, state, result);
            return 1;
        }
        catch (Exception e)
        {
            return Native.mobius_error(state, e.Message);
        }
    }

    // ------------------------------------------------------------------
    // Value conversion
    // ------------------------------------------------------------------
    //   C# -> script: null, bool, integers, float/double, string, byte[]
    //   (buffer), IDictionary (table, string keys), other IEnumerable
    //   (array), Func<object?[], object?> (function), MobiusFunction.
    //   Script -> C#: null, bool, long, ulong, double, string, byte[],
    //   object?[] (array), Dictionary<string, object?> (table, string keys),
    //   MobiusFunction (function). Anything else: its text.

    private void Push(object? value) => Push(this, S, value);
    private object? Read(int idx) => Read(this, S, idx);

    private static void Push(MobiusState self, IntPtr s, object? value)
    {
        switch (value)
        {
            case null: Native.mobius_stack_pushNil(s); break;
            case bool b: Native.mobius_stack_pushBool(s, b); break;
            case sbyte or short or int or long: Native.mobius_stack_pushInt64(s, Convert.ToInt64(value)); break;
            case byte or ushort or uint: Native.mobius_stack_pushInt64(s, Convert.ToInt64(value)); break;
            case ulong ul: Native.mobius_stack_pushUInt64(s, ul); break;
            case float or double: Native.mobius_stack_pushFloat64(s, Convert.ToDouble(value)); break;
            case string str:
            {
                byte[] bytes = System.Text.Encoding.UTF8.GetBytes(str);   // with its length: NULs survive
                fixed (byte* p = bytes) Native.mobius_stack_pushStringLength(s, p, (nuint)bytes.Length);
                break;
            }
            case byte[] data:
                fixed (byte* p = data) Native.mobius_stack_pushBufferCopy(s, p, (nuint)data.Length);
                break;
            case MobiusFunction f:
                if (!Native.mobius_push_ref(s, f.Ref)) throw new ObjectDisposedException(nameof(MobiusFunction));
                break;
            case Func<object?[], object?> fn:
                Native.mobius_stack_pushFunction(s, &Dispatch, self.Keep(fn));
                break;
            case IDictionary dict:
                Native.mobius_stack_pushNewTable(s, (nuint)dict.Count);
                foreach (DictionaryEntry e in dict)
                {
                    Push(self, s, e.Value);
                    Native.mobius_stack_setTableField(s, -2, e.Key.ToString()!);
                }
                break;
            case IEnumerable list:
                Native.mobius_stack_pushNewArray(s, 8);
                foreach (object? item in list)
                {
                    Push(self, s, item);
                    Native.mobius_stack_arrayPush(s, -2);
                }
                break;
            default:
                throw new ArgumentException($"Can't pass a {value.GetType().Name} to Mobius");
        }
    }

    private static object? Read(MobiusState self, IntPtr s, int idx)
    {
        if (idx < 0) idx = Native.mobius_stack_size(s) + idx;   // absolute: we push temporaries
        switch (Native.mobius_stack_type(s, idx))
        {
            case MobiusValueType.Nil: return null;
            case MobiusValueType.Bool: return Native.mobius_stack_getBool(s, idx);
            case MobiusValueType.Int64: return Native.mobius_stack_getInt64(s, idx);
            case MobiusValueType.UInt64: return Native.mobius_stack_getUInt64(s, idx);
            case MobiusValueType.Float64: return Native.mobius_stack_getFloat64(s, idx);
            case MobiusValueType.String:
            {
                nuint len;
                byte* p = Native.mobius_stack_getStringData(s, idx, &len);
                return Native.Utf8(p, len);
            }
            case MobiusValueType.Buffer:
            {
                nuint size;
                byte* p = (byte*)Native.mobius_stack_getBufferData(s, idx, &size);
                return new ReadOnlySpan<byte>(p, checked((int)size)).ToArray();
            }
            case MobiusValueType.Array:
            {
                int n = checked((int)Native.mobius_stack_getArrayLength(s, idx));
                var items = new object?[n];
                for (int i = 0; i < n; i++)
                {
                    Native.mobius_stack_getArrayElement(s, idx, (nuint)i);
                    items[i] = Read(self, s, -1);
                    Native.mobius_stack_pop(s, 1);
                }
                return items;
            }
            case MobiusValueType.Table:
            {
                var table = new Dictionary<string, object?>();
                Native.mobius_stack_getTableKeys(s, idx);
                int keys = Native.mobius_stack_size(s) - 1;
                int n = checked((int)Native.mobius_stack_getArrayLength(s, keys));
                for (int i = 0; i < n; i++)
                {
                    Native.mobius_stack_getArrayElement(s, keys, (nuint)i);
                    if (Read(self, s, -1) is string key)
                    {
                        Native.mobius_stack_getTableField(s, idx, key);
                        table[key] = Read(self, s, -1);
                        Native.mobius_stack_pop(s, 1);
                    }
                    Native.mobius_stack_pop(s, 1);
                }
                Native.mobius_stack_pop(s, 1);
                return table;
            }
            case MobiusValueType.Function:
            case MobiusValueType.NativeFunction:
                return new MobiusFunction(self, Native.mobius_ref_value(s, idx));
            default:
                return Native.Utf8(Native.mobius_stack_asString(s, idx));
        }
    }
}
