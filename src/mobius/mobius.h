#ifndef MOBIUS_H
#define MOBIUS_H

/*
 * Mobius Scripting Language Library
 * Public API Header
 * 
 * This header provides the complete public interface for the Mobius
 * scripting language interpreter library.
 */

// Core components
#include "token.h"
#include "ast.h"
#include "scanner.h"
#include "parser.h"
#include "environment.h"
#include "evaluator.h"

// Extended functionality
#include "file_io.h"
#include "repl.h"
#include "stdlib.h"

// Plugin system
#include "plugin.h"
#include "module_registry.h"

// Library version
#define MOBIUS_VERSION_MAJOR 0
#define MOBIUS_VERSION_MINOR 1
#define MOBIUS_VERSION_PATCH 0
#define MOBIUS_VERSION_STRING "0.1.0"

// Library initialization and cleanup
int mobius_init(void);
void mobius_cleanup(void);

// Convenience functions for embedding
typedef struct {
    ModuleRegistry* registry;
    Environment* global_env;
    bool initialized;
} MobiusContext;

MobiusContext* mobius_create_context(void);
void mobius_free_context(MobiusContext* ctx);
EvalResult mobius_eval_string(MobiusContext* ctx, const char* source);
EvalResult mobius_eval_file(MobiusContext* ctx, const char* filename);

#endif // MOBIUS_H
