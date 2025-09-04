# Mobius Scripting Language - Library-based Build System
# Organized structure: libmobius + application + modules

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O0 -fPIC
LDFLAGS = -lm -ldl

# Directories
SRC_DIR = src
MOBIUS_DIR = $(SRC_DIR)/mobius
MODULES_DIR = $(SRC_DIR)/modules
EXAMPLES_DIR = $(SRC_DIR)/examples
BUILD_DIR = build
BIN_DIR = bin
DATA_DIR = data
DOC_DIR = doc

# Library and executable names
MOBIUS_LIB = $(BUILD_DIR)/libmobius.a
MOBIUS_SHARED = $(BUILD_DIR)/libmobius.so
TARGET = $(BIN_DIR)/mobius

# Module targets
STDLIB_MODULE = $(BIN_DIR)/modules/stdlib.so
MATHLIB_MODULE = $(BIN_DIR)/modules/mathlib.so

# Source files
MOBIUS_SOURCES = $(wildcard $(MOBIUS_DIR)/*.c)
MOBIUS_OBJECTS = $(MOBIUS_SOURCES:$(MOBIUS_DIR)/%.c=$(BUILD_DIR)/mobius_%.o)

APP_SOURCES = $(SRC_DIR)/main.c
APP_OBJECTS = $(APP_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Default target
all: directories $(MOBIUS_LIB) $(TARGET) modules

# Create necessary directories
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(BIN_DIR)/modules

# Build the Mobius static library
$(MOBIUS_LIB): $(MOBIUS_OBJECTS)
	@echo "📚 Building Mobius library..."
	ar rcs $@ $^

# Build the Mobius shared library
$(MOBIUS_SHARED): $(MOBIUS_OBJECTS)
	@echo "🔗 Building Mobius shared library..."
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LDFLAGS)

# Build the main executable
$(TARGET): $(APP_OBJECTS) $(MOBIUS_LIB)
	@echo "🚀 Building Mobius interpreter..."
	$(CC) $(APP_OBJECTS) -L$(BUILD_DIR) -lmobius -o $@ $(LDFLAGS)

# Compile Mobius library object files
$(BUILD_DIR)/mobius_%.o: $(MOBIUS_DIR)/%.c | directories
	@echo "🔧 Compiling $<..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# Compile application object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | directories
	@echo "🔧 Compiling $<..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# Build all modules
modules: $(STDLIB_MODULE) $(MATHLIB_MODULE)

# Build stdlib module
$(STDLIB_MODULE): $(MODULES_DIR)/stdlib/stdlib_plugin.c $(MOBIUS_LIB) | directories
	@echo "📦 Building stdlib module..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -shared -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build mathlib example module  
$(MATHLIB_MODULE): $(EXAMPLES_DIR)/mathlib.c $(MOBIUS_LIB) | directories
	@echo "📦 Building mathlib example module..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -shared -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Install (copy to system directories)
install: $(TARGET) $(MOBIUS_LIB)
	@echo "📥 Installing Mobius..."
	sudo cp $(TARGET) /usr/local/bin/
	sudo cp $(MOBIUS_LIB) /usr/local/lib/
	sudo mkdir -p /usr/local/include/mobius
	sudo cp $(MOBIUS_DIR)/*.h /usr/local/include/mobius/
	sudo ldconfig

# Uninstall
uninstall:
	@echo "🗑️  Uninstalling Mobius..."
	sudo rm -f /usr/local/bin/mobius
	sudo rm -f /usr/local/lib/libmobius.a
	sudo rm -rf /usr/local/include/mobius

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)/*
	rm -rf $(BIN_DIR)/*

# Run the interpreter
run: $(TARGET)
	./$(TARGET)

# Test the interpreter with plugins
test: $(TARGET) modules
	@echo "🧪 Testing Mobius interpreter..."
	./$(TARGET) --list-modules
	@echo ""
	./$(TARGET) $(DATA_DIR)/test_plugins.mob

# Test plugin loading
test-plugins: $(TARGET) modules
	@echo "🔌 Testing plugin system..."
	./$(TARGET) --list-modules

# Development build (debug symbols)
debug: CFLAGS += -DDEBUG -g3 -O0
debug: clean all

# Release build (optimized)
release: CFLAGS += -DNDEBUG -O2 -s
release: clean all

# Create development documentation
docs:
	@echo "📖 Generating documentation..."
	doxygen Doxyfile 2>/dev/null || echo "⚠️  Doxygen not found, skipping docs"

# Library information
info:
	@echo "📋 Mobius Library Information:"
	@echo "   Library: $(MOBIUS_LIB)"
	@echo "   Executable: $(TARGET)"
	@echo "   Modules: $(BIN_DIR)/modules/"
	@echo "   Source: $(MOBIUS_DIR)/"
	@echo "   Build: $(BUILD_DIR)/"

# Show help
help:
	@echo "🎯 Mobius Build System Help:"
	@echo ""
	@echo "Main targets:"
	@echo "  all        - Build library, executable, and modules (default)"
	@echo "  clean      - Remove all build artifacts"
	@echo "  install    - Install Mobius system-wide"
	@echo "  uninstall  - Remove system-wide installation"
	@echo ""
	@echo "Development:"
	@echo "  debug      - Build with debug symbols"
	@echo "  release    - Build optimized release version"
	@echo "  docs       - Generate documentation"
	@echo ""
	@echo "Testing:"
	@echo "  run        - Run the interpreter"
	@echo "  test       - Run comprehensive tests"
	@echo "  test-plugins - Test plugin loading"
	@echo ""
	@echo "Information:"
	@echo "  info       - Show build configuration"
	@echo "  help       - Show this help message"

# Declare phony targets
.PHONY: all directories modules clean install uninstall run test test-plugins debug release docs info help

# Special target dependencies
$(TARGET): | $(MOBIUS_LIB)
$(STDLIB_MODULE): | $(MOBIUS_LIB)
$(MATHLIB_MODULE): | $(MOBIUS_LIB)