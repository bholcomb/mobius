#include "stdlib_init.h"
#include "library.h"
#include "../value.h"
#include "../environment.h"

// Register all standard library functions as VAL_NATIVE_FUNCTION values
// in the given environment
void register_stdlib_functions(Environment* env) {
    if (!env) return;
    
    // Get the library registry
    const LibraryEntry* registry = get_library_registry();
    if (!registry) return;
    
    // Iterate through all stdlib functions and register them
    const LibraryEntry* entry = registry;
    while (entry->name != NULL) {
        // Create a VAL_NATIVE_FUNCTION value for this function
        Value func_value = make_native_function_value((void*)entry->func);
        
        // Define it in the environment
        define_variable(env, entry->name, func_value);
        
        entry++;
    }
}

