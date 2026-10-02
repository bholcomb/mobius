// P/Invoke declarations for the Mobius C API (include/mobius/mobius.h and
// mobius_plugin.h). Only what the binding uses. The native library is
// libmobius-core (mobius-core.dll on Windows); it must be on the loader's
// search path (LD_LIBRARY_PATH, or next to the application).

using System;
using System.Runtime.InteropServices;

namespace Mobius;

internal static unsafe class Native
{
    private const string Lib = "mobius-core";

    // Return codes (mobius.h)
    public const int OK = 0;
    public const int ERROR_SYNTAX = 1;
    public const int ERROR_BUSY = 8;
    public const int ERROR_ABORTED = 9;
    public const int PAUSED = 10;

    public const int STDOUT = 1;
    public const int STDERR = 2;

    public const uint CAP_OUTPUT = 0x1;
    public const uint CAP_FILES = 0x2;

    // MobiusValueType (mobius_plugin.h)
    public const int VAL_NIL = 0, VAL_BOOL = 1, VAL_INT64 = 2, VAL_UINT64 = 3, VAL_FLOAT64 = 4,
                     VAL_CHAR = 5, VAL_NATIVE_FUNCTION = 6, VAL_STRING = 7, VAL_ARRAY = 8,
                     VAL_FUNCTION = 9, VAL_TABLE = 10, VAL_BUFFER = 17;

