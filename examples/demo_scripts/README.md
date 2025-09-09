# Demo Scripts Collection

This directory contains various Mobius script files demonstrating different language features and use cases.

## Files

- `complete_graphics_demo.mob` - Comprehensive graphics programming demonstration
- `entity_ai.mob` - Entity AI behavior scripting example
- `multi_script_demo.mob` - Multi-file script loading and interaction
- `riscv_compilation_demo.mob` - RISC-V compilation target demonstration
- `utils.mob` - Utility functions and helper library

## Script Descriptions

### complete_graphics_demo.mob
A comprehensive demonstration of graphics programming capabilities in Mobius.

**Features:**
- 2D graphics primitives
- Animation and movement
- Event handling
- Collision detection
- Scene management

**Usage:**
```bash
./bin/mobius examples/demo_scripts/complete_graphics_demo.mob
```

### entity_ai.mob
Demonstrates AI behavior scripting for game entities or simulation objects.

**Features:**
- State machines
- Behavior trees
- Decision making
- Path finding algorithms
- Entity interactions

**Usage:**
```bash
./bin/mobius examples/demo_scripts/entity_ai.mob
```

### multi_script_demo.mob
Shows how to structure and load multiple Mobius script files.

**Features:**
- Module loading
- Cross-script function calls
- Namespace management
- Code organization patterns

**Usage:**
```bash
./bin/mobius examples/demo_scripts/multi_script_demo.mob
```

### riscv_compilation_demo.mob
Demonstrates features relevant to RISC-V compilation and optimization.

**Features:**
- Performance-critical code patterns
- Low-level operations
- Optimization hints
- Hardware-specific features

**Usage:**
```bash
./bin/mobius examples/demo_scripts/riscv_compilation_demo.mob
```

### utils.mob
A utility library with common helper functions.

**Contains:**
- String manipulation utilities
- Math helper functions
- Data structure operations
- Common algorithms

**Usage:**
```javascript
// In other scripts
load("examples/demo_scripts/utils.mob");
var result = utility_function(data);
```

## Running the Demos

### Prerequisites
- Mobius interpreter built (`make`)
- Any required plugins loaded

### Individual Scripts
```bash
# Run a specific demo
./bin/mobius examples/demo_scripts/[script_name].mob
```

### Interactive Mode
```bash
# Load and test in REPL
./bin/mobius
> load("examples/demo_scripts/utils.mob");
> help();
```

## Educational Value

These scripts serve as:

1. **Language Learning**: Examples of Mobius syntax and idioms
2. **Best Practices**: Demonstrated patterns for common tasks
3. **Feature Showcase**: Comprehensive coverage of language capabilities
4. **Template Code**: Starting points for your own scripts
5. **Testing**: Validation of interpreter functionality

## Script Categories

### 🎮 Game Development
- `entity_ai.mob` - AI and behavior
- `complete_graphics_demo.mob` - Graphics and rendering

### 🔧 System Programming  
- `riscv_compilation_demo.mob` - Low-level optimization
- `utils.mob` - System utilities

### 📚 Code Organization
- `multi_script_demo.mob` - Project structure
- `utils.mob` - Library patterns

## Customization

These scripts can be modified to:

- **Test new features** in development
- **Benchmark performance** across different scenarios
- **Prototype applications** before full implementation
- **Learn language features** through experimentation

## Integration

Use these scripts as:

- **Components** in larger applications
- **Templates** for new projects  
- **Test cases** for language validation
- **Documentation** examples

Each script is self-contained but can also work as part of larger applications demonstrating Mobius's flexibility for various programming domains!
