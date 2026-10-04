// The Mobius C API (include/mobius/mobius.h and mobius_plugin.h), one to one.
//
// Every exported function is declared here under its C name, so C# code can
// use the API exactly as C does; MobiusState is an optional convenience layer
// on top. Mapping:
//   MobiusState*        IntPtr
//   size_t              nuint
//   bool                bool (marshalled as one byte)
//   const char* input   string (marshalled as UTF-8, NUL-terminated)
//   const char* result  byte* (valid until the next call; decode with Native.Utf8)
//   callbacks           delegate* unmanaged<...> (a static [UnmanagedCallersOnly] method)
//   MobiusValueRef      ulong
// Not mirrored: the plugin-definition structs (MobiusPlugin and friends), as a
// .NET assembly can't be loaded as a Mobius plugin.
//
// The native library is libmobius-core (mobius-core.dll on Windows); it must be
// on the loader's search path (LD_LIBRARY_PATH, or next to the application).

using System;
using System.Runtime.InteropServices;

namespace Mobius;

public enum MobiusValueType
{
    Unknown = -1,
    Nil, Bool, Int64, UInt64, Float64, Char, NativeFunction, String,
    Array, Function, Table, Userdata, Enum, Future, ArraySlice, Channel, SharedCell, Buffer,
}

public enum MobiusOverrideBehavior { Error, Warn, Quiet }

[StructLayout(LayoutKind.Sequential)]
public struct MobiusConfig
{
    public nuint initial_stack_size;
    public nuint max_stack_size;
    public nuint max_call_depth;
    // C bools: byte keeps the struct blittable, so its layout matches C (0 or 1).
    public byte strict_mode;
    public byte warn_on_conversion;
    public byte debug_mode;
    public MobiusOverrideBehavior override_behavior;
    public nuint fiber_stack_size;
    public nuint main_fiber_stack_size;
    public nuint initial_fiber_pool_size;
    public nuint max_fiber_pool_size;
    public int max_worker_threads;
    public nuint string_pool_buckets;
    public nuint global_slot_capacity;
}

[StructLayout(LayoutKind.Sequential)]
public struct MobiusMetrics
{
    public nuint peak_call_depth;
    public nuint peak_registers;
    public nuint peak_upvalues;
    public nuint peak_try_depth;
    public nuint peak_globals;
    public nuint peak_interned_strings;
    public nuint peak_fibers;
    public nuint peak_worker_threads;
    public nuint total_fibers_spawned;
    public nuint total_jobs_executed;
    public nuint peak_fiber_stack_bytes;
    public nuint avg_fiber_stack_bytes;
    public ulong total_execution_time_ns;
}

/// <summary>Passed to the error handler; all pointers are valid only during the call.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MobiusError
{
    public int code;
    public byte* message;
    public byte* suggestion;
    public byte* filename;
    public int line;
    public int column;
    public byte* function_name;
}

/// <summary>Host file system callbacks (mobius_set_file_system). Any member may be null.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MobiusFileSystem
{
    /// <summary>(state, path, request, userdata) -> MOBIUS_OK after mobius_file_set_data.</summary>
    public delegate* unmanaged<IntPtr, byte*, IntPtr, IntPtr, int> read;
    /// <summary>(state, path, data, length, append, request, userdata) -> MOBIUS_OK.</summary>
    public delegate* unmanaged<IntPtr, byte*, byte*, nuint, int, IntPtr, IntPtr, int> write;
    /// <summary>(state, path, userdata) -> 1 if it exists.</summary>
    public delegate* unmanaged<IntPtr, byte*, IntPtr, int> exists;
}

[StructLayout(LayoutKind.Sequential)]
public struct MobiusIoWait
{
    public int fd;
    public int events;
}

public static unsafe class Native
{
    private const string Lib = "mobius-core";

    // ---- Constants -------------------------------------------------------

