/*
 * Mobius Scripting Language — Plugin & Native Function API
 *
 * Include this header when writing Mobius plugins (.so/.dll) or when
 * registering C functions from an embedding application.
 *
 * All interaction with the interpreter happens through the stack:
 *   - Read arguments with mobius_stack_as*() / mobius_stack_get*()
 *   - Pop consumed arguments with mobius_stack_pop()
 *   - Push return values with mobius_stack_push*()
 *   - Return the number of values pushed (>= 0) on success
 *   - Call mobius_error() and return its (negative) result on failure
 */
#ifndef MOBIUS_PLUGIN_H
#define MOBIUS_PLUGIN_H

#include "mobius.h"  /* MobiusState, MobiusConfig, error codes */

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================== */
/*  Native function signature                                              */
/* ====================================================================== */

/**
 * Signature for native C functions callable from Mobius scripts.
 *
 * Arguments are on the stack.  Pop/read them, push return values, and
 * return the number of values pushed (>= 0).
 *
 * On error, call mobius_error() and return its result (always negative).
 */
typedef int (*MobiusCFunction)(MobiusState* state, int arg_count);

/**
 * Report an error from a native function.
 *
 * Invokes the error handler registered with mobius_set_error_handler()
 * (or the default stderr handler).  Returns a negative value that the
 * native function should return immediately.
 *
 * Usage:
 *   return mobius_error(state, "bad argument to foo()");
 */
MOBIUS_API int mobius_error(MobiusState* state, const char* message);

/* ====================================================================== */
/*  Value types (for stack inspection)                                     */
/* ====================================================================== */

typedef enum {
    MOBIUS_VAL_NIL,
    MOBIUS_VAL_BOOL,

    /* Integer types */
    MOBIUS_VAL_INT64,
    MOBIUS_VAL_UINT64,

    /* Float types */
    MOBIUS_VAL_FLOAT64,

    MOBIUS_VAL_STRING,
    MOBIUS_VAL_CHAR,
    MOBIUS_VAL_ARRAY,
    MOBIUS_VAL_FUNCTION,
    MOBIUS_VAL_NATIVE_FUNCTION,
    MOBIUS_VAL_TABLE,
    MOBIUS_VAL_USERDATA,
    MOBIUS_VAL_ENUM
} MobiusValueType;

/* ====================================================================== */
/*  Stack inspection                                                       */
/* ====================================================================== */

MOBIUS_API int mobius_stack_size(MobiusState* state);
MOBIUS_API MobiusValueType mobius_stack_type(MobiusState* state, int idx);

