#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include <string>
#include "internal/string_intern.h"
#include "internal/ref_counted.h"
#include "data/number.h"
#include "data/function.h"

#include <cstring>
#include <cstdlib>
#include <mobius/mobius.h>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#  define MOBIUS_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define MOBIUS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define MOBIUS_FORCEINLINE __attribute__((always_inline)) inline
#  define MOBIUS_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#  define MOBIUS_LIKELY(x)   (!!(x))
#  define MOBIUS_UNLIKELY(x) (!!(x))
#  define MOBIUS_FORCEINLINE __forceinline
#  define MOBIUS_NOINLINE __declspec(noinline)
#else
#  define MOBIUS_LIKELY(x)   (!!(x))
#  define MOBIUS_UNLIKELY(x) (!!(x))
#  define MOBIUS_FORCEINLINE inline
#  define MOBIUS_NOINLINE
#endif

class ArrayValue;
class ArraySlice;
class BufferValue;
class Channel;
struct EnumValue;
class EnumDefinition;
class FutureValue;
class SharedCell;
class Table;
class MobiusState;

typedef int (*MobiusCFunction)(MobiusState* ctx, int arg_count);
typedef void (*UserdataDestructor)(void* ptr);

class BufferValue;
struct UserdataObject;
// Maps a buffer to the receiving fiber's copy of it (retained for the
// caller). One spawn copies each buffer once, so views keep sharing.
typedef BufferValue* (*FiberBufferCopy)(BufferValue* buffer, void* ctx);
// Copies a userdata value for another fiber (spawn arguments, captures,
// channel sends). Userdata without one crosses by reference.
typedef Value (*UserdataFiberCopy)(const UserdataObject* src,
                                   FiberBufferCopy copy_buffer, void* ctx);

struct UserdataObject {
    std::atomic<int> ref_count;
    void*        ptr;
    UserdataDestructor destructor;
    MobiusString* type_tag;
    const char*  type_name;
    size_t       size;
    UserdataFiberCopy fiber_copy = nullptr;
};

// Value flags — stored in the int8_t `flags` field of Value.
#define VAL_FLAG_DEFINED   0x01  // slot has been explicitly assigned a value
#define VAL_FLAG_DELETED   0x02  // slot was defined then explicitly removed
#define VAL_FLAG_READONLY  0x04  // value cannot be reassigned (const, builtins)
#define VAL_FLAG_FIXED     0x08  // container has a stable size/address and cannot be resized
#define VAL_FLAG_MARKED    0x10  // reserved: GC mark phase
#define VAL_FLAG_FROZEN    0x20  // reserved: container contents are immutable
#define VAL_FLAG_SHARED    0x40  // container is shared across fibers; mutations are mutex-protected
#define VAL_FLAG_UNUSED    0x80  // container is shared across fibers; mutations are mutex-protected

enum ValueType : int8_t {
    // Internal sentinel — type not yet determined (compiler/VM only, never user-visible)
    VAL_UNKNOWN = -1,
    // Non-refcounted (inline) types — must stay below VAL_STRING
    VAL_NIL,
    VAL_BOOL,
    VAL_INT64,   // signed int64_t  — stored in as.i64
    VAL_UINT64,  // unsigned uint64_t — stored in as.u64
    VAL_FLOAT64,
    VAL_CHAR,
    VAL_NATIVE_FUNCTION,
    // Refcounted (heap-allocated) types — must stay at VAL_STRING or above.
    // Interned strings carry an immortal refcount, so retain/release on them is
    // a load and a branch rather than an atomic; only computed (heap) strings
    // are actually counted.
    VAL_STRING,
    VAL_ARRAY,
    VAL_FUNCTION,
    VAL_TABLE,
    VAL_USERDATA,
    VAL_ENUM,
    VAL_FUTURE,
    VAL_ARRAY_SLICE,
    VAL_CHANNEL,
    VAL_SHARED_CELL,
    VAL_BUFFER
};

// Values of this type and above own a reference to a heap object.
static constexpr ValueType VAL_FIRST_REFCOUNTED = VAL_STRING;

static constexpr int VALUE_TYPE_COUNT = (int)VAL_BUFFER + 1;

class Value {
public:
    union ValueData {
        bool boolean;
        int64_t  i64;    // VAL_INT64
        uint64_t u64;    // VAL_UINT64
        double double_val;

        MobiusString* string;
        char character;