    public const int MOBIUS_VERSION_MAJOR = 0, MOBIUS_VERSION_MINOR = 1, MOBIUS_VERSION_PATCH = 0;
    public const string MOBIUS_VERSION_STRING = "0.1.0";

    public const int MOBIUS_OK = 0;
    public const int MOBIUS_ERROR_SYNTAX = 1;
    public const int MOBIUS_ERROR_RUNTIME = 2;
    public const int MOBIUS_ERROR_TYPE = 3;
    public const int MOBIUS_ERROR_ARGUMENT = 4;
    public const int MOBIUS_ERROR_MEMORY = 5;
    public const int MOBIUS_ERROR_FILE = 6;
    public const int MOBIUS_ERROR_PLUGIN = 7;
    public const int MOBIUS_ERROR_BUSY = 8;
    public const int MOBIUS_ERROR_ABORTED = 9;
    public const int MOBIUS_PAUSED = 10;

    public const int MOBIUS_STDOUT = 1;
    public const int MOBIUS_STDERR = 2;

    public const uint MOBIUS_CAP_OUTPUT = 0x1;
    public const uint MOBIUS_CAP_FILES = 0x2;

    public const int MOBIUS_IO_READ = 1;
    public const int MOBIUS_IO_WRITE = 2;
    public const int MOBIUS_IO_TIMEOUT = -1;
    public const int MOBIUS_IO_CANCELLED = -2;
    public const int MOBIUS_IO_CLOSED = -3;
    public const int MOBIUS_IO_ERROR = -4;

    public const int MOBIUS_PLUGIN_API_VERSION = 2;