MOBIUS_API bool mobius_stack_isNumber(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isInteger(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isFloat(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isString(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isBool(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isNil(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isTable(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isArray(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isFunction(MobiusState* state, int idx);
MOBIUS_API bool mobius_stack_isUserdata(MobiusState* state, int idx);

/* ====================================================================== */
/*  Stack getters — permissive (auto-convert where possible)               */
/* ====================================================================== */

MOBIUS_API int8_t      mobius_stack_asInt8(MobiusState* state, int idx);
MOBIUS_API uint8_t     mobius_stack_asUInt8(MobiusState* state, int idx);
MOBIUS_API int16_t     mobius_stack_asInt16(MobiusState* state, int idx);
MOBIUS_API uint16_t    mobius_stack_asUInt16(MobiusState* state, int idx);
MOBIUS_API int32_t     mobius_stack_asInt32(MobiusState* state, int idx);
MOBIUS_API uint32_t    mobius_stack_asUInt32(MobiusState* state, int idx);
MOBIUS_API int64_t     mobius_stack_asInt64(MobiusState* state, int idx);
MOBIUS_API uint64_t    mobius_stack_asUInt64(MobiusState* state, int idx);
MOBIUS_API float       mobius_stack_asFloat32(MobiusState* state, int idx);
MOBIUS_API double      mobius_stack_asFloat64(MobiusState* state, int idx);
MOBIUS_API bool        mobius_stack_asBool(MobiusState* state, int idx);
MOBIUS_API const char* mobius_stack_asString(MobiusState* state, int idx);

/* ====================================================================== */
/*  Stack getters — strict (respect strict_types pragma)                   */
/* ====================================================================== */

MOBIUS_API int8_t      mobius_stack_getInt8(MobiusState* state, int idx);
MOBIUS_API uint8_t     mobius_stack_getUInt8(MobiusState* state, int idx);
MOBIUS_API int16_t     mobius_stack_getInt16(MobiusState* state, int idx);
MOBIUS_API uint16_t    mobius_stack_getUInt16(MobiusState* state, int idx);
MOBIUS_API int32_t     mobius_stack_getInt32(MobiusState* state, int idx);
MOBIUS_API uint32_t    mobius_stack_getUInt32(MobiusState* state, int idx);
MOBIUS_API int64_t     mobius_stack_getInt64(MobiusState* state, int idx);
MOBIUS_API uint64_t    mobius_stack_getUInt64(MobiusState* state, int idx);
MOBIUS_API float       mobius_stack_getFloat32(MobiusState* state, int idx);
MOBIUS_API double      mobius_stack_getFloat64(MobiusState* state, int idx);
MOBIUS_API bool        mobius_stack_getBool(MobiusState* state, int idx);
MOBIUS_API const char* mobius_stack_getString(MobiusState* state, int idx);

/* ====================================================================== */
/*  Stack push                                                             */
/* ====================================================================== */

MOBIUS_API void mobius_stack_pushNil(MobiusState* state);
MOBIUS_API void mobius_stack_pushBool(MobiusState* state, bool value);
MOBIUS_API void mobius_stack_pushInt8(MobiusState* state, int8_t value);
MOBIUS_API void mobius_stack_pushUInt8(MobiusState* state, uint8_t value);
MOBIUS_API void mobius_stack_pushInt16(MobiusState* state, int16_t value);
MOBIUS_API void mobius_stack_pushUInt16(MobiusState* state, uint16_t value);
MOBIUS_API void mobius_stack_pushInt32(MobiusState* state, int32_t value);
MOBIUS_API void mobius_stack_pushUInt32(MobiusState* state, uint32_t value);
MOBIUS_API void mobius_stack_pushInt64(MobiusState* state, int64_t value);
MOBIUS_API void mobius_stack_pushUInt64(MobiusState* state, uint64_t value);
MOBIUS_API void mobius_stack_pushFloat32(MobiusState* state, float value);
MOBIUS_API void mobius_stack_pushFloat64(MobiusState* state, double value);
MOBIUS_API void mobius_stack_pushString(MobiusState* state, const char* str);
MOBIUS_API void mobius_stack_pushNewTable(MobiusState* state, size_t capacity);
MOBIUS_API void mobius_stack_pushNewArray(MobiusState* state, size_t capacity);

typedef void (*MobiusUserdataDestructor)(void* ptr);
MOBIUS_API void  mobius_stack_pushUserdata(MobiusState* state, void* ptr,
                                          MobiusUserdataDestructor destructor,
                                          const char* type_name, size_t size);
MOBIUS_API void* mobius_stack_getUserdata(MobiusState* state, int idx,
                                          const char** out_type_name);

/* ====================================================================== */
/*  Stack manipulation                                                     */
/* ====================================================================== */

MOBIUS_API void mobius_stack_pop(MobiusState* state, int count);
MOBIUS_API void mobius_stack_copy(MobiusState* state, int idx);

/* ====================================================================== */
/*  Global variable access                                                 */
/* ====================================================================== */

/**
 * Push the value of a global variable onto the stack.
 * Pushes nil if the variable does not exist.
 */
MOBIUS_API void mobius_stack_getGlobal(MobiusState* state, const char* name);

/**
 * Pop the top value from the stack and assign it to a global variable.
 */
MOBIUS_API void mobius_stack_setGlobal(MobiusState* state, const char* name);

/* ====================================================================== */
/*  Table operations (on values already on the stack)                      */
/* ====================================================================== */

MOBIUS_API void mobius_stack_setTableField(MobiusState* state, int table_idx, const char* key);
MOBIUS_API void mobius_stack_getTableField(MobiusState* state, int table_idx, const char* key);

/* ====================================================================== */
/*  Array operations (on values already on the stack)                      */
/* ====================================================================== */

MOBIUS_API void   mobius_stack_setArrayElement(MobiusState* state, int array_idx, size_t element_idx);
MOBIUS_API void   mobius_stack_getArrayElement(MobiusState* state, int array_idx, size_t element_idx);
MOBIUS_API size_t mobius_stack_getArrayLength(MobiusState* state, int array_idx);

/* ====================================================================== */
/*  Register a native C function as a global                               */
/* ====================================================================== */

/**
 * Register a C function so it can be called from Mobius scripts.
 * Equivalent to assigning a native-function value to a global variable.
 */
MOBIUS_API void mobius_register_function(MobiusState* state, const char* name,
                                        MobiusCFunction func);

/* ====================================================================== */
/*  Plugin registration structs                                            */
/* ====================================================================== */

#define MOBIUS_PLUGIN_API_VERSION 1

#ifndef _WIN32
#define MOBIUS_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define MOBIUS_PLUGIN_EXPORT __declspec(dllexport)
#endif

typedef struct {
    const char* name;
    const char* version;
    const char* description;
    const char* author;
    size_t      api_version;
    const char* license;        /* may be NULL */
} MobiusPluginMetadata;

typedef struct {
    const char*     name;
    MobiusCFunction function;
    size_t          arg_count;    /* SIZE_MAX for variadic */
    const char*     description;  /* short help text, may be NULL */
} MobiusPluginFunction;

typedef struct {
    MobiusPluginMetadata  metadata;
    MobiusPluginFunction* functions;
    size_t                function_count;

    /* Optional lifecycle hooks (may be NULL) */
    int  (*init_plugin)(void);
    void (*cleanup_plugin)(void);
} MobiusPlugin;

/**
 * Every plugin shared library must export a function with this signature:
 *   MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void);
 */
typedef MobiusPlugin* (*MobiusPluginInfoFunc)(void);

#ifdef __cplusplus
}
#endif

#endif /* MOBIUS_PLUGIN_H */
