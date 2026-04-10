# Mobius Examples Directory

This directory contains comprehensive examples demonstrating how to use and extend the Mobius scripting language. Each subdirectory focuses on a specific aspect of Mobius integration and usage.

## 📁 Directory Structure

```
examples/
├── README.md                    # This file - examples overview
├── networking/                  # HTTP and WebSocket script servers
│   ├── http_server_utils.mob    # Shared HTTP server helpers
│   ├── http_hello_server.mob    # Minimal dynamic page example
│   ├── http_rest_site.mob       # REST API + server-rendered HTML app
│   ├── websocket_echo_server.mob # Plain WebSocket echo server
│   └── README.md                # Networking guide
├── simple_embedding/           # Minimal embedding example
│   ├── simple_embedding.c      # Basic C integration
│   └── README.md               # Getting started guide
├── embedding_example/          # Comprehensive embedding
│   ├── embedding_example.c     # Advanced C integration
│   └── README.md               # Full API demonstration
├── game_engine/               # Real-world application
│   ├── game_engine.c          # Game engine with scripting
│   ├── game_init.mob          # Game initialization script
│   └── README.md              # Application integration guide
├── text_processing_plugin/    # Custom extension development
│   ├── text_processing_plugin.c # Plugin implementation
│   └── README.md              # Plugin development guide
├── cpp_class_example/         # C++ object binding
│   ├── cpp_class_example.cpp  # C++ class integration
│   └── README.md              # Object-oriented binding
├── multi_environment_demo/    # Multiple interpreters
│   ├── multi_environment_demo.c # Multi-instance management
│   └── README.md              # Advanced deployment patterns
├── simple_userdata_test/      # Custom data types
│   ├── simple_userdata_test.c # Userdata type example
│   └── README.md              # Custom type integration
├── demo_scripts/              # Script demonstrations
│   ├── complete_graphics_demo.mob # Graphics programming
│   ├── entity_ai.mob          # AI behavior scripting
│   ├── multi_script_demo.mob  # Multi-file projects
│   ├── riscv_compilation_demo.mob # RISC-V optimization
│   ├── utils.mob              # Utility functions
│   └── README.md              # Script collection guide
└── performance_tests/         # Benchmarking and optimization
    ├── performance_test_current.mob # Current implementation
    ├── performance_test_refcount.mob # Reference counting
    ├── refcount_implementation_sample.c # C implementation
    └── README.md              # Performance testing guide
```

## 🎯 Quick Start Guide

### For Beginners
1. **Start with**: [Simple Embedding](simple_embedding/) - Basic integration
2. **Next try**: [Demo Scripts](demo_scripts/) - Language features
3. **Then explore**: [Game Engine](game_engine/) - Real application

### For Advanced Users
1. **Study**: [Comprehensive Embedding](embedding_example/) - Full API
2. **Implement**: [Text Processing Plugin](text_processing_plugin/) - Extensions
3. **Optimize**: [Performance Tests](performance_tests/) - Benchmarking

### For C++ Developers
1. **Begin with**: [C++ Class Example](cpp_class_example/) - Object binding
2. **Scale to**: [Multi Environment Demo](multi_environment_demo/) - Complex scenarios

## 📚 Example Categories

### 🔗 Embedding Examples
Learn how to integrate Mobius into your applications:

| Example | Complexity | Best For |
|---------|------------|----------|
| [Simple Embedding](simple_embedding/) | 🟢 Beginner | First integration, basic usage |
| [Comprehensive Embedding](embedding_example/) | 🟡 Intermediate | Production applications |
| [Game Engine](game_engine/) | 🟡 Intermediate | Real-world applications |
| [Multi Environment](multi_environment_demo/) | 🔴 Advanced | Complex deployments |

### 🧩 Extension Examples  
Create custom functionality and plugins:

| Example | Language | Best For |
|---------|----------|----------|
| [Text Processing Plugin](text_processing_plugin/) | C | String manipulation extensions |
| [C++ Classes](cpp_class_example/) | C++ | Object-oriented integration |
| [Userdata Types](simple_userdata_test/) | C | Custom data type creation |

### 📜 Script Examples
Demonstrate language features and best practices:

