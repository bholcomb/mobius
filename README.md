# Mobius Scripting Language

A simple scripting language interpreter written in C.

## Project Structure

```
mobius/
├── src/        # Source code files
├── bin/        # Compiled binaries (created during build)
├── data/       # Data files and test scripts
├── doc/        # Documentation
├── Makefile    # Build configuration
└── README.md   # This file
```

## Building

The project uses a simple Makefile with system GCC:

```bash
# Build the interpreter
make

# Build and run
make run

# Clean build artifacts
make clean

# Build with debug symbols
make debug

# Build optimized release
make release

# See all available targets
make help
```

## Requirements

- GCC compiler (system installation)
- Make utility
- Linux/Unix environment

## Getting Started

1. Build the project: `make`
2. Run the interpreter: `./bin/mobius`
3. Start implementing your language features in `src/`

## Development

- Add new `.c` files to the `src/` directory
- The Makefile will automatically include them in the build
- Use `make debug` for development builds with debug symbols
- Use `make release` for optimized production builds
