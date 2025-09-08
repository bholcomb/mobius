#define _GNU_SOURCE

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

// ANSI color codes for output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

TestResult run_test_case(const TestCase* test) {
    TestResult result = {0};
    result.test_name = test->name;
    result.error_message = NULL;
    
    // Create execution contexts for both backends
    ExecutionContext* ast_ctx = execution_context_create(EXEC_BACKEND_AST);
    ExecutionContext* bytecode_ctx = execution_context_create(EXEC_BACKEND_BYTECODE);
    
    if (!ast_ctx || !bytecode_ctx) {
        result.error_message = strdup("Failed to create execution contexts");
        if (ast_ctx) execution_context_free(ast_ctx);
        if (bytecode_ctx) execution_context_free(bytecode_ctx);
        return result;
    }
    
    // Execute with AST interpreter
    ExecutionResult ast_result = execute_source(ast_ctx, test->source_code);
    result.ast_success = ast_result.success;
    result.ast_time_ms = ast_result.execution_time_ms;
    result.ast_instructions = ast_result.instructions_executed;
    
    // Execute with bytecode VM
    ExecutionResult bytecode_result = execute_source(bytecode_ctx, test->source_code);
    result.bytecode_success = bytecode_result.success;
    result.bytecode_time_ms = bytecode_result.execution_time_ms;
    result.bytecode_instructions = bytecode_result.instructions_executed;
    
    // Calculate speedup factor
    if (result.ast_time_ms > 0 && result.bytecode_time_ms > 0) {
        result.speedup_factor = result.ast_time_ms / result.bytecode_time_ms;
    } else {
        result.speedup_factor = 1.0;
    }
    
    // Check if results match
    result.results_match = execution_results_equivalent(&ast_result, &bytecode_result);
    
    // Check if test passed overall
    bool test_passed = true;
    char error_buffer[512] = {0};
    
    if (test->should_succeed) {
        if (!result.ast_success || !result.bytecode_success) {
            test_passed = false;
            snprintf(error_buffer, sizeof(error_buffer), 
                    "Expected success but got failure (AST: %s, Bytecode: %s)",
                    result.ast_success ? "OK" : "FAIL",
                    result.bytecode_success ? "OK" : "FAIL");
        }
    } else {
        if (result.ast_success || result.bytecode_success) {
            test_passed = false;
            snprintf(error_buffer, sizeof(error_buffer),
                    "Expected failure but got success (AST: %s, Bytecode: %s)",
                    result.ast_success ? "OK" : "FAIL", 
                    result.bytecode_success ? "OK" : "FAIL");
        }
    }
    
    if (!result.results_match && result.ast_success && result.bytecode_success) {
        test_passed = false;
        snprintf(error_buffer, sizeof(error_buffer),
                "AST and bytecode produced different results");
    }
    
    if (!test_passed && strlen(error_buffer) > 0) {
        result.error_message = strdup(error_buffer);
    }
    
    // Cleanup
    free_execution_result(&ast_result);
    free_execution_result(&bytecode_result);
    execution_context_free(ast_ctx);
    execution_context_free(bytecode_ctx);
    
    return result;
}

TestSuiteStats run_test_suite(const TestCase* tests, size_t count, bool verbose) {
    TestSuiteStats stats = {0};
    stats.total_tests = count;
    
    printf("%s=== MOBIUS EXECUTION TEST SUITE ===%s\n", COLOR_BOLD COLOR_CYAN, COLOR_RESET);
    printf("Running %zu tests...\n\n", count);
    
    double total_speedup = 0.0;
    size_t speedup_count = 0;
    
    for (size_t i = 0; i < count; i++) {
        TestResult result = run_test_case(&tests[i]);
        
        bool test_passed = (result.error_message == NULL);
        if (test_passed) {
            stats.passed_tests++;
        } else {
            stats.failed_tests++;
        }
        
        if (!result.results_match) {
            stats.validation_failures++;
        }
        
        stats.total_ast_time_ms += result.ast_time_ms;
        stats.total_bytecode_time_ms += result.bytecode_time_ms;
        
        if (result.speedup_factor > 0) {
            total_speedup += result.speedup_factor;
            speedup_count++;
        }
        
        // Print test result
        const char* status_color = test_passed ? COLOR_GREEN : COLOR_RED;
        const char* status_text = test_passed ? "PASS" : "FAIL";
        
        printf("[%s%s%s] %s", status_color, status_text, COLOR_RESET, tests[i].name);
        
        if (verbose || !test_passed) {
            printf("\n");
            print_test_result(&result);
        } else {
            printf(" (%.3fms AST, %.3fms bytecode, %.2fx speedup)\n",
                   result.ast_time_ms, result.bytecode_time_ms, result.speedup_factor);
        }
        
        free_test_result(&result);
    }
    
    if (speedup_count > 0) {
        stats.average_speedup = total_speedup / speedup_count;
    }
    
    printf("\n");
    print_test_suite_summary(&stats);
    
    return stats;
}