| Script | Focus Area | Features |
|--------|------------|----------|
| [Graphics Demo](demo_scripts/) | Graphics | 2D programming, animations |
| [Entity AI](demo_scripts/) | AI/Logic | State machines, behaviors |
| [Multi-Script](demo_scripts/) | Organization | Module systems, imports |
| [RISC-V Demo](demo_scripts/) | Performance | Low-level optimization |
| [Networking](networking/) | Servers | HTTP, REST, HTML, and WebSocket examples |

### ⚡ Performance Examples
Benchmark and optimize your usage:

| Test | Purpose | Measures |
|------|---------|----------|
| [Current Performance](performance_tests/) | Baseline | Speed, memory usage |
| [Reference Counting](performance_tests/) | Memory Management | GC performance |
| [C Implementation](performance_tests/) | Reference | Native comparison |

## 🚀 Building and Running

### Prerequisites
```bash
# Build Mobius library and examples
make clean && make all
```

### Running Examples
```bash
# C/C++ examples (compiled)
./bin/simple_embedding
./bin/embedding_example  
./bin/game_engine
./bin/cpp_class_example

# Script examples (interpreted)
./bin/mobius examples/demo_scripts/entity_ai.mob
./bin/mobius examples/performance_tests/performance_test_current.mob
./bin/mobius examples/networking/http_rest_site.mob

# Plugin examples (with dynamic loading)
LD_LIBRARY_PATH=./bin/modules ./bin/mobius
```

### Testing Examples
```bash
# Run all example tests
make test-examples

# Test specific categories
make test-embedding
make test-plugins
```

## 🎓 Learning Path

### Week 1: Fundamentals
- [ ] Build and run [Simple Embedding](simple_embedding/)
- [ ] Execute [Demo Scripts](demo_scripts/) 
- [ ] Read [Comprehensive Embedding](embedding_example/) guide

### Week 2: Integration
- [ ] Implement custom functions in [Embedding Example](embedding_example/)
- [ ] Study [Game Engine](game_engine/) architecture
- [ ] Try [C++ Class Example](cpp_class_example/) if using C++

### Week 3: Extensions
- [ ] Build [Text Processing Plugin](text_processing_plugin/)
- [ ] Create custom [Userdata Types](simple_userdata_test/)
- [ ] Explore [Multi Environment](multi_environment_demo/) patterns

### Week 4: Production
- [ ] Run [Performance Tests](performance_tests/)
- [ ] Optimize based on benchmarks
- [ ] Design your own application integration

## 🛠️ Development Tips

### Best Practices
1. **Always check return codes** from Mobius API functions
2. **Free allocated memory** (errors, values, states)
3. **Validate function arguments** in custom functions
4. **Handle errors gracefully** with proper cleanup
5. **Test with plugins disabled** for deployment flexibility

### Common Patterns
```c
// Safe state creation
MobiusState* state = mobius_new_state();
if (!state) return -1;

// Safe error handling  
if (mobius_exec_string(state, script) != MOBIUS_OK) {
    MobiusError* error = mobius_get_last_error(state);
    if (error) {
        fprintf(stderr, "Error: %s\n", error->message);
        mobius_free_error(error);
    }
}

// Safe cleanup
mobius_free_state(state);
```

### Debugging
```bash
# Enable debug output
export MOBIUS_DEBUG=1
./bin/mobius script.mob

# Verbose compilation
make CFLAGS="-DDEBUG -g3" examples

# Memory checking
valgrind --leak-check=full ./bin/embedding_example
```

## 📖 Documentation Links

- **[Language Reference](../docs/language_reference.md)** - Complete syntax guide
- **[Embedding Guide](../docs/embedding_guide.md)** - C API documentation  
- **[Examples Guide](../docs/examples_guide.md)** - Detailed example walkthroughs
- **[Design Documents](../docs/design/)** - Implementation details

## 🤝 Contributing Examples

### Adding New Examples
1. **Create directory** with descriptive name
2. **Add source files** with clear comments
3. **Write comprehensive README.md**
4. **Update this main README**
5. **Add build targets** to Makefile
6. **Test on clean system**

### Example Structure
```
your_example/
├── source_file.c           # Implementation
├── script_file.mob         # If applicable  
├── README.md              # Documentation
└── Makefile.snippet       # Build instructions
```

### Documentation Requirements
- **Clear purpose** and learning objectives
- **Complete build instructions**
- **Expected output** examples
- **Troubleshooting** common issues
- **Next steps** suggestions

These examples collectively demonstrate the full power and flexibility of the Mobius scripting language across diverse use cases and integration scenarios!
