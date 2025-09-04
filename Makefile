# Mobius Scripting Language Interpreter
# Simple Makefile for building with system GCC

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g -O0
LDFLAGS = -lm

# Directories
SRC_DIR = src
BIN_DIR = bin
DATA_DIR = data
DOC_DIR = doc

# Target executable
TARGET = $(BIN_DIR)/mobius

# Source files (automatically find all .c files in src/)
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

# Default target
all: $(TARGET)

# Create directories if they don't exist
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build the main executable
$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Compile object files
$(BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BIN_DIR)/*

# Install (optional - copies to /usr/local/bin)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	rm -f /usr/local/bin/mobius

# Run the interpreter
run: $(TARGET)
	./$(TARGET)

# Debug build (with debug symbols and no optimization)
debug: CFLAGS += -DDEBUG -g3 -O0
debug: $(TARGET)

# Release build (optimized)
release: CFLAGS += -DNDEBUG -O2
release: clean $(TARGET)

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the interpreter (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build optimized release version"
	@echo "  run       - Build and run the interpreter"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  uninstall - Remove from /usr/local/bin"
	@echo "  help      - Show this help message"

# Declare phony targets
.PHONY: all clean install uninstall run debug release help