void free_test_result(TestResult* result) {
    if (!result) return;
    
    if (result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
}

void print_test_result(const TestResult* result) {
    printf("  Test: %s\n", result->test_name);
    printf("  AST Result: %s%s%s (%.3fms, %zu ops)\n",
           result->ast_success ? COLOR_GREEN : COLOR_RED,
           result->ast_success ? "SUCCESS" : "FAILURE",
           COLOR_RESET,
           result->ast_time_ms,
           result->ast_instructions);
    
    printf("  Bytecode Result: %s%s%s (%.3fms, %zu ops)\n", 
           result->bytecode_success ? COLOR_GREEN : COLOR_RED,
           result->bytecode_success ? "SUCCESS" : "FAILURE",
           COLOR_RESET,
           result->bytecode_time_ms,
           result->bytecode_instructions);
    
    printf("  Results Match: %s%s%s\n",
           result->results_match ? COLOR_GREEN : COLOR_RED,
           result->results_match ? "YES" : "NO",
           COLOR_RESET);
    
    if (result->speedup_factor > 0) {
        const char* speedup_color = result->speedup_factor > 1.0 ? COLOR_GREEN : COLOR_YELLOW;
        printf("  Performance: %s%.2fx speedup%s\n",
               speedup_color, result->speedup_factor, COLOR_RESET);
    }
    
    if (result->error_message) {
        printf("  %sError: %s%s\n", COLOR_RED, result->error_message, COLOR_RESET);
    }
    
    printf("\n");
}

void print_test_suite_summary(const TestSuiteStats* stats) {
    printf("%s=== TEST SUITE SUMMARY ===%s\n", COLOR_BOLD COLOR_CYAN, COLOR_RESET);
    
    printf("Tests: %zu total, ", stats->total_tests);
    printf("%s%zu passed%s, ", COLOR_GREEN, stats->passed_tests, COLOR_RESET);
    printf("%s%zu failed%s\n", COLOR_RED, stats->failed_tests, COLOR_RESET);
    
    if (stats->validation_failures > 0) {
        printf("%sValidation: %zu mismatches between AST and bytecode%s\n",
               COLOR_YELLOW, stats->validation_failures, COLOR_RESET);
    } else {
        printf("%sValidation: All results match between backends%s\n", COLOR_GREEN, COLOR_RESET);
    }
    
    printf("Performance:\n");
    printf("  Total AST time: %.3fms\n", stats->total_ast_time_ms);
    printf("  Total Bytecode time: %.3fms\n", stats->total_bytecode_time_ms);
    
    if (stats->average_speedup > 0) {
        const char* perf_color = stats->average_speedup > 1.0 ? COLOR_GREEN : COLOR_YELLOW;
        printf("  Average speedup: %s%.2fx%s\n", perf_color, stats->average_speedup, COLOR_RESET);
    }
    
    double success_rate = stats->total_tests > 0 ? 
        (double)stats->passed_tests / stats->total_tests * 100.0 : 0.0;
    
    const char* success_color = success_rate >= 95.0 ? COLOR_GREEN : 
                               success_rate >= 80.0 ? COLOR_YELLOW : COLOR_RED;
    
    printf("Success Rate: %s%.1f%%%s\n", success_color, success_rate, COLOR_RESET);
}

BenchmarkResult benchmark_execution(ExecutionContext* ctx, const char* source, size_t iterations) {
    BenchmarkResult benchmark = {0};
    benchmark.iterations = iterations;
    
    double* times = malloc(iterations * sizeof(double));
    if (!times) return benchmark;
    
    double total_time = 0.0;
    benchmark.min_time_ms = INFINITY;
    benchmark.max_time_ms = 0.0;
    
    // Run benchmark iterations
    for (size_t i = 0; i < iterations; i++) {
        ExecutionResult result = execute_source(ctx, source);
        
        times[i] = result.execution_time_ms;
        total_time += times[i];
        
        if (times[i] < benchmark.min_time_ms) benchmark.min_time_ms = times[i];
        if (times[i] > benchmark.max_time_ms) benchmark.max_time_ms = times[i];
        
        free_execution_result(&result);
    }
    
    benchmark.avg_time_ms = total_time / iterations;
    
    // Calculate standard deviation
    double variance = 0.0;
    for (size_t i = 0; i < iterations; i++) {
        double diff = times[i] - benchmark.avg_time_ms;
        variance += diff * diff;
    }
    benchmark.std_dev_ms = sqrt(variance / iterations);
    
    free(times);
    return benchmark;
}