        ArrayValue* array;
        struct MobiusFunction* function;
        MobiusCFunction native_function;
        struct Table* table;
        UserdataObject* userdata;
        EnumDefinition* enum_def;    // VAL_ENUM — member index stored in aux
        FutureValue* future;         // VAL_FUTURE
        ArraySlice* array_slice;     // VAL_ARRAY_SLICE
        Channel* channel;            // VAL_CHANNEL
        SharedCell* shared_cell;     // VAL_SHARED_CELL
        BufferValue* buffer;         // VAL_BUFFER

        ValueData() : i64(0) {}
    } as;
    ValueType type;
    int8_t    flags;
    uint8_t   _pad[2];
    int32_t   aux;       // VAL_ENUM: member index

    Value() : type(VAL_NIL), flags(0), aux(0) { as.i64 = 0; }

    ~Value() { releaseRef(); }

    MOBIUS_FORCEINLINE Value(const Value& other) : type(other.type), flags(other.flags), aux(other.aux) {
        as.i64 = other.as.i64;
        retain();
    }

    MOBIUS_FORCEINLINE Value& operator=(const Value& other) {
        if (this != &other) {
            if (MOBIUS_LIKELY(type < VAL_FIRST_REFCOUNTED && other.type < VAL_FIRST_REFCOUNTED)) {
                type = other.type; flags = other.flags;
                aux = other.aux; as.i64 = other.as.i64;
            } else {
                // Retain new before releasing old to handle self-referencing safely
                other.retain();
                releaseRef();
                type = other.type; flags = other.flags;
                aux = other.aux; as.i64 = other.as.i64;
            }
        }
        return *this;
    }

    Value(Value&& other) noexcept : type(other.type), flags(other.flags), aux(other.aux) {
        as.i64 = other.as.i64;
        other.type = VAL_NIL;
    }

    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            releaseRef();
            type  = other.type;
            flags = other.flags;
            aux   = other.aux;
            as.i64 = other.as.i64;
            other.type = VAL_NIL;
        }
        return *this;
    }

    MOBIUS_FORCEINLINE void rawCopyFrom(const Value& other) {
        type = other.type; flags = other.flags;
        aux = other.aux; as.i64 = other.as.i64;
    }

    MOBIUS_API bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

    MOBIUS_FORCEINLINE bool exactlyEqual(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case VAL_STRING:
                return as.string == other.as.string ||
                       (as.string && other.as.string && *as.string == *other.as.string);
            case VAL_INT64:  return as.i64 == other.as.i64;
            case VAL_NIL:    return true;
            case VAL_BOOL:   return as.boolean == other.as.boolean;
            case VAL_UINT64: return as.u64 == other.as.u64;
            case VAL_FLOAT64: return as.double_val == other.as.double_val;
            case VAL_CHAR:   return as.character == other.as.character;
            case VAL_ARRAY:  return as.array == other.as.array;
            case VAL_FUNCTION: return as.function == other.as.function;
            case VAL_NATIVE_FUNCTION: return as.native_function == other.as.native_function;
            case VAL_TABLE:  return as.table == other.as.table;
            case VAL_USERDATA:
                return as.userdata && other.as.userdata &&
                       as.userdata->ptr == other.as.userdata->ptr &&
                       as.userdata->type_tag == other.as.userdata->type_tag;
            case VAL_ENUM:
                return as.enum_def == other.as.enum_def && aux == other.aux;
            case VAL_FUTURE:
                return as.future == other.as.future;
            case VAL_ARRAY_SLICE:
                return as.array_slice == other.as.array_slice;
            case VAL_CHANNEL:
                return as.channel == other.as.channel;
            case VAL_SHARED_CELL:
                return as.shared_cell == other.as.shared_cell;
            case VAL_BUFFER:
                return as.buffer == other.as.buffer;
            default: return false;
        }
    }

    static Value makeEnum(EnumDefinition* definition, int64_t value);

