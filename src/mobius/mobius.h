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
#include "frontend/token.h"
#include "frontend/ast.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "state/environment.h"
#include "eval/evaluator.h"

// Extended functionality
#include "util/file_io.h"
#include "repl.h"
#include "library/library.h"

// Plugin system
#include "plugin/plugin.h"
#include "plugin/module_registry.h"

// Library version
#define MOBIUS_VERSION_MAJOR 0
#define MOBIUS_VERSION_MINOR 1
#define MOBIUS_VERSION_PATCH 0
#define MOBIUS_VERSION_STRING "0.1.0"

// Library initialization and cleanup (deprecated - use embedding API)
// int mobius_init(void);
// void mobius_cleanup(void);

#endif // MOBIUS_H
