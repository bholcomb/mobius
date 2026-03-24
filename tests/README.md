# Mobius Test Suite

This directory contains comprehensive tests for the Mobius scripting language interpreter.

## Directory Structure

```
tests/
├── basic/          # Basic functionality tests (hello world, stdlib, functions)
├── types/          # Type system tests (annotations, conversions, precision)
├── tables/         # Table and metatable functionality tests
├── errors/         # Error handling and validation tests
├── integration/    # Complex integration and feature interaction tests
└── README.md       # This file
```

## Running Tests

### Quick Start

```bash
# Run all tests
make test

# Or directly:
./run_tests.sh
```

### Advanced Usage

```bash
# Run tests for a specific category
./run_tests.sh --category basic
./run_tests.sh --category types
./run_tests.sh --category tables

# Run with verbose output
./run_tests.sh --verbose

# Show help
./run_tests.sh --help
```

### Makefile Targets

```bash
make test                           # Run complete test suite
make test-category CATEGORY=basic   # Run specific category
make test-legacy                    # Run old single-file test
```

## Test Categories

### Basic Tests (`tests/basic/`)
- **hello.mob**: Basic Hello World and language features
- **test_functions.mob**: Function definitions and calls
- **test_stdlib.mob**: Standard library functions
- **test_arithmetic_metamethods.mob**: Basic arithmetic operations

### Type System Tests (`tests/types/`)
- **test_type_annotations.mob**: Type annotation syntax
- **test_type_checking.mob**: Type validation and conversion
- **test_simplified_integers.mob**: Integer parsing with simplified token structure
- **test_float64_keyword.mob**: Double precision float keyword
- **test_strict_*.mob**: Strict type checking modes

### Table Tests (`tests/tables/`)
- **simple_table.mob**: Basic table operations
- **comprehensive_tables.mob**: Advanced table features
- **test_*_metamethods.mob**: Metatable functionality
- **oop_examples.mob**: Object-oriented programming patterns
- **test_pairs.mob**: Table iteration

### Error Tests (`tests/errors/`)
- **simple_errors.mob**: Basic error reporting
- **test_errors.mob**: Advanced error handling

### Integration Tests (`tests/integration/`)
- **test_plugins.mob**: Plugin system functionality
- **test_userdata.mob**: C++ integration via userdata
- **test_types_comprehensive.mob**: Complete type system validation

## Writing Tests

### Test File Naming
- Use descriptive names: `test_feature_name.mob`
- Group related tests in appropriate subdirectories
- Use prefixes for organization: `test_`, `simple_`, `comprehensive_`

### Test Validation Annotations

Add validation comments to test files for automatic checking:

```mobius
// Test file example
// REQUIRE: Expected output text that must appear
// REQUIRE: Another required output pattern

print("Expected output text");
```

### Test Expectations

The test runner automatically validates:
- **Exit codes**: Tests should exit with code 0 (success)
- **Error output**: Error tests should produce errors, others should not
- **Required patterns**: Tests with `// REQUIRE:` comments must output those patterns
- **Category-specific validation**: 
  - Basic tests: Should contain "Hello" or basic output
  - Math tests: Should show arithmetic operations
  - Table tests: Should demonstrate table operations
  - Type tests: Should show type operations

### Test Structure

```mobius
// Test description and validation annotations
// REQUIRE: test completed

print("=== Test Name ===");

// Test implementation
var x = 42;
print("Result:", x);

print("test completed");
```

## Test Runner Features

### Automatic Validation
- ✅ **Syntax validation**: Detects parse errors
- ✅ **Runtime validation**: Catches runtime errors and crashes
- ✅ **Output validation**: Checks for required output patterns
- ✅ **Timeout protection**: Prevents hanging tests (30s timeout)
- ✅ **Category-specific checks**: Validates test-appropriate behavior

### Reporting
- ✅ **Colorized output**: Green (pass), red (fail), yellow (skip)
- ✅ **Detailed summaries**: Pass/fail/skip counts
- ✅ **Error diagnostics**: Specific failure reasons
- ✅ **Debug information**: Temporary output files for investigation

### Performance
- ✅ **Parallel execution**: Fast test execution
- ✅ **Category filtering**: Run subset of tests
- ✅ **Early termination**: Stop on critical failures

## Debugging Failed Tests

When tests fail, the runner provides debug information:

```bash
# Run specific test manually
./bin/mobius tests/category/failing_test.mob

# Check temporary output files
ls /tmp/mobius_test_*/

# Run with verbose output
./run_tests.sh --verbose
```

## Contributing Tests

1. **Add new test files** to appropriate category directories
2. **Include validation annotations** with `// REQUIRE:` comments  
3. **Test your changes** with `./run_tests.sh --category your_category`
4. **Update this README** if adding new test categories

## Test Maintenance

- **Keep tests focused**: One feature per test file when possible
- **Use clear naming**: Test names should describe what they validate
- **Add documentation**: Include comments explaining complex test logic
- **Validate regularly**: Run tests after any language changes
- **Update annotations**: Ensure `// REQUIRE:` patterns match actual output

This test suite ensures the Mobius language interpreter maintains quality and catches regressions during development.
