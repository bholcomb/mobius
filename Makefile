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
MATH_MODULE = $(BIN_DIR)/modules/math.so

# Example targets
EMBEDDING_EXAMPLE = $(BIN_DIR)/embedding_example
SIMPLE_EMBEDDING_EXAMPLE = $(BIN_DIR)/simple_embedding
GAME_ENGINE_EXAMPLE = $(BIN_DIR)/game_engine
CPP_CLASS_EXAMPLE = $(BIN_DIR)/cpp_class_example
SIMPLE_USERDATA_TEST = $(BIN_DIR)/simple_userdata_test

# Test suite targets
TEST_RUNNER = $(BIN_DIR)/test_runner
TEST_SUITE_DIR = test_suite

# Plugin targets
TEXT_PROCESSING_PLUGIN = $(BIN_DIR)/modules/text_processing.so

# Source files
MOBIUS_SOURCES = $(wildcard $(MOBIUS_DIR)/*.c)
MOBIUS_OBJECTS = $(MOBIUS_SOURCES:$(MOBIUS_DIR)/%.c=$(BUILD_DIR)/mobius_%.o)

APP_SOURCES = $(SRC_DIR)/main.c
APP_OBJECTS = $(APP_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Default target
all: directories $(MOBIUS_LIB) $(TARGET) modules examples tests

# Test suite target
tests: $(TEST_RUNNER)

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
modules: $(MATH_MODULE)

# Build math extension module
$(MATH_MODULE): $(MODULES_DIR)/math/math_plugin.c $(MOBIUS_LIB) | directories
	@echo "📦 Building math extension module..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -shared -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build examples
examples: $(EMBEDDING_EXAMPLE) $(SIMPLE_EMBEDDING_EXAMPLE) $(GAME_ENGINE_EXAMPLE) $(CPP_CLASS_EXAMPLE) $(SIMPLE_USERDATA_TEST) $(TEXT_PROCESSING_PLUGIN)

# Build embedding example
$(EMBEDDING_EXAMPLE): examples/embedding_example/embedding_example.c $(MOBIUS_LIB) | directories
	@echo "🔧 Building embedding example..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build simple embedding example
$(SIMPLE_EMBEDDING_EXAMPLE): examples/simple_embedding/simple_embedding.c $(MOBIUS_LIB) | directories
	@echo "🔧 Building simple embedding example..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build game engine example
$(GAME_ENGINE_EXAMPLE): examples/game_engine/game_engine.c $(MOBIUS_LIB) | directories
	@echo "🎮 Building game engine example..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build C++ class example
$(CPP_CLASS_EXAMPLE): examples/cpp_class_example/cpp_class_example.cpp $(MOBIUS_LIB) | directories
	@echo "🔧 Building C++ class example..."
	g++ -Wall -Wextra -std=c++11 -g -O0 -I$(SRC_DIR) -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build simple userdata test
$(SIMPLE_USERDATA_TEST): examples/simple_userdata_test/simple_userdata_test.c $(MOBIUS_LIB) | directories
	@echo "🔧 Building simple userdata test..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Build text processing plugin
$(TEXT_PROCESSING_PLUGIN): examples/text_processing_plugin/text_processing_plugin.c $(MOBIUS_LIB) | directories
	@echo "📝 Building text processing plugin..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -shared -o $@ $< -L$(BUILD_DIR) -lmobius $(LDFLAGS)

# Test runner
$(TEST_RUNNER): $(MOBIUS_LIB) $(TEST_SUITE_DIR)/test_runner.c $(TEST_SUITE_DIR)/test_framework.c $(TEST_SUITE_DIR)/test_cases.c | directories
	@echo "🧪 Building test runner..."
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $(TEST_RUNNER) $(TEST_SUITE_DIR)/test_runner.c $(TEST_SUITE_DIR)/test_framework.c $(TEST_SUITE_DIR)/test_cases.c -L$(BUILD_DIR) -lmobius $(LDFLAGS)

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

# Run comprehensive test suite
test: $(TARGET) modules
	@echo "🧪 Running Mobius test suite..."
	./run_tests.sh

# Test specific category
test-category: $(TARGET) modules
	@echo "🧪 Running category-specific tests..."
	@if [ -z "$(CATEGORY)" ]; then \
		echo "Usage: make test-category CATEGORY=<basic|types|tables|errors|integration>"; \
		exit 1; \
	fi
	./run_tests.sh --category $(CATEGORY)

# Legacy test (single plugin test)
test-legacy: $(TARGET) modules
	@echo "🧪 Testing Mobius interpreter (legacy)..."
	./$(TARGET) --list-modules
	@echo ""
	./$(TARGET) tests/integration/test_plugins.mob

# Test plugin loading
test-plugins: $(TARGET) modules
	@echo "🔌 Testing plugin system..."
	./$(TARGET) --list-modules

# Test embedding API
test-embedding: $(SIMPLE_EMBEDDING_EXAMPLE) modules
	@echo "🔗 Testing embedding API..."
	./$(SIMPLE_EMBEDDING_EXAMPLE)

# Test game engine example
test-game-engine: $(GAME_ENGINE_EXAMPLE) modules
	@echo "🎮 Testing game engine example..."
	./$(GAME_ENGINE_EXAMPLE)

# Test text processing plugin
test-text-plugin: $(TEXT_PROCESSING_PLUGIN) $(TARGET)
	@echo "📝 Testing text processing plugin..."
	@echo 'print("Text plugin test:"); print("Words:", word_count("hello beautiful world"));' | LD_LIBRARY_PATH=./bin/modules ./$(TARGET)

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
	@echo "  test-dual  - Run dual-backend validation tests"
	@echo "  test-performance - Run performance benchmarks"
	@echo "  test-plugins - Test plugin loading"
	@echo "  test-embedding - Test embedding API"
	@echo "  test-game-engine - Test game engine example"
	@echo "  test-text-plugin - Test text processing plugin"
	@echo ""
	@echo "Information:"
	@echo "  info       - Show build configuration"
	@echo "  help       - Show this help message"

# Test targets
test-dual: $(TEST_RUNNER)
	@echo "🧪 Running dual-backend validation tests..."
	./$(TEST_RUNNER) --validation

test-performance: $(TEST_RUNNER)
	@echo "⚡ Running performance benchmarks..."
	./$(TEST_RUNNER) --performance --benchmark

test-all-suites: $(TEST_RUNNER)
	@echo "🎯 Running complete test suite..."
	./$(TEST_RUNNER) --verbose

# Declare phony targets
.PHONY: all directories modules examples tests clean install uninstall run test test-dual test-performance test-all-suites test-plugins test-embedding test-game-engine test-text-plugin debug release docs info help

# Special target dependencies
$(TARGET): | $(MOBIUS_LIB)
$(MATH_MODULE): | $(MOBIUS_LIB)
$(EMBEDDING_EXAMPLE): | $(MOBIUS_LIB)
$(SIMPLE_EMBEDDING_EXAMPLE): | $(MOBIUS_LIB)
$(GAME_ENGINE_EXAMPLE): | $(MOBIUS_LIB)
$(TEXT_PROCESSING_PLUGIN): | $(MOBIUS_LIB)