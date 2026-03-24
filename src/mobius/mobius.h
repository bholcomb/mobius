#ifndef MOBIUS_INTERNAL_H
#define MOBIUS_INTERNAL_H

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

// Extended functionality
#include "util/file_io.h"
#include "repl.h"
#include "library/library.h"

// Plugin system
#include "plugin/plugin.h"
#include "plugin/module_registry.h"


#endif // MOBIUS_INTERNAL_H