    /// <summary>Decode a NUL-terminated UTF-8 string returned by the API (null stays null).</summary>
    public static string? Utf8(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((IntPtr)p);

    /// <summary>Decode `length` bytes of UTF-8 (strings may contain NUL bytes).</summary>
    public static string Utf8(byte* p, nuint length) =>
        p == null ? "" : System.Text.Encoding.UTF8.GetString(p, checked((int)length));

    // ---- Configuration and metrics --------------------------------------

    [DllImport(Lib)] public static extern MobiusConfig mobius_default_config();
    [DllImport(Lib)] public static extern void mobius_default_config_into(MobiusConfig* @out);
    [DllImport(Lib)] public static extern void mobius_get_metrics(IntPtr state, MobiusMetrics* @out);
    [DllImport(Lib)] public static extern void mobius_reset_metrics(IntPtr state);

    // ---- Errors, output, exit, files -------------------------------------

    /// <summary>Returns the previous handler (null for the default).</summary>
    [DllImport(Lib)] public static extern IntPtr mobius_set_error_handler(IntPtr state, delegate* unmanaged<IntPtr, MobiusError*, IntPtr, void> handler, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_clear_error(IntPtr state);
    [DllImport(Lib)] public static extern void mobius_set_output_handler(IntPtr state, delegate* unmanaged<IntPtr, int, byte*, nuint, IntPtr, void> handler, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_set_exit_handler(IntPtr state, delegate* unmanaged<IntPtr, int, IntPtr, void> handler, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_set_file_system(IntPtr state, MobiusFileSystem* fs, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_file_set_data(IntPtr request, byte* data, nuint length);
    [DllImport(Lib)] public static extern void mobius_file_set_error(IntPtr request, [MarshalAs(UnmanagedType.LPUTF8Str)] string message);

    // ---- Sandbox, time limits, pausing ------------------------------------

    [DllImport(Lib)] public static extern void mobius_sandbox(IntPtr state, uint allow);
    [DllImport(Lib)] public static extern void mobius_set_time_limit(IntPtr state, uint milliseconds);
    [DllImport(Lib)] public static extern void mobius_pause(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_resume(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_abort(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_is_paused(IntPtr state);

    // ---- Lifecycle and execution -----------------------------------------

    [DllImport(Lib)] public static extern IntPtr mobius_new_state(MobiusConfig* config);
    [DllImport(Lib)] public static extern int mobius_init_stdlib(IntPtr state);
    [DllImport(Lib)] public static extern void mobius_free_state(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_exec_string(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string code);
    [DllImport(Lib)] public static extern int mobius_exec_file(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string filename);
    [DllImport(Lib)] public static extern int mobius_exec_string_named(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string code, [MarshalAs(UnmanagedType.LPUTF8Str)] string? name);
    [DllImport(Lib)] public static extern void mobius_add_plugin_directory(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Lib)] public static extern void mobius_clear_plugin_directories(IntPtr state);
    [DllImport(Lib)] public static extern void mobius_start_repl(IntPtr state);

    // ---- Native functions ---------------------------------------------------

    /// <summary>Report an error from a native function; return its (negative) result.</summary>
    [DllImport(Lib)] public static extern int mobius_error(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string message);
    /// <summary>Register `func(state, argc, userdata)` as a read-only global.</summary>
    [DllImport(Lib)] public static extern void mobius_register_function(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, delegate* unmanaged<IntPtr, int, IntPtr, int> func, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_stack_pushFunction(IntPtr state, delegate* unmanaged<IntPtr, int, IntPtr, int> func, IntPtr userdata);
    /// <summary>Pop the table on top of the stack and make it importable as `name`.</summary>
    [DllImport(Lib)] public static extern int mobius_register_module(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    // ---- Stack: inspection -------------------------------------------------

    [DllImport(Lib)] public static extern int mobius_stack_size(IntPtr state);
    [DllImport(Lib)] public static extern MobiusValueType mobius_stack_type(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isNumber(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isInteger(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isFloat(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isString(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isBool(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isNil(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isTable(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isArray(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isFunction(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isUserdata(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_isBuffer(IntPtr state, int idx);

    // ---- Stack: permissive reads (convert where possible) --------------------

    [DllImport(Lib)] public static extern sbyte mobius_stack_asInt8(IntPtr state, int idx);
    [DllImport(Lib)] public static extern byte mobius_stack_asUInt8(IntPtr state, int idx);
    [DllImport(Lib)] public static extern short mobius_stack_asInt16(IntPtr state, int idx);
    [DllImport(Lib)] public static extern ushort mobius_stack_asUInt16(IntPtr state, int idx);
    [DllImport(Lib)] public static extern int mobius_stack_asInt32(IntPtr state, int idx);
    [DllImport(Lib)] public static extern uint mobius_stack_asUInt32(IntPtr state, int idx);
    [DllImport(Lib)] public static extern long mobius_stack_asInt64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern ulong mobius_stack_asUInt64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern float mobius_stack_asFloat32(IntPtr state, int idx);
    [DllImport(Lib)] public static extern double mobius_stack_asFloat64(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_asBool(IntPtr state, int idx);
    [DllImport(Lib)] public static extern byte* mobius_stack_asString(IntPtr state, int idx);
    [DllImport(Lib)] public static extern byte* mobius_stack_getStringData(IntPtr state, int idx, nuint* out_length);

    // ---- Stack: strict reads (the value must have the type) -------------------

    [DllImport(Lib)] public static extern sbyte mobius_stack_getInt8(IntPtr state, int idx);
    [DllImport(Lib)] public static extern byte mobius_stack_getUInt8(IntPtr state, int idx);
    [DllImport(Lib)] public static extern short mobius_stack_getInt16(IntPtr state, int idx);
    [DllImport(Lib)] public static extern ushort mobius_stack_getUInt16(IntPtr state, int idx);
    [DllImport(Lib)] public static extern int mobius_stack_getInt32(IntPtr state, int idx);
    [DllImport(Lib)] public static extern uint mobius_stack_getUInt32(IntPtr state, int idx);
    [DllImport(Lib)] public static extern long mobius_stack_getInt64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern ulong mobius_stack_getUInt64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern float mobius_stack_getFloat32(IntPtr state, int idx);
    [DllImport(Lib)] public static extern double mobius_stack_getFloat64(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_getBool(IntPtr state, int idx);
    [DllImport(Lib)] public static extern byte* mobius_stack_getString(IntPtr state, int idx);

    // ---- Stack: push --------------------------------------------------------

    [DllImport(Lib)] public static extern void mobius_stack_pushNil(IntPtr state);
    [DllImport(Lib)] public static extern void mobius_stack_pushBool(IntPtr state, [MarshalAs(UnmanagedType.U1)] bool value);
    [DllImport(Lib)] public static extern void mobius_stack_pushInt8(IntPtr state, sbyte value);
    [DllImport(Lib)] public static extern void mobius_stack_pushUInt8(IntPtr state, byte value);
    [DllImport(Lib)] public static extern void mobius_stack_pushInt16(IntPtr state, short value);
    [DllImport(Lib)] public static extern void mobius_stack_pushUInt16(IntPtr state, ushort value);
    [DllImport(Lib)] public static extern void mobius_stack_pushInt32(IntPtr state, int value);
    [DllImport(Lib)] public static extern void mobius_stack_pushUInt32(IntPtr state, uint value);
    [DllImport(Lib)] public static extern void mobius_stack_pushInt64(IntPtr state, long value);
    [DllImport(Lib)] public static extern void mobius_stack_pushUInt64(IntPtr state, ulong value);
    [DllImport(Lib)] public static extern void mobius_stack_pushFloat32(IntPtr state, float value);
    [DllImport(Lib)] public static extern void mobius_stack_pushFloat64(IntPtr state, double value);
    [DllImport(Lib)] public static extern void mobius_stack_pushString(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string str);
    [DllImport(Lib)] public static extern void mobius_stack_pushStringLength(IntPtr state, byte* str, nuint length);
    [DllImport(Lib)] public static extern void mobius_stack_pushNewTable(IntPtr state, nuint capacity);
    [DllImport(Lib)] public static extern void mobius_stack_pushNewArray(IntPtr state, nuint capacity);

    // ---- Buffers -------------------------------------------------------------

    [DllImport(Lib)] public static extern void mobius_stack_pushNewBuffer(IntPtr state, nuint size);
    [DllImport(Lib)] public static extern void mobius_stack_pushBufferCopy(IntPtr state, void* data, nuint size);
    /// <summary>A buffer over memory the host owns; `release(ptr, size, userdata)` runs when the buffer dies.</summary>
    [DllImport(Lib)] public static extern void mobius_stack_pushBufferExternal(IntPtr state, void* data, nuint size, delegate* unmanaged<void*, nuint, IntPtr, void> release, IntPtr userdata, [MarshalAs(UnmanagedType.U1)] bool @readonly);
    [DllImport(Lib)] public static extern void* mobius_stack_getBufferData(IntPtr state, int idx, nuint* out_size);
    [DllImport(Lib)] public static extern nuint mobius_stack_getBufferSize(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_bufferIsFixed(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_bufferIsReadonly(IntPtr state, int idx);

    // ---- Userdata ------------------------------------------------------------

    /// <summary>Wrap a host pointer as a script value; `destructor(ptr)` runs when it dies (may be null).</summary>
    [DllImport(Lib)] public static extern void mobius_stack_pushUserdata(IntPtr state, void* ptr, delegate* unmanaged<void*, void> destructor, [MarshalAs(UnmanagedType.LPUTF8Str)] string type_name, nuint size);
    [DllImport(Lib)] public static extern void* mobius_stack_getUserdata(IntPtr state, int idx, byte** out_type_name);

    // ---- Enums ---------------------------------------------------------------

    [DllImport(Lib)] public static extern void mobius_stack_pushNewEnum(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_enumAddMember(IntPtr state, int enum_idx, [MarshalAs(UnmanagedType.LPUTF8Str)] string member_name, long value);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_enumAddAutoMember(IntPtr state, int enum_idx, [MarshalAs(UnmanagedType.LPUTF8Str)] string member_name);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_getEnumMember(IntPtr state, int enum_idx, [MarshalAs(UnmanagedType.LPUTF8Str)] string member_name);

    // ---- References (values kept alive by the host) -----------------------

    [DllImport(Lib)] public static extern ulong mobius_ref_value(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_unref_value(IntPtr state, ulong reference);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_push_ref(IntPtr state, ulong reference);

    // ---- Stack manipulation and globals ------------------------------------

    [DllImport(Lib)] public static extern void mobius_stack_pop(IntPtr state, int count);
    [DllImport(Lib)] public static extern void mobius_stack_copy(IntPtr state, int idx);
    [DllImport(Lib)] public static extern void mobius_stack_getGlobal(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Lib)] public static extern void mobius_stack_setGlobal(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Lib)] public static extern void mobius_set_global_readonly(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.U1)] bool @readonly);
    [DllImport(Lib)] public static extern void mobius_remove_global(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    // ---- Calling script functions ------------------------------------------

    /// <summary>Inside a native function: call the function below `nargs` arguments on the stack.</summary>
    [DllImport(Lib)] public static extern int mobius_pcall(IntPtr state, int nargs, int nresults);
    /// <summary>Call a referenced function with referenced arguments (works on the host thread too); results are pushed.</summary>
    [DllImport(Lib)] public static extern int mobius_call_ref(IntPtr state, ulong function_ref, ulong* arg_refs, nuint nargs, int nresults);

    // ---- Waiting for I/O (Linux) -----------------------------------------------

    [DllImport(Lib)] public static extern int mobius_io_wait(IntPtr state, MobiusIoWait* waits, int count, long timeout_ms);
    [DllImport(Lib)] public static extern void mobius_io_wake_fd(int fd);

    // ---- Tables and arrays (on values already on the stack) ---------------------

    [DllImport(Lib)] public static extern void mobius_stack_setTableField(IntPtr state, int table_idx, [MarshalAs(UnmanagedType.LPUTF8Str)] string key);
    [DllImport(Lib)] public static extern void mobius_stack_getTableField(IntPtr state, int table_idx, [MarshalAs(UnmanagedType.LPUTF8Str)] string key);
    [DllImport(Lib)] public static extern nuint mobius_stack_getTableSize(IntPtr state, int table_idx);
    [DllImport(Lib)] public static extern void mobius_stack_getTableKeys(IntPtr state, int table_idx);
    [DllImport(Lib)] public static extern void mobius_stack_setArrayElement(IntPtr state, int array_idx, nuint element_idx);
    [DllImport(Lib)] public static extern void mobius_stack_getArrayElement(IntPtr state, int array_idx, nuint element_idx);
    [DllImport(Lib)] public static extern nuint mobius_stack_getArrayLength(IntPtr state, int array_idx);
    [DllImport(Lib)] public static extern void mobius_stack_arrayPush(IntPtr state, int array_idx);
    [DllImport(Lib)] public static extern void mobius_stack_arrayPop(IntPtr state, int array_idx);
    [DllImport(Lib)] public static extern void mobius_stack_arrayInsert(IntPtr state, int array_idx, nuint element_idx);
    [DllImport(Lib)] public static extern void mobius_stack_arrayRemove(IntPtr state, int array_idx, nuint element_idx);

    // ---- Metatables ----------------------------------------------------------

    [DllImport(Lib)] public static extern void mobius_push_type_metatable(IntPtr state, MobiusValueType type);
    [DllImport(Lib)] public static extern void mobius_set_type_metatable(IntPtr state, MobiusValueType type);
    [DllImport(Lib)] public static extern void mobius_push_userdata_type_metatable(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string type_name);
    [DllImport(Lib)] public static extern void mobius_set_userdata_type_metatable(IntPtr state, [MarshalAs(UnmanagedType.LPUTF8Str)] string type_name);
}