    [StructLayout(LayoutKind.Sequential)]
    public struct Config
    {
        public nuint initial_stack_size;
        public nuint max_stack_size;
        public nuint max_call_depth;
        public byte strict_mode;
        public byte warn_on_conversion;
        public byte debug_mode;
        public int override_behavior;
        public nuint fiber_stack_size;
        public nuint main_fiber_stack_size;
        public nuint initial_fiber_pool_size;
        public nuint max_fiber_pool_size;
        public int max_worker_threads;
        public nuint string_pool_buckets;
        public nuint global_slot_capacity;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Error
    {
        public int code;
        public byte* message;
        public byte* suggestion;
        public byte* filename;
        public int line;
        public int column;
        public byte* function_name;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FileSystem
    {
        public delegate* unmanaged<IntPtr, byte*, IntPtr, IntPtr, int> read;
        public delegate* unmanaged<IntPtr, byte*, byte*, nuint, int, IntPtr, IntPtr, int> write;
        public delegate* unmanaged<IntPtr, byte*, IntPtr, int> exists;
    }

    // Lifecycle and execution
    [DllImport(Lib)] public static extern void mobius_default_config_into(Config* config);
    [DllImport(Lib)] public static extern IntPtr mobius_new_state(Config* config);
    [DllImport(Lib)] public static extern int mobius_init_stdlib(IntPtr state);
    [DllImport(Lib)] public static extern void mobius_free_state(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_exec_string_named(IntPtr state, byte* code, byte* name);
    [DllImport(Lib)] public static extern int mobius_exec_file(IntPtr state, byte* filename);
    [DllImport(Lib)] public static extern void mobius_add_plugin_directory(IntPtr state, byte* path);

    // Handlers
    [DllImport(Lib)] public static extern IntPtr mobius_set_error_handler(IntPtr state, delegate* unmanaged<IntPtr, Error*, IntPtr, void> handler, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_set_output_handler(IntPtr state, delegate* unmanaged<IntPtr, int, byte*, nuint, IntPtr, void> handler, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_set_exit_handler(IntPtr state, delegate* unmanaged<IntPtr, int, IntPtr, void> handler, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_set_file_system(IntPtr state, FileSystem* fs, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_file_set_data(IntPtr request, byte* data, nuint length);
    [DllImport(Lib)] public static extern void mobius_file_set_error(IntPtr request, byte* message);

    // Sandbox and time limits
    [DllImport(Lib)] public static extern void mobius_sandbox(IntPtr state, uint allow);
    [DllImport(Lib)] public static extern void mobius_set_time_limit(IntPtr state, uint milliseconds);
    [DllImport(Lib)] public static extern void mobius_pause(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_resume(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_abort(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_is_paused(IntPtr state);

    // Stack
    [DllImport(Lib)] public static extern int mobius_stack_size(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_stack_type(IntPtr state, int idx);
    [DllImport(Lib)] public static extern void mobius_stack_pop(IntPtr state, int count);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_stack_getBool(IntPtr state, int idx);
    [DllImport(Lib)] public static extern long mobius_stack_getInt64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern ulong mobius_stack_getUInt64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern double mobius_stack_getFloat64(IntPtr state, int idx);
    [DllImport(Lib)] public static extern byte* mobius_stack_getStringData(IntPtr state, int idx, nuint* length);
    [DllImport(Lib)] public static extern byte* mobius_stack_asString(IntPtr state, int idx);
    [DllImport(Lib)] public static extern void* mobius_stack_getBufferData(IntPtr state, int idx, nuint* size);
    [DllImport(Lib)] public static extern void mobius_stack_pushNil(IntPtr state);
    [DllImport(Lib)] public static extern void mobius_stack_pushBool(IntPtr state, [MarshalAs(UnmanagedType.U1)] bool value);
    [DllImport(Lib)] public static extern void mobius_stack_pushInt64(IntPtr state, long value);
    [DllImport(Lib)] public static extern void mobius_stack_pushUInt64(IntPtr state, ulong value);
    [DllImport(Lib)] public static extern void mobius_stack_pushFloat64(IntPtr state, double value);
    [DllImport(Lib)] public static extern void mobius_stack_pushStringLength(IntPtr state, byte* str, nuint length);
    [DllImport(Lib)] public static extern void mobius_stack_pushBufferCopy(IntPtr state, void* data, nuint size);
    [DllImport(Lib)] public static extern void mobius_stack_pushNewTable(IntPtr state, nuint capacity);
    [DllImport(Lib)] public static extern void mobius_stack_pushNewArray(IntPtr state, nuint capacity);
    [DllImport(Lib)] public static extern void mobius_stack_setTableField(IntPtr state, int tableIdx, byte* key);
    [DllImport(Lib)] public static extern void mobius_stack_getTableField(IntPtr state, int tableIdx, byte* key);
    [DllImport(Lib)] public static extern void mobius_stack_getTableKeys(IntPtr state, int tableIdx);
    [DllImport(Lib)] public static extern nuint mobius_stack_getArrayLength(IntPtr state, int arrayIdx);
    [DllImport(Lib)] public static extern void mobius_stack_getArrayElement(IntPtr state, int arrayIdx, nuint elementIdx);
    [DllImport(Lib)] public static extern void mobius_stack_arrayPush(IntPtr state, int arrayIdx);
    [DllImport(Lib)] public static extern void mobius_stack_getGlobal(IntPtr state, byte* name);
    [DllImport(Lib)] public static extern void mobius_stack_setGlobal(IntPtr state, byte* name);

    // References and calls
    [DllImport(Lib)] public static extern ulong mobius_ref_value(IntPtr state, int idx);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_unref_value(IntPtr state, ulong reference);
    [DllImport(Lib)] [return: MarshalAs(UnmanagedType.U1)] public static extern bool mobius_push_ref(IntPtr state, ulong reference);
    [DllImport(Lib)] public static extern int mobius_call_ref(IntPtr state, ulong function, ulong* args, nuint nargs, int nresults);

    // Host functions and modules
    [DllImport(Lib)] public static extern int mobius_error(IntPtr state, byte* message);
    [DllImport(Lib)] public static extern void mobius_register_function(IntPtr state, byte* name, delegate* unmanaged<IntPtr, int, int> func, IntPtr userdata);
    [DllImport(Lib)] public static extern void mobius_stack_pushFunction(IntPtr state, delegate* unmanaged<IntPtr, int, int> func, IntPtr userdata);
    [DllImport(Lib)] public static extern IntPtr mobius_function_userdata(IntPtr state);
    [DllImport(Lib)] public static extern int mobius_register_module(IntPtr state, byte* name);
}

/// <summary>A NUL-terminated UTF-8 copy of a string, for one native call.</summary>
internal unsafe ref struct Utf8
{
    private readonly byte[]? _bytes;
    public Utf8(string? s)
    {
        if (s == null) { _bytes = null; return; }
        int n = System.Text.Encoding.UTF8.GetByteCount(s);
        _bytes = new byte[n + 1];
        System.Text.Encoding.UTF8.GetBytes(s, 0, s.Length, _bytes, 0);
    }
    public int Length => _bytes == null ? 0 : _bytes.Length - 1;
    public ref byte GetPinnableReference() => ref (_bytes == null ? ref System.Runtime.CompilerServices.Unsafe.NullRef<byte>() : ref _bytes[0]);

    public static string? Read(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((IntPtr)p);
    public static string Read(byte* p, nuint length) =>
        p == null ? "" : System.Text.Encoding.UTF8.GetString(p, checked((int)length));
}
