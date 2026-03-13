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
} MobiusConfig;

/**
 * Return a MobiusConfig populated with sensible defaults.
 */
MobiusConfig mobius_default_config(void);

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
MobiusErrorHandler mobius_set_error_handler(MobiusState* state,
                                           MobiusErrorHandler handler,
                                           void* userdata);

/* ====================================================================== */
/*  Lifecycle                                                              */
/* ====================================================================== */

/**
 * Create a new interpreter instance.
 * @param config  Configuration, or NULL for defaults.
 * @return  Opaque state handle, or NULL on failure.
 */
MobiusState* mobius_new_state(MobiusConfig* config);

/**
 * Initialise the standard library (print, math, string, etc.).
 * Call once after mobius_new_state().
 * @return MOBIUS_OK on success.
 */
int mobius_init_stdlib(MobiusState* state);

/**
 * Destroy the interpreter and free all resources.
 */
void mobius_free_state(MobiusState* state);

/* ====================================================================== */
/*  Execution                                                              */
/* ====================================================================== */

/**
 * Execute a string of Mobius source code.
 * @return MOBIUS_OK on success, or an error code.
 */
int mobius_exec_string(MobiusState* state, const char* code);

/**
 * Execute a Mobius source file.
 * @return MOBIUS_OK on success, or an error code.
 */
int mobius_exec_file(MobiusState* state, const char* filename);

/* ====================================================================== */
/*  Module / plugin management                                             */
/* ====================================================================== */

/**
 * Add a directory to the plugin search path.
 * Call before mobius_new_state() or mobius_init_stdlib().
 */
void mobius_add_plugin_directory(const char* path);

/**
 * Return the number of currently loaded modules.
 */
size_t mobius_get_module_count(MobiusState* state);

/**
 * Print a summary of loaded modules to stdout.
 */
void mobius_print_modules(MobiusState* state);

/* ====================================================================== */
/*  REPL                                                                   */
/* ====================================================================== */

/**
 * Start the interactive read-eval-print loop.
 * Blocks until the user exits.
 */
void mobius_start_repl(MobiusState* state);

#ifdef __cplusplus
}
#endif

#endif /* MOBIUS_H */
