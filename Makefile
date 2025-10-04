# Mobius Scripting Language - Simple Makefile
# No Qt, no CMake, no qmake - just clean, simple make

# Compiler and flags
CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O2 -fPIC
CXXFLAGS = -Wall -Wextra -std=c++11 -g -O2 -fPIC
CPPFLAGS = -Isrc -Isrc/mobius
LDFLAGS = -lm -ldl

# Directories
SRCDIR = src
BUILDDIR = build
BINDIR = bin
OBJDIR = $(BUILDDIR)

# Core library sources
MOBIUS_SOURCES = \
	$(SRCDIR)/mobius/data/array.c \
	$(SRCDIR)/mobius/data/enum.c \
	$(SRCDIR)/mobius/data/number.c \
	$(SRCDIR)/mobius/data/table.c \
	$(SRCDIR)/mobius/data/value.c \
	$(SRCDIR)/mobius/internal/refString.c \
	$(SRCDIR)/mobius/frontend/ast.c \
	$(SRCDIR)/mobius/frontend/parser.c \
	$(SRCDIR)/mobius/frontend/scanner.c \
	$(SRCDIR)/mobius/frontend/token.c \
	$(SRCDIR)/mobius/eval/eval_arithmatic.c \
	$(SRCDIR)/mobius/eval/eval_array.c \
	$(SRCDIR)/mobius/eval/eval_core.c \
	$(SRCDIR)/mobius/eval/eval_enum.c \
	$(SRCDIR)/mobius/eval/eval_error.c \
	$(SRCDIR)/mobius/eval/eval_expression.c \
	$(SRCDIR)/mobius/eval/eval_import.c \
	$(SRCDIR)/mobius/eval/eval_statement.c \
	$(SRCDIR)/mobius/eval/eval_switch.c \
	$(SRCDIR)/mobius/eval/eval_table.c \
	$(SRCDIR)/mobius/library/array.c \
	$(SRCDIR)/mobius/library/core.c \
	$(SRCDIR)/mobius/library/library.c \
	$(SRCDIR)/mobius/library/math.c \
	$(SRCDIR)/mobius/library/string.c \
	$(SRCDIR)/mobius/library/table_lib.c \
	$(SRCDIR)/mobius/library/types.c \
	$(SRCDIR)/mobius/library/util.c \
	$(SRCDIR)/mobius/state/environment.c \
	$(SRCDIR)/mobius/state/mobius_state.c \
	$(SRCDIR)/mobius/plugin/module_registry.c \
	$(SRCDIR)/mobius/util/file_io.c \
	$(SRCDIR)/mobius/util/utility.c \
	$(SRCDIR)/mobius/repl.c \

# All library sources combined  
ALL_LIB_SOURCES = $(MOBIUS_SOURCES)

# Main executable source
MAIN_SOURCE = $(SRCDIR)/main.c

# Object files - preserve directory structure to avoid naming conflicts
MOBIUS_OBJECTS = $(MOBIUS_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
MAIN_OBJECT = $(MAIN_SOURCE:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

ALL_OBJECTS = $(MOBIUS_OBJECTS) $(MAIN_OBJECT)

# Library and executables
LIBMOBIUS = $(BUILDDIR)/libmobius.a
MOBIUS_EXE = $(BINDIR)/mobius

# Example executables
EXAMPLES = \
	$(BINDIR)/embedding_example \
	$(BINDIR)/simple_embedding \
	$(BINDIR)/game_engine \
	$(BINDIR)/multi_environment_demo \
	$(BINDIR)/simple_userdata_test

# Plugin libraries
PLUGINS = \
	$(BINDIR)/modules/text_processing.so \
	$(BINDIR)/modules/math.so

# Default target
all: directories $(LIBMOBIUS) $(MOBIUS_EXE) examples plugins

# Create necessary directories
directories:
	@mkdir -p $(BUILDDIR)/modules/math
	@mkdir -p $(BINDIR)/modules

# Build static library
$(LIBMOBIUS): $(MOBIUS_OBJECTS)
	@echo "Creating static library: $@"
	ar rcs $@ $^

# Build main executable
$(MOBIUS_EXE): $(MAIN_OBJECT) $(LIBMOBIUS)
	@echo "Linking: $@"
	$(CC) $(CFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

# Generic rule for C object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Generic rule for C++ object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "Compiling: $<"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Example executables
$(BINDIR)/embedding_example: examples/embedding_example/embedding_example.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/simple_embedding: examples/simple_embedding/simple_embedding.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/game_engine: examples/game_engine/game_engine.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/multi_environment_demo: examples/multi_environment_demo/multi_environment_demo.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/simple_userdata_test: examples/simple_userdata_test/simple_userdata_test.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

# Build text processing plugin as shared library
$(BINDIR)/modules/text_processing.so: examples/text_processing_plugin/text_processing_plugin.c $(LIBMOBIUS)
	@echo "Building plugin: $@"
	@mkdir -p $(BINDIR)/modules
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -shared -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

# Build math plugin as shared library
$(BINDIR)/modules/math.so: $(SRCDIR)/modules/math/math_plugin.c $(LIBMOBIUS)
	@echo "Building plugin: $@"
	@mkdir -p $(BINDIR)/modules
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -shared -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

# Build all examples
examples: $(EXAMPLES)

# Build all plugins
plugins: $(PLUGINS)

# Test runner
test: $(MOBIUS_EXE)
	@echo "Running basic tests..."
	@if [ -f test_simple.sh ]; then ./test_simple.sh; else echo "No test_simple.sh found"; fi

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILDDIR) $(BINDIR)
	rm -f *.o *~ core

# Install (optional)
install: $(MOBIUS_EXE) $(LIBMOBIUS)
	@echo "Installing Mobius..."
	install -d $(DESTDIR)/usr/local/bin
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/mobius
	install -m 755 $(MOBIUS_EXE) $(DESTDIR)/usr/local/bin/
	install -m 644 $(LIBMOBIUS) $(DESTDIR)/usr/local/lib/
	install -m 644 src/mobius/*.h $(DESTDIR)/usr/local/include/mobius/

# Uninstall
uninstall:
	@echo "Uninstalling Mobius..."
	rm -f $(DESTDIR)/usr/local/bin/mobius
	rm -f $(DESTDIR)/usr/local/lib/libmobius.a
	rm -rf $(DESTDIR)/usr/local/include/mobius

# Help
help:
	@echo "Mobius Scripting Language Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all        - Build library, main executable, and examples (default)"
	@echo "  clean      - Remove all build artifacts"
	@echo "  test       - Run basic tests"
	@echo "  examples   - Build all example programs"
	@echo "  install    - Install to system directories"
	@echo "  uninstall  - Remove from system directories"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Individual targets:"
	@echo "  $(MOBIUS_EXE) - Main Mobius interpreter"
	@echo "  $(LIBMOBIUS) - Static library"
	@echo "  directories - Create build directories"

# Phony targets
.PHONY: all clean test examples install uninstall help directories

