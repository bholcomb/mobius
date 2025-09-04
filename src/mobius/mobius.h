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

// Embedding API
#include "embedding.h"

// Library version
#define MOBIUS_VERSION_MAJOR 0
#define MOBIUS_VERSION_MINOR 1
#define MOBIUS_VERSION_PATCH 0
#define MOBIUS_VERSION_STRING "0.1.0"

// Library initialization and cleanup (deprecated - use embedding API)
// int mobius_init(void);
// void mobius_cleanup(void);

#endif // MOBIUS_H
