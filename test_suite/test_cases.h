#ifndef TEST_CASES_H
#define TEST_CASES_H

#include "test_framework.h"

// =============================================================================
// COMPREHENSIVE TEST CASES
// =============================================================================

// Basic arithmetic and literals
extern const TestCase basic_tests[];
extern const size_t basic_tests_count;

// Variables and expressions
extern const TestCase variable_tests[];
extern const size_t variable_tests_count;

// Control flow (if, while, for)
extern const TestCase control_flow_tests[];
extern const size_t control_flow_tests_count;

// Arrays and tables
extern const TestCase data_structure_tests[];
extern const size_t data_structure_tests_count;

// Functions and calls
extern const TestCase function_tests[];
extern const size_t function_tests_count;

// Error handling
extern const TestCase error_tests[];
extern const size_t error_tests_count;

// Performance-focused tests
extern const TestCase performance_tests[];
extern const size_t performance_tests_count;

// Complex integration tests
extern const TestCase integration_tests[];
extern const size_t integration_tests_count;

// All test cases combined
extern const TestCase* all_test_suites[];
extern const size_t* all_test_counts[];
extern const char* all_test_names[];
extern const size_t total_test_suites;

// Helper function to get total test count across all suites
size_t get_total_test_count(void);

#endif // TEST_CASES_H
