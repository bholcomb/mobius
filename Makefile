# Mobius Scripting Language - Simple Makefile
# No Qt, no CMake, no qmake - just clean, simple make

# Compiler and flags
CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O2 -fPIC
CXXFLAGS = -Wall -Wextra -std=c++17 -g -O2 -fPIC -fpermissive
CPPFLAGS = -Iinclude -Isrc -Isrc/mobius
LDFLAGS = -lm -ldl

# Directories
SRCDIR = src
BUILDDIR = build
BINDIR = bin
OBJDIR = $(BUILDDIR)

# Core library sources (C++)
MOBIUS_SOURCES = \
	$(SRCDIR)/mobius/data/array.cpp \
	$(SRCDIR)/mobius/data/enum.cpp \
	$(SRCDIR)/mobius/data/number.cpp \
	$(SRCDIR)/mobius/data/table.cpp \
	$(SRCDIR)/mobius/data/value.cpp \
	$(SRCDIR)/mobius/frontend/ast.cpp \
	$(SRCDIR)/mobius/frontend/parser.cpp \
	$(SRCDIR)/mobius/frontend/scanner.cpp \
	$(SRCDIR)/mobius/frontend/token.cpp \
	$(SRCDIR)/mobius/eval/eval_arithmatic.cpp \
	$(SRCDIR)/mobius/eval/eval_array.cpp \
	$(SRCDIR)/mobius/eval/eval_core.cpp \
	$(SRCDIR)/mobius/eval/eval_enum.cpp \
	$(SRCDIR)/mobius/eval/eval_error.cpp \
	$(SRCDIR)/mobius/eval/eval_expression.cpp \
	$(SRCDIR)/mobius/eval/eval_import.cpp \
	$(SRCDIR)/mobius/eval/eval_statement.cpp \
	$(SRCDIR)/mobius/eval/eval_switch.cpp \
	$(SRCDIR)/mobius/eval/eval_table.cpp \
	$(SRCDIR)/mobius/internal/string_intern.cpp \
	$(SRCDIR)/mobius/library/array.cpp \
	$(SRCDIR)/mobius/library/core.cpp \
	$(SRCDIR)/mobius/library/library.cpp \
	$(SRCDIR)/mobius/library/math.cpp \
	$(SRCDIR)/mobius/library/string.cpp \
	$(SRCDIR)/mobius/library/table_lib.cpp \
	$(SRCDIR)/mobius/library/types.cpp \
	$(SRCDIR)/mobius/library/util.cpp \
	$(SRCDIR)/mobius/state/environment.cpp \
	$(SRCDIR)/mobius/state/mobius_state.cpp \
	$(SRCDIR)/mobius/state/stack.cpp \
	$(SRCDIR)/mobius/plugin/module_registry.cpp \
	$(SRCDIR)/mobius/util/file_io.cpp \
	$(SRCDIR)/mobius/util/utility.cpp \
	$(SRCDIR)/mobius/repl.cpp \

# All library sources combined  
ALL_LIB_SOURCES = $(MOBIUS_SOURCES)

# Main executable source
MAIN_SOURCE = $(SRCDIR)/main.cpp

# Object files - preserve directory structure to avoid naming conflicts
MOBIUS_OBJECTS = $(MOBIUS_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
MAIN_OBJECT = $(MAIN_SOURCE:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

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

# Build main executable (link with C++ linker since library contains C++)
$(MOBIUS_EXE): $(MAIN_OBJECT) $(LIBMOBIUS)
	@echo "Linking: $@"
	$(CXX) $(CXXFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS) -lstdc++

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
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/simple_embedding: examples/simple_embedding/simple_embedding.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/game_engine: examples/game_engine/game_engine.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/multi_environment_demo: examples/multi_environment_demo/multi_environment_demo.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/simple_userdata_test: examples/simple_userdata_test/simple_userdata_test.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

$(BINDIR)/stack_api_test: examples/stack_api_test/stack_api_test.c $(LIBMOBIUS)
	@echo "Building example: $@"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

# Build text processing plugin as shared library
$(BINDIR)/modules/text_processing.so: examples/text_processing_plugin/text_processing_plugin.c $(LIBMOBIUS)
	@echo "Building plugin: $@"
	@mkdir -p $(BINDIR)/modules
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fPIC -shared -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

# Build math plugin as shared library
$(BINDIR)/modules/math.so: $(SRCDIR)/modules/math/math_plugin.c $(LIBMOBIUS)
	@echo "Building plugin: $@"
	@mkdir -p $(BINDIR)/modules
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fPIC -shared -o $@ $< -L$(BUILDDIR) -lmobius $(LDFLAGS)

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