private:
    MOBIUS_FORCEINLINE void retain() const {
        if (type < VAL_FIRST_REFCOUNTED) return;
        // Strings are the most-copied heap value by a wide margin, so they get
        // their own branch ahead of the switch. Interned strings are immortal:
        // MobiusString::retain() sees the sentinel and does nothing.
        if (MOBIUS_LIKELY(type == VAL_STRING)) {
            if (as.string) as.string->retain();
            return;
        }
        switch (type) {
            case VAL_ARRAY:    if (as.array) ((RefCounted*)as.array)->retain(); break;
            case VAL_FUNCTION:
                if (as.function && !as.function->immortal.load(std::memory_order_relaxed))
                    as.function->ref_count.fetch_add(1, std::memory_order_relaxed);
                break;
            case VAL_TABLE:    if (as.table) ((RefCounted*)as.table)->retain(); break;
            case VAL_USERDATA: if (as.userdata) as.userdata->ref_count.fetch_add(1, std::memory_order_relaxed); break;
            case VAL_ENUM:     if (as.enum_def) ((RefCounted*)as.enum_def)->retain(); break;
            case VAL_FUTURE:   if (as.future) ((RefCounted*)as.future)->retain(); break;
            case VAL_ARRAY_SLICE: if (as.array_slice) ((RefCounted*)as.array_slice)->retain(); break;
            case VAL_CHANNEL: if (as.channel) ((RefCounted*)as.channel)->retain(); break;
            case VAL_SHARED_CELL: if (as.shared_cell) ((RefCounted*)as.shared_cell)->retain(); break;
            case VAL_BUFFER: if (as.buffer) ((RefCounted*)as.buffer)->retain(); break;
            default: break;
        }
    }
    MOBIUS_FORCEINLINE void releaseRef() {
        if (type < VAL_FIRST_REFCOUNTED) return;
        // Inlined for the same reason retain() special-cases strings, and to
        // keep them out of releaseRefSlow(), which is an exported (non-inlined)
        // call.
        if (MOBIUS_LIKELY(type == VAL_STRING)) {
            if (as.string) as.string->release();
            type = VAL_NIL;
            return;
        }
        releaseRefSlow();
    }
    MOBIUS_API void releaseRefSlow();
};

// Value creation functions
inline Value make_nil_value() { return Value(); }

inline Value make_bool_value(bool val) {
    Value value;
    value.type = VAL_BOOL;
    value.as.boolean = val;
    return value;
}

inline Value make_char_value(char val) {
    Value value;
    value.type = VAL_CHAR;
    value.as.character = val;
    return value;
}

inline Value make_int64_value(int64_t val) {
    Value value;
    value.type = VAL_INT64;
    value.as.i64 = val;
    return value;
}

inline Value make_uint64_value(uint64_t val) {
    Value value;
    value.type = VAL_UINT64;
    value.as.u64 = val;
    return value;
}

// Compatibility shim — use make_int64_value / make_uint64_value in new code.
inline Value make_integer_value(NumberType numtype, int64_t val) {
    if (numtype == NUM_UINT64) return make_uint64_value((uint64_t)val);
    return make_int64_value(val);
}

inline Value make_float_value(double val) {
    Value value;
    value.type = VAL_FLOAT64;
    value.as.double_val = val;
    return value;
}

// Retains `string` (a no-op for interned/immortal strings).
MOBIUS_API Value make_string_value(MobiusString* string);
// Takes ownership of one existing reference — for a freshly created heap string,
// which is returned with refcount 1. Do not use with an interned string you did
// not create.
MOBIUS_API Value make_string_value_adopt(MobiusString* string);
MOBIUS_API Value make_string_value_from_cstr(MobiusState* state, const char* cstr);
// A refcounted (not interned) string holding `len` bytes, embedded NULs
// included. For runtime data: the intern pool never frees, so interning
// unboundedly many distinct strings leaks. make_string_value_from_cstr
// interns and is meant for names and other bounded sets.
MOBIUS_API Value make_heap_string_value(const char* data, size_t len);
// Make the tables, arrays and capture-less functions reachable from `v`
// immortal (RefCounted::setImmortal). For a top-level constant's deep-frozen
// value and top-level functions.
MOBIUS_API void make_value_immortal(const Value& v);
// Format a float the way str() and print show it: the shortest digits that
// read back as the same double, fixed notation for exponents -4..15 and
// scientific otherwise, with ".0" on integral values (Python's repr):
// 0.1 + 0.2 -> "0.30000000000000004", 3.0 -> "3.0", 1e300 -> "1e+300".
// Writes at most 32 bytes plus a NUL; returns the length.
MOBIUS_API int format_float(double d, char* buf, size_t size);
MOBIUS_API MobiusString* value_to_interned_string(MobiusState* state, const Value& value);
// str()-facing: heap (reclaimable) result for numeric/char values, pass-through
// for strings, interned for the bounded/rare cases. Returns an owned Value.
MOBIUS_API Value value_to_string_value(MobiusState* state, const Value& value);
MOBIUS_API Value make_function_value(struct MobiusFunction* function);
MOBIUS_API Value make_native_function_value(MobiusCFunction function);
MOBIUS_API Value make_userdata_value(MobiusState* state, void* ptr, UserdataDestructor destructor,
                                     const char* type_name, size_t size);
MOBIUS_API Value make_array_value(ArrayValue* array);
MOBIUS_API Value make_table_value(struct Table* table);
MOBIUS_API Value make_shared_cell_value(SharedCell* shared_cell);
MOBIUS_API Value make_buffer_value(BufferValue* buffer);
MOBIUS_API Value deep_copy_value_for_spawn(const Value& value);

