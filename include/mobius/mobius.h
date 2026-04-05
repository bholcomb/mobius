/*
 * Mobius Scripting Language — Public Embedding API
 *
 * This is the only header needed to embed the Mobius interpreter in a C or
 * C++ application.  MobiusState is an opaque handle; all interaction goes
 * through the functions declared here.
 *
 * For writing native functions or plugins, also include <mobius/mobius_plugin.h>.
 */
#ifndef MOBIUS_H
#define MOBIUS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ====================================================================== */
/*  Shared-library export macro                                            */
/* ====================================================================== */

#ifndef MOBIUS_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef MOBIUS_BUILDING
#      define MOBIUS_API __declspec(dllexport)
#    else
#      define MOBIUS_API __declspec(dllimport)
#    endif
#  elif __GNUC__ >= 4
#    define MOBIUS_API __attribute__((visibility("default")))
#  else
#    define MOBIUS_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================== */
/*  Version                                                                */
/* ====================================================================== */

#define MOBIUS_VERSION_MAJOR  1
#define MOBIUS_VERSION_MINOR  0
#define MOBIUS_VERSION_PATCH  0
#define MOBIUS_VERSION_STRING "1.0.0"

/* ====================================================================== */
/*  Opaque state handle                                                    */
/* ====================================================================== */

typedef struct MobiusState MobiusState;

/* ====================================================================== */
/*  Error codes                                                            */
/* ====================================================================== */

#define MOBIUS_OK               0
#define MOBIUS_ERROR_SYNTAX     1
#define MOBIUS_ERROR_RUNTIME    2
#define MOBIUS_ERROR_TYPE       3
#define MOBIUS_ERROR_ARGUMENT   4
#define MOBIUS_ERROR_MEMORY     5
#define MOBIUS_ERROR_FILE       6
#define MOBIUS_ERROR_PLUGIN     7

/* ====================================================================== */
/*  Configuration                                                          */
/* ====================================================================== */

typedef enum {
    MOBIUS_OVERRIDE_ERROR,   /* Error on function name conflict (default) */
    MOBIUS_OVERRIDE_WARN,    /* Warn but allow */
    MOBIUS_OVERRIDE_QUIET    /* Silent override */
} MobiusOverrideBehavior;

typedef struct {
    size_t initial_stack_size;
    size_t max_stack_size;
    size_t max_call_depth;
    bool   strict_mode;
    bool   warn_on_conversion;
    bool   debug_mode;
    bool   enable_hot_reload;
    MobiusOverrideBehavior override_behavior;

    /* -- Fiber configuration -- */

    size_t fiber_stack_size;         /* Per-fiber stack size in bytes.
                                       Default: 131072 (128 KiB). */

    size_t initial_fiber_pool_size;  /* Fibers pre-allocated on first spawn.
                                       Default: 16. Pool doubles on exhaustion. */

    size_t max_fiber_pool_size;      /* Hard cap on total fibers in the pool.
                                       Default: 256. */

    int    max_worker_threads;       /* Additional OS worker threads for this state.
                                       Default: hardware_concurrency() / 2, floor 1.
                                       Set to 0 for single-threaded cooperative mode.
                                       The calling thread always participates as a
                                       worker, so total workers = this value + 1. */
} MobiusConfig;

/**
 * Return a MobiusConfig populated with sensible defaults.
 */
MOBIUS_API MobiusConfig mobius_default_config(void);

/* ====================================================================== */
/*  Runtime Metrics                                                        */
/* ====================================================================== */

typedef struct {
    /* VM execution high-water marks */
    size_t   peak_call_depth;
    size_t   peak_registers;
    size_t   peak_upvalues;
    size_t   peak_try_depth;

    /* State-level high-water marks */
    size_t   peak_globals;
    size_t   peak_interned_strings;

    /* Fiber/threading high-water marks (zeroed until fibers land) */
    size_t   peak_fibers;
    size_t   peak_worker_threads;
    size_t   total_fibers_spawned;
    size_t   total_jobs_executed;
    size_t   peak_fiber_stack_bytes;
    size_t   avg_fiber_stack_bytes;

    /* Timing */
    uint64_t total_execution_time_ns;
} MobiusMetrics;

