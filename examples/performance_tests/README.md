# Performance Tests and Benchmarks

This directory contains performance testing scripts and reference counting implementation samples for benchmarking and optimization validation.

## Files

- `performance_test_current.mob` - Current interpreter performance benchmark
- `performance_test_refcount.mob` - Reference counting performance test
- `refcount_implementation_sample.c` - C implementation sample for reference counting

## Test Descriptions

### performance_test_current.mob
Benchmarks the current interpreter implementation across various operations.

**Tests Include:**
- Arithmetic operations
- String manipulation
- Array operations
- Table operations
- Function calls
- Memory allocation patterns

**Usage:**
```bash
./bin/mobius examples/performance_tests/performance_test_current.mob
```

### performance_test_refcount.mob
Specific benchmarks for reference counting memory management.

**Tests Include:**
- Object creation/destruction cycles
- Reference assignment patterns
- Circular reference handling
- Memory pressure scenarios
- Garbage collection triggers

**Usage:**
```bash
./bin/mobius examples/performance_tests/performance_test_refcount.mob
```

### refcount_implementation_sample.c
C code sample demonstrating reference counting implementation patterns.

**Contains:**
- Reference counting data structures
- Increment/decrement operations
- Cycle detection algorithms
- Memory management patterns

**Usage:**
```bash
gcc -o refcount_sample examples/performance_tests/refcount_implementation_sample.c
./refcount_sample
```

## Benchmark Categories

### 📊 Core Operations
- Basic arithmetic (+, -, *, /, %)
- Logical operations (&&, ||, !)
- Comparison operations (<, >, ==, !=)

### 🧮 Data Structures
- Array creation, access, modification
- Table insertion, lookup, deletion
- String concatenation and manipulation

### 🔄 Memory Management
- Object allocation and deallocation
- Reference counting overhead
- Garbage collection performance

### 📞 Function Performance
- Function call overhead
- Parameter passing costs
- Return value handling

## Running Benchmarks

### Individual Tests
```bash
# Current implementation benchmark
./bin/mobius examples/performance_tests/performance_test_current.mob

# Reference counting benchmark  
./bin/mobius examples/performance_tests/performance_test_refcount.mob
```

### Comparative Analysis
```bash
# Run both tests and compare
./bin/mobius examples/performance_tests/performance_test_current.mob > current.log
./bin/mobius examples/performance_tests/performance_test_refcount.mob > refcount.log
diff current.log refcount.log
```

### Performance Monitoring
```bash
# Monitor system resources during tests
time ./bin/mobius examples/performance_tests/performance_test_current.mob
```

## Metrics Measured

### 🕐 Timing Metrics
- **Operation Duration**: Time per individual operation
- **Throughput**: Operations per second
- **Total Runtime**: End-to-end execution time

### 💾 Memory Metrics  
- **Peak Memory Usage**: Maximum memory consumption
- **Allocation Rate**: Memory allocations per second
- **Garbage Collection**: GC frequency and duration

### 🔄 Reference Counting Metrics
- **Reference Operations**: Increment/decrement frequency
- **Cycle Detection**: Time spent finding cycles
- **Cleanup Efficiency**: Deallocation patterns

## Expected Output Format

```
=== Mobius Performance Test ===
Test: Arithmetic Operations
Iterations: 1000000
Duration: 0.245s
Ops/sec: 4,081,632

Test: String Operations  
Iterations: 100000
Duration: 0.156s
Ops/sec: 641,025

Test: Array Operations
Iterations: 50000
Duration: 0.089s
Ops/sec: 561,797

=== Memory Statistics ===
Peak Memory: 2.3 MB
Allocations: 150,000
Average Object Lifetime: 0.003s

=== Performance Summary ===
Overall Score: 8.5/10
Memory Efficiency: 9.2/10
CPU Efficiency: 7.8/10
```

## Optimization Insights

### 🚀 Performance Tips
- **Minimize allocations** in tight loops
- **Reuse objects** when possible
- **Cache frequently accessed** table values
- **Use appropriate data types** for the use case

### 📈 Benchmark Best Practices
- **Run multiple times** to get stable measurements
- **Use consistent test environments**
- **Measure both speed and memory**
- **Test realistic workloads**

## Integration with Development

### 🔧 Development Workflow
1. **Baseline**: Run tests before changes
2. **Develop**: Implement optimizations
3. **Measure**: Run tests after changes
4. **Compare**: Analyze performance delta
5. **Iterate**: Repeat until satisfied

### 📊 Regression Testing
```bash
# Automated performance regression detection
./run_performance_tests.sh --baseline=v1.0 --current=HEAD
```

### 🎯 Optimization Targets
- **50% faster** arithmetic operations
- **30% less** memory usage
- **2x improvement** in string operations
- **Zero overhead** reference counting

## Contributing

When adding performance tests:

1. **Focus on real workloads** not synthetic micro-benchmarks
2. **Test edge cases** like empty collections or extreme values
3. **Include memory tests** alongside speed tests
4. **Document expected ranges** for different hardware
5. **Use consistent measurement methodology**

These performance tests ensure that Mobius optimizations improve real-world performance while maintaining correctness and reliability!