// Whether a value crossing a fiber boundary (spawn arguments and captured
// upvalues, channel send/recv) must be deep-copied: non-shared arrays,
// tables, buffers and closures. Shared values keep their identity; scalars
// and immutable values pass as-is.
MOBIUS_API bool value_needs_fiber_copy(const Value& value);

// The value a `const` binds: a deep copy of v in which every array, table
// and buffer is frozen (immutable). Fails, with *error set, for values that
// cannot be immutable: closures that capture variables, shared values,
// channels, futures, userdata and array spans.
MOBIUS_API bool freeze_for_const(const Value& v, Value* out, const char** error);

// Whether v is an array or table that is part of a const value.
MOBIUS_API bool value_is_frozen(const Value& v);

// Copy several values for one fiber crossing with a single memo, so a
// container reachable from more than one of them stays one container in
// the copy (`spawn f(a, a)` gives the fiber one copy of `a`, not two).
MOBIUS_API void deep_copy_values_for_spawn(std::vector<Value>& values);

// A future's result as handed to one awaiter: a fresh copy of non-shared
// containers. A future can be awaited many times and from many fibers;
// handing out the stored object made every awaiter share it unsynchronized.
inline Value copy_for_awaiter(const Value& result) {
    return value_needs_fiber_copy(result) ? deep_copy_value_for_spawn(result) : result;
}

inline bool is_truthy(const Value& value) {
    switch (value.type) {
        case VAL_NIL: return false;
        case VAL_BOOL: return value.as.boolean;
        case VAL_INT64: return value.as.i64 != 0;
        case VAL_UINT64:  return value.as.u64 != 0;
        case VAL_FLOAT64: return value.as.double_val != 0.0;
        case VAL_STRING: return value.as.string != nullptr && value.as.string->length > 0;
        case VAL_CHAR: return value.as.character != '\0';
        case VAL_ARRAY: return value.as.array != nullptr;
        case VAL_FUNCTION: return value.as.function != nullptr;
        case VAL_NATIVE_FUNCTION: return value.as.native_function != nullptr;
        case VAL_TABLE: return value.as.table != nullptr;
        case VAL_USERDATA: return value.as.userdata != nullptr && value.as.userdata->ptr != nullptr;
        case VAL_ENUM: return true;
        case VAL_FUTURE: return value.as.future != nullptr;
        case VAL_ARRAY_SLICE: return value.as.array_slice != nullptr;
        case VAL_CHANNEL: return value.as.channel != nullptr;
        case VAL_SHARED_CELL: return value.as.shared_cell != nullptr;
        case VAL_BUFFER: return value.as.buffer != nullptr;
        default: return false;
    }
}

// Adopts the caller's reference, like the other make_*_value functions. It
// used to take another one, so every channel (and its buffer) leaked.
inline Value make_channel_value(Channel* ch) {
    Value value;
    value.type = VAL_CHANNEL;
    value.as.channel = ch;
    return value;
}

// Adopts the caller's reference (see make_channel_value). Taking another
// leaked every span, and a leaked span kept its parent array from growing.
inline Value make_array_slice_value(ArraySlice* slice) {
    Value value;
    value.type = VAL_ARRAY_SLICE;
    value.as.array_slice = slice;
    return value;
}

inline Value make_future_value(FutureValue* future) {
    Value value;
    value.type = VAL_FUTURE;
    value.as.future = future;
    if (future) ((RefCounted*)future)->retain();
    return value;
}

MOBIUS_API void print_value(const Value& value);
// Append the text print() shows for a value (without __tostring).
MOBIUS_API void format_value(std::string& out, const Value& value);

// Cycle and depth guard shared by array and table printing. A container
// that contains itself, directly or through other arrays and tables,
// prints as `[...circular...]` / `{...circular...}` instead of recursing
// until the stack overflows. print_container_enter appends the marker and
// returns false when the container must not be printed again.
MOBIUS_API bool print_container_enter(std::string& out, const void* container, bool is_table);
MOBIUS_API void print_container_leave();
MOBIUS_API char* value_to_string(const Value& value);
MOBIUS_API const char* value_type_name(ValueType type);

typedef struct {
    bool success;
    Value converted_value;
    char* error_message;
    bool was_converted;
} TypeConversionResult;

typedef struct {
    bool strict_mode;
    bool warn_on_conversion;
} TypeCheckConfig;

TypeConversionResult validate_and_convert_value(const Value& value, NumberType target_type, bool is_annotated, TypeCheckConfig config);

#endif // MOBIUS_VALUE_H
