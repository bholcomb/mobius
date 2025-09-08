#define _GNU_SOURCE

#include "test_framework.h"
#include "test_cases.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

typedef struct {
    bool verbose;
    bool performance_only;
    bool validation_only;
    bool run_benchmarks;
    const char* suite_filter;
    size_t benchmark_iterations;
} RunnerOptions;

static void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("\nOptions:\n");
    printf("  -v, --verbose         Verbose output with detailed results\n");
    printf("  -p, --performance     Run only performance tests\n");
    printf("  -V, --validation      Run only validation tests (no performance focus)\n");
    printf("  -b, --benchmark       Run benchmarks with multiple iterations\n");
    printf("  -i, --iterations N    Number of benchmark iterations (default: 100)\n");
    printf("  -s, --suite NAME      Run only specified test suite\n");
    printf("  -h, --help           Show this help message\n");
    printf("\nTest Suites:\n");
    for (size_t i = 0; i < total_test_suites; i++) {
        printf("  %s (%zu tests)\n", all_test_names[i], *all_test_counts[i]);
    }
    printf("\nTotal: %zu tests across %zu suites\n", get_total_test_count(), total_test_suites);
}

static void run_benchmark_suite(void) {
    printf("\n=== PERFORMANCE BENCHMARKS ===\n");
    
    ExecutionContext* ast_ctx = execution_context_create(EXEC_BACKEND_AST);
    ExecutionContext* bytecode_ctx = execution_context_create(EXEC_BACKEND_BYTECODE);
    
    if (!ast_ctx || !bytecode_ctx) {
        printf("Failed to create execution contexts for benchmarks\n");
        return;
    }
    
    // Benchmark representative test cases
    const char* benchmark_cases[][2] = {
        {"Simple Loop", "var sum = 0; var i = 0; while (i < 100) { sum = sum + i; i = i + 1; } sum;"},
        {"Array Operations", "var arr = [1, 2, 3, 4, 5]; var sum = 0; var i = 0; while (i < 5) { sum = sum + arr[i]; i = i + 1; } sum;"},
        {"Table Lookup", "var table = {a: 1, b: 2, c: 3}; table[\"a\"] + table[\"b\"] + table[\"c\"];"},
        {"Complex Expression", "var x = 10; (x + 5) * (x - 3) / 2;"}
    };
    
    size_t num_benchmarks = sizeof(benchmark_cases) / sizeof(benchmark_cases[0]);
    
    for (size_t i = 0; i < num_benchmarks; i++) {
        printf("\nBenchmarking: %s\n", benchmark_cases[i][0]);
        
        BenchmarkResult ast_bench = benchmark_execution(ast_ctx, benchmark_cases[i][1], 1000);
        BenchmarkResult bytecode_bench = benchmark_execution(bytecode_ctx, benchmark_cases[i][1], 1000);
        
        printf("  AST:      %.3f ± %.3f ms (%.3f - %.3f ms)\n",
               ast_bench.avg_time_ms, ast_bench.std_dev_ms,
               ast_bench.min_time_ms, ast_bench.max_time_ms);
        
        printf("  Bytecode: %.3f ± %.3f ms (%.3f - %.3f ms)\n",
               bytecode_bench.avg_time_ms, bytecode_bench.std_dev_ms,
               bytecode_bench.min_time_ms, bytecode_bench.max_time_ms);
        
        if (bytecode_bench.avg_time_ms > 0) {
            double speedup = ast_bench.avg_time_ms / bytecode_bench.avg_time_ms;
            const char* color = speedup > 1.0 ? "\033[32m" : "\033[33m";  // Green if faster, yellow if slower
            printf("  Speedup:  %s%.2fx\033[0m\n", color, speedup);
        }
    }
    
    execution_context_free(ast_ctx);
    execution_context_free(bytecode_ctx);
}

static int find_suite_index(const char* suite_name) {
    for (size_t i = 0; i < total_test_suites; i++) {
        if (strcasecmp(all_test_names[i], suite_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int main(int argc, char* argv[]) {
    RunnerOptions options = {
        .verbose = false,
        .performance_only = false,
        .validation_only = false,
        .run_benchmarks = false,
        .suite_filter = NULL,
        .benchmark_iterations = 100
    };
    
    // Parse command line options
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"performance", no_argument, 0, 'p'},
        {"validation", no_argument, 0, 'V'},
        {"benchmark", no_argument, 0, 'b'},
        {"iterations", required_argument, 0, 'i'},
        {"suite", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "vpVbi:s:h", long_options, NULL)) != -1) {
        switch (c) {
            case 'v':
                options.verbose = true;
                break;
            case 'p':
                options.performance_only = true;
                break;
            case 'V':
                options.validation_only = true;
                break;
            case 'b':
                options.run_benchmarks = true;
                break;
            case 'i':
                options.benchmark_iterations = (size_t)atoi(optarg);
                break;
            case 's':
                options.suite_filter = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                print_usage(argv[0]);
                return 1;
        }
    }
    
    printf("🎯 Mobius Dual-Backend Test Suite 🎯\n");
    printf("Testing AST Tree-Walker vs Bytecode VM\n");
    printf("Total test cases available: %zu\n\n", get_total_test_count());
    
    TestSuiteStats overall_stats = {0};
    
    // Run test suites
    if (options.suite_filter) {
        int suite_index = find_suite_index(options.suite_filter);
        if (suite_index < 0) {
            printf("Error: Unknown test suite '%s'\n", options.suite_filter);
            print_usage(argv[0]);
            return 1;
        }
        
        printf("Running suite: %s\n", all_test_names[suite_index]);
        overall_stats = run_test_suite(all_test_suites[suite_index], 
                                     *all_test_counts[suite_index], 
                                     options.verbose);
    } else {
        // Run all suites or filtered suites
        for (size_t i = 0; i < total_test_suites; i++) {
            if (options.performance_only && strcmp(all_test_names[i], "Performance Tests") != 0) {
                continue;
            }
            if (options.validation_only && strcmp(all_test_names[i], "Performance Tests") == 0) {
                continue;
            }
            
            printf("\n--- %s ---\n", all_test_names[i]);
            TestSuiteStats suite_stats = run_test_suite(all_test_suites[i], 
                                                       *all_test_counts[i], 
                                                       options.verbose);
            
            // Aggregate stats
            overall_stats.total_tests += suite_stats.total_tests;
            overall_stats.passed_tests += suite_stats.passed_tests;
            overall_stats.failed_tests += suite_stats.failed_tests;
            overall_stats.total_ast_time_ms += suite_stats.total_ast_time_ms;
            overall_stats.total_bytecode_time_ms += suite_stats.total_bytecode_time_ms;
            overall_stats.validation_failures += suite_stats.validation_failures;
        }
        
        if (overall_stats.total_tests > 0) {
            overall_stats.average_speedup = overall_stats.total_ast_time_ms / 
                                          overall_stats.total_bytecode_time_ms;
        }
    }
    
    // Run benchmarks if requested
    if (options.run_benchmarks) {
        run_benchmark_suite();
    }
    
    // Print final summary
    printf("\n🏁 FINAL SUMMARY 🏁\n");
    print_test_suite_summary(&overall_stats);
    
    // Determine exit code
    if (overall_stats.failed_tests > 0 || overall_stats.validation_failures > 0) {
        printf("\n❌ Some tests failed or produced inconsistent results\n");
        return 1;
    } else {
        printf("\n✅ All tests passed with consistent results!\n");
        return 0;
    }
}
