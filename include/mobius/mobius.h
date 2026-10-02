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

#define MOBIUS_VERSION_MAJOR  0
#define MOBIUS_VERSION_MINOR  1
#define MOBIUS_VERSION_PATCH  0
#define MOBIUS_VERSION_STRING "0.1.0"

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
#define MOBIUS_ERROR_BUSY       8   /* a paused execution must be resumed or aborted first */
#define MOBIUS_ERROR_ABORTED    9   /* mobius_abort stopped the execution */
#define MOBIUS_PAUSED          10   /* not an error: paused, continue with mobius_resume */

/* ====================================================================== */
/*  Configuration                                                          */
/* ====================================================================== */

typedef enum {
    MOBIUS_OVERRIDE_ERROR,   /* Error on function name conflict (default) */
    MOBIUS_OVERRIDE_WARN,    /* Warn but allow */
    MOBIUS_OVERRIDE_QUIET    /* Silent override */
} MobiusOverrideBehavior;

typedef struct {
    size_t initial_stack_size;       /* Reserved; not used. */
    size_t max_stack_size;           /* Reserved; not enforced. Recursion is
                                       limited by max_call_depth. */
    size_t max_call_depth;
    bool   strict_mode;
    bool   warn_on_conversion;
    bool   debug_mode;
    MobiusOverrideBehavior override_behavior;

    /* -- Fiber configuration -- */

    size_t fiber_stack_size;         /* Per-fiber stack size in bytes for pooled
                                       worker fibers (spawned work).
                                       Default: 524288 (512 KiB).  Deep native
                                       calls (e.g. Vulkan drivers) can overflow a
                                       smaller per-fiber stack. */

    size_t main_fiber_stack_size;    /* Stack size in bytes for the top-level
                                       (main) script fiber, which hosts the whole
                                       script and any deep native calls it makes.
                                       Larger than fiber_stack_size so it behaves
                                       like a normal thread stack.
                                       Default: 8388608 (8 MiB). 0 = use
                                       fiber_stack_size. */

    size_t initial_fiber_pool_size;  /* Fibers pre-allocated on first spawn.
                                       Default: 16. Pool doubles on exhaustion. */

    size_t max_fiber_pool_size;      /* Hard cap on total fibers in the pool.
                                       Default: 256. */

    int    max_worker_threads;       /* Additional OS worker threads for this state.
                                       Default: hardware_concurrency() / 2 - 1,
                                       floor 1.
                                       Set to 0 for single-threaded cooperative mode.
                                       The calling thread always participates as a
                                       worker, so total workers = this value + 1. */

    /* -- Memory footprint tuning -- */

    size_t string_pool_buckets;      /* Initial hash buckets in the string intern
                                       pool. Rounded up to a power of two; the
                                       pool grows as needed. Default: 65536
                                       (sized for speed). Memory-tight embeddings
                                       can start much smaller, e.g. 1024. */

    size_t global_slot_capacity;     /* Preallocated global-variable slots.
                                       Default: 16384. This is currently a hard
                                       cap as well as a preallocation, so set it
                                       to the most globals (including stdlib
                                       registrations) the state may ever hold. */
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
 * The handler is not called for errors raised while a script `try` block
 * is active: those unwind to the script's `catch`.
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
/*  Output and exit                                                        */
/* ====================================================================== */

#define MOBIUS_STDOUT 1
#define MOBIUS_STDERR 2

/**
 * Receives text the interpreter writes: print() output on MOBIUS_STDOUT
 * (one call per print, including its newline), and on MOBIUS_STDERR
 * errors from the default error handler and warnings. `data` is valid
 * only during the call and is not NUL-terminated. May be called from any
 * worker thread running the state's fibers, concurrently.
 */
typedef void (*MobiusOutputHandler)(MobiusState* state, int stream,
                                    const char* data, size_t length,
                                    void* userdata);

/**
 * Route this state's output to `handler`; NULL restores the default
 * (the process's stdout and stderr).
 */
MOBIUS_API void mobius_set_output_handler(MobiusState* state,
                                          MobiusOutputHandler handler,
                                          void* userdata);

/**
 * Called when a script calls exit(code). The handler decides what that
 * means (the mobius command-line tool ends the process). When it returns,
 * the script continues after the exit() call. Without a handler, exit()
 * does nothing but write a warning to MOBIUS_STDERR.
 */
typedef void (*MobiusExitHandler)(MobiusState* state, int code, void* userdata);

MOBIUS_API void mobius_set_exit_handler(MobiusState* state,
                                        MobiusExitHandler handler,
                                        void* userdata);

/* ====================================================================== */
/*  Files                                                                  */
/* ====================================================================== */

/** Passed to a file-system callback to hand back data or an error. */
typedef struct MobiusFileRequest MobiusFileRequest;

/**
 * Host implementation of the files scripts can reach: readfile,
 * readlines, writefile, appendfile, file_exists, load(), and import of
 * .mob modules all go through it when it is set (a game can serve scripts
 * from its own archives). Paths are passed exactly as scripts wrote them.
 * Any member may be NULL: that operation is then not available.
 * Callbacks may run on any worker thread, concurrently.
 */
typedef struct {
    /* Read a whole file: call mobius_file_set_data(request, ...) and
       return MOBIUS_OK, or mobius_file_set_error(request, ...) and return
       any other value. */
    int (*read)(MobiusState* state, const char* path,
                MobiusFileRequest* request, void* userdata);
    /* Write (append = 0) or append (append = 1) `length` bytes. Return
       MOBIUS_OK, or an error as for read. */
    int (*write)(MobiusState* state, const char* path,
                 const char* data, size_t length, int append,
                 MobiusFileRequest* request, void* userdata);
    /* 1 if the file exists, 0 if not. */
    int (*exists)(MobiusState* state, const char* path, void* userdata);
} MobiusFileSystem;

/**
 * Use `fs` (copied) for this state's file access; NULL restores the
 * default (the real file system, unless the state is sandboxed).
 */
MOBIUS_API void mobius_set_file_system(MobiusState* state,
                                       const MobiusFileSystem* fs,
                                       void* userdata);

/** From a read callback: the file's contents (copied). */
MOBIUS_API void mobius_file_set_data(MobiusFileRequest* request,
                                     const char* data, size_t length);

/** From a read or write callback: why it failed (copied). */
MOBIUS_API void mobius_file_set_error(MobiusFileRequest* request,
                                      const char* message);

/* ====================================================================== */
/*  Sandbox                                                                */
/* ====================================================================== */

#define MOBIUS_CAP_OUTPUT 0x1u   /* print and error text to stdout/stderr */
#define MOBIUS_CAP_FILES  0x2u   /* the real file system (files, load, import) */

/**
 * Sandbox the state, before running untrusted scripts. Native plugins are
 * never loaded. Everything a script could use to reach outside goes to
 * the host's handlers (mobius_set_output_handler, mobius_set_exit_handler,
 * mobius_set_file_system); where the host set none, the built-in behavior
 * is used only if `allow` includes its MOBIUS_CAP_ flag. Otherwise:
 *   - file functions, load() and imports of .mob files raise a catchable
 *     "not available" error (modules the host registered, and fiber, can
 *     still be imported);
 *   - print output and error text are discarded;
 *   - exit() only warns (as without a sandbox).
 * Calling it again changes `allow`.
 */
MOBIUS_API void mobius_sandbox(MobiusState* state, unsigned int allow);

/* ====================================================================== */
/*  Pausing, time limits and abort                                         */
/* ====================================================================== */

/**
 * Give each mobius_exec_string / mobius_exec_file / mobius_resume call at
 * most `milliseconds` of running time (0, the default: no limit). When it
 * runs out, the script and all its fibers pause at their next safe point
 * (a loop iteration or a function call), the call returns MOBIUS_PAUSED,
 * and a warning goes to the state's error output. A pause waits while a
 * host function is running inside the script.
 */
MOBIUS_API void mobius_set_time_limit(MobiusState* state, unsigned int milliseconds);

/**
 * Ask the state to pause at its next safe point (any thread). The running
 * exec/resume call returns MOBIUS_PAUSED. Fibers left running in the
 * background by an earlier call pause too.
 */
MOBIUS_API void mobius_pause(MobiusState* state);

/**
 * Continue a paused execution. Returns MOBIUS_OK when it finishes, an
 * error code if it fails, or MOBIUS_PAUSED if it pauses again. Without a
 * paused execution, lets paused background fibers run and returns
 * MOBIUS_OK. While an execution is paused, the state accepts only
 * mobius_resume and mobius_abort; other exec calls return
 * MOBIUS_ERROR_BUSY.
 */
MOBIUS_API int mobius_resume(MobiusState* state);

/**
 * Stop the state's execution and every fiber, at their next safe point or
 * wait. Scripts can't catch it, and finally blocks don't run. A paused
 * execution is discarded on the calling thread before this returns; a
 * running one (on another thread) returns MOBIUS_ERROR_ABORTED from its
 * exec call. The state stays usable: globals keep what was assigned
 * before the abort.
 */
MOBIUS_API int mobius_abort(MobiusState* state);

/** 1 if the state has a paused execution, else 0. */
MOBIUS_API int mobius_is_paused(MobiusState* state);

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
 * Add a directory to the plugin/module search path for this state.
 * Modules are loaded lazily on first import.
 */
MOBIUS_API void mobius_add_plugin_directory(MobiusState* state, const char* path);

/**
 * Clear all configured plugin/module search directories for this state.
 */
MOBIUS_API void mobius_clear_plugin_directories(MobiusState* state);

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
