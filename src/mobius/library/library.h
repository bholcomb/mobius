#ifndef MOBIUS_LIBRARY_H
#define MOBIUS_LIBRARY_H

#include "data/value.h"
#include "state/environment.h"
#include "state/mobius_state.h"
#include "plugin/plugin.h"

// =============================================================================
// UNIFIED LIBRARY INTERFACE
// =============================================================================

/**
 * Register all standard library functions in the global environment
 * This function iterates through the library registry and registers each
 * function as a native function in the state's global environment.
 * 
 * @param state The MobiusState containing the global environment
 */
void register_stdlib_functions(MobiusState* state);

/**
 * Get the library function registry (for inspection/tooling)
 * Useful for debugging, documentation generation, or runtime introspection.
 * 
 * @return Pointer to the library function registry array (NULL-terminated)
 */
const PluginFunction* get_library_registry(void);

/**
 * Get the number of functions in the library registry
 * @return Number of registered library functions (excluding sentinel)
 */
MOBIUS_API size_t get_library_function_count(void);

#endif // MOBIUS_LIBRARY_H
