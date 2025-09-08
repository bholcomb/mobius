#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include "../src/mobius/execution.h"
#include <stdbool.h>
#include <stddef.h>

// Test case structure
typedef struct {
    const char* name;
    const char* source_code;
    const char* expected_description;  // Human-readable description of expected result
    bool should_succeed;               // Whether execution should succeed
} TestCase;

// Test result for a single test case
typedef struct {
    const char* test_name;
    bool ast_success;
    bool bytecode_success;
    bool results_match;               // Do AST and bytecode produce same result?
    double ast_time_ms;
    double bytecode_time_ms;
    double speedup_factor;            // bytecode_time / ast_time (< 1.0 means bytecode faster)
    size_t ast_instructions;
    size_t bytecode_instructions;
    char* error_message;              // Error details if test failed
} TestResult;

// Test suite statistics
typedef struct {
    size_t total_tests;
    size_t passed_tests;
    size_t failed_tests;
    double total_ast_time_ms;
    double total_bytecode_time_ms;
    double average_speedup;
    size_t validation_failures;      // Tests where AST != bytecode result
} TestSuiteStats;

// =============================================================================
// TEST FRAMEWORK API
// =============================================================================

// Run a single test case and return detailed results
TestResult run_test_case(const TestCase* test);

// Run a collection of test cases
TestSuiteStats run_test_suite(const TestCase* tests, size_t count, bool verbose);

// Free test result resources
void free_test_result(TestResult* result);

// Print test result in detailed format
void print_test_result(const TestResult* result);

// Print test suite summary
void print_test_suite_summary(const TestSuiteStats* stats);

// Performance benchmark - run same test multiple times
typedef struct {
    double min_time_ms;
    double max_time_ms;
    double avg_time_ms;
    double std_dev_ms;
    size_t iterations;
} BenchmarkResult;

BenchmarkResult benchmark_execution(ExecutionContext* ctx, const char* source, size_t iterations);

#endif // TEST_FRAMEWORK_H
