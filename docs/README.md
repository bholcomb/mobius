# Mobius Documentation

Welcome to the Mobius Scripting Language documentation! This directory contains comprehensive guides and references for using Mobius.

## 📚 Documentation Index

### For Users and Script Writers

#### 🎯 [Language Reference](language_reference.md)
**The complete guide to Mobius syntax and built-in libraries**
- Language syntax and grammar
- All data types and operators
- Control flow constructs
- Complete standard library reference
- Math extension library documentation
- Text processing extension reference
- Error handling guide
- Complete examples and best practices

*Start here if you want to learn how to write Mobius scripts*

### For Developers and System Integrators

#### 🔗 [Embedding Guide](embedding_guide.md)
**Complete C API for embedding Mobius in applications**
- MobiusState management and initialization
- Bidirectional value exchange (C ↔ Mobius)
- Custom C function registration
- Error handling in embedded scenarios
- Memory management best practices
- Multiple interpreter instances
- Security and sandboxing considerations

*Use this when integrating Mobius into your C application*

#### 🎮 [Examples Guide](examples_guide.md)
**Real-world examples of embedding and plugin development**
- Game Engine Example - practical embedding demonstration
- Text Processing Plugin - complete plugin development
- Build system integration
- Advanced embedding patterns
- Security considerations
- Performance optimization techniques

*Study these examples to understand real-world usage patterns*

### For Language Developers and Contributors

#### 🔧 [Design Documents](design/)
**Internal design specifications and implementation plans**
- Bytecode system architecture and implementation
- Memory management and reference counting design
- Language feature specifications (enums, types, etc.)
- RISC-V backend and compilation strategies
- Implementation roadmaps and risk assessments

*Use these documents if you're contributing to the Mobius language implementation*

## 🚀 Quick Start

### For Script Writers
1. Read the [Language Reference](language_reference.md) syntax section
2. Try the examples in the built-in REPL: `./bin/mobius`
3. Write your first script and run it: `./bin/mobius your_script.mob`

### For C Developers
1. Check the [Embedding Guide](embedding_guide.md) for API overview
2. Study the [simple embedding example](../examples/simple_embedding.c)
3. Build and run: `make examples && ./bin/simple_embedding`

### For Plugin Developers
1. Read the plugin section in [Examples Guide](examples_guide.md)
2. Use the [text processing plugin](../examples/text_processing_plugin.c) as template
3. Build your plugin: `gcc -shared -fPIC -o my_plugin.so my_plugin.c -lmobius`

## 📖 What's Available

### Core Language Features
- ✅ **Dynamic typing** with runtime type checking
- ✅ **Multiple numeric types** (8/16/32/64-bit integers, 64-bit floats)
- ✅ **First-class functions** with closures and lexical scoping
- ✅ **Control flow** (if/else, while, for loops)
- ✅ **String manipulation** with Unicode support
- ✅ **Error handling** with detailed diagnostics

### Built-in Standard Library (22 functions)
- ✅ **Core functions**: `print()`, `typeof()`, type conversions
- ✅ **Math functions**: `abs()`, `min()`, `max()`, `pow()`, `sqrt()`, etc.
- ✅ **String functions**: `len()`, `substr()`, `concat()`, `upper()`, `lower()`, etc.
- ✅ **Utility functions**: `random()`, `time()`, `clock()`

### Available Extensions
- ✅ **Math Extension** (18 functions): trigonometry, logarithms, advanced math
- ✅ **Text Processing** (9 functions): word counting, text analysis, formatting

### Development Tools
- ✅ **Interactive REPL** for testing and experimentation
- ✅ **File execution** for running script files
- ✅ **Comprehensive error reporting** with suggestions
- ✅ **Plugin system** for custom extensions

## 🛠️ System Requirements

### Runtime Requirements
- Linux x86_64 (primary platform)
- GCC-compatible C library
- Dynamic linking support (`libdl`)
- Math library (`libm`)

### Development Requirements
- GCC with C99 support
- Make build system
- POSIX-compatible development environment

## 📁 File Organization

```
docs/
├── README.md              # This file - documentation index
├── language_reference.md  # Complete language and library reference
├── embedding_guide.md     # C API and embedding documentation
├── examples_guide.md      # Real-world usage examples
├── ENUM_REFERENCE.md      # Enum system reference
└── design/                # Internal design documents and specifications

examples/
├── simple_embedding.c     # Basic embedding example
├── embedding_example.c    # Comprehensive embedding demo
├── game_engine.c          # Game engine with scripting
└── text_processing_plugin.c # Plugin development example

src/
├── mobius/               # Core interpreter library
├── modules/              # Extension modules
└── main.c               # Standalone interpreter

bin/
├── mobius               # Standalone interpreter
└── modules/             # Dynamically loaded extensions
    ├── math.so         # Math extension
    └── text_processing.so # Text processing extension
```

## 🎯 Use Cases

### Mobius is Excellent For:
- **Application scripting** - embed in games, tools, applications
- **Configuration files** - dynamic, programmable configuration
- **Data processing** - text analysis, mathematical calculations
- **Automation scripts** - task automation with C integration
- **Plugin systems** - extensible application architectures
- **Learning** - simple syntax for teaching programming concepts

### Architecture Benefits:
- **Lightweight** - minimal memory footprint
- **Embeddable** - clean C API for integration
- **Extensible** - plugin system for domain-specific functionality
- **Safe** - comprehensive error handling prevents crashes
- **Fast** - efficient interpretation and execution

## 🔄 Version Information

- **Language Version**: Mobius 0.1.0
- **API Version**: 1
- **Documentation Version**: 1.0
- **Last Updated**: December 2024

## 📞 Getting Help

1. **Language Questions**: Check [Language Reference](language_reference.md)
2. **Embedding Issues**: See [Embedding Guide](embedding_guide.md)
3. **Examples**: Study [Examples Guide](examples_guide.md)
4. **Build Problems**: Check the main project README.md

## 🚀 Next Steps

1. **Try the REPL**: `./bin/mobius` - Interactive exploration
2. **Run examples**: `make examples && make test-examples` 
3. **Write a script**: Create a `.mob` file and execute it
4. **Embed in C**: Start with the simple embedding example
5. **Create a plugin**: Use the text processing plugin as template

Welcome to the Mobius ecosystem! 🌟