/**
 * Copy the current metrics snapshot into *out.
 */
MOBIUS_API void mobius_get_metrics(MobiusState* state, MobiusMetrics* out);

/**
 * Reset all metrics counters and high-water marks to zero.
 */
MOBIUS_API void mobius_reset_metrics(MobiusState* state);

/* ====================================================================== */
/*  Error handling                                                         */
/* ====================================================================== */

/**
 * Error information passed to the error handler callback.
 * The struct and all its string pointers are only valid for the duration
 * of the callback invocation — do not store them.  Copy if needed.
 */
typedef struct {
    int         code;
    const char* message;
    const char* suggestion;    /* may be NULL */
    const char* filename;      /* may be NULL */
    int         line;
    int         column;
    const char* function_name; /* may be NULL */
} MobiusError;

/**
 * Error handler callback signature.
 * @param state     The interpreter that raised the error.
 * @param error     Error details (valid only for the duration of the call).
 * @param userdata  The opaque pointer supplied to mobius_set_error_handler().
 */
typedef void (*MobiusErrorHandler)(MobiusState* state, const MobiusError* error,
                                   void* userdata);

/**
 * Set the error handler for this interpreter instance.
 *
 * A default handler that prints errors to stderr is installed automatically
 * by mobius_new_state().  Pass NULL to restore the default handler.
 *
 * @param handler   New error handler, or NULL to restore the default.
 * @param userdata  Opaque pointer forwarded to the handler (may be NULL).
 * @return The previous handler, or NULL if it was the default.
 */
MOBIUS_API MobiusErrorHandler mobius_set_error_handler(MobiusState* state,
                                                      MobiusErrorHandler handler,
                                                      void* userdata);

/**
 * Clear the last error stored in the interpreter.
 */
MOBIUS_API void mobius_clear_error(MobiusState* state);

/* ====================================================================== */
/*  Lifecycle                                                              */
/* ====================================================================== */

/**
 * Create a new interpreter instance.
 * @param config  Configuration, or NULL for defaults.
 * @return  Opaque state handle, or NULL on failure.
 */
MOBIUS_API MobiusState* mobius_new_state(MobiusConfig* config);

/**
 * Initialise the standard library (print, math, string, etc.).
 * Call once after mobius_new_state().
 * @return MOBIUS_OK on success.
 */
MOBIUS_API int mobius_init_stdlib(MobiusState* state);

/**
 * Destroy the interpreter and free all resources.
 */
MOBIUS_API void mobius_free_state(MobiusState* state);

/* ====================================================================== */
/*  Execution                                                              */
/* ====================================================================== */

/**
 * Execute a string of Mobius source code.
 * @return MOBIUS_OK on success, or an error code.
 */
MOBIUS_API int mobius_exec_string(MobiusState* state, const char* code);

/**
 * Execute a Mobius source file.
 * @return MOBIUS_OK on success, or an error code.
 */
MOBIUS_API int mobius_exec_file(MobiusState* state, const char* filename);

/* ====================================================================== */
/*  Module / plugin management                                             */
/* ====================================================================== */

/**
 * Add a directory to the plugin/module search path.
 * Modules are loaded lazily on first import.
 */
MOBIUS_API void mobius_add_plugin_directory(const char* path);

/**
 * Clear all configured plugin/module search directories.
 */
MOBIUS_API void mobius_clear_plugin_directories(void);

/* ====================================================================== */
/*  REPL                                                                   */
/* ====================================================================== */

/**
 * Start the interactive read-eval-print loop.
 * Blocks until the user exits.
 */
MOBIUS_API void mobius_start_repl(MobiusState* state);

#ifdef __cplusplus
}
#endif

#endif /* MOBIUS_H */
