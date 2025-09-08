#include "test_cases.h"

// =============================================================================
// BASIC TESTS - Literals, arithmetic, simple expressions
// =============================================================================

const TestCase basic_tests[] = {
    {"Integer Literal", "42;", "42", true},
    {"Boolean True", "true;", "true", true},
    {"Boolean False", "false;", "false", true},
    {"Nil Value", "nil;", "nil", true},
    {"String Literal", "\"hello\";", "hello", true},
    
    {"Simple Addition", "1 + 2;", "3", true},
    {"Simple Subtraction", "5 - 3;", "2", true},
    {"Simple Multiplication", "4 * 3;", "12", true},
    {"Simple Division", "8 / 2;", "4", true},
    {"Modulo Operation", "7 % 3;", "1", true},
    
    {"Arithmetic Precedence", "2 + 3 * 4;", "14", true},
    {"Parentheses", "(2 + 3) * 4;", "20", true},
    {"Negative Numbers", "-5;", "-5", true},
    {"Unary Plus", "+42;", "42", true},
    
    {"Boolean AND", "true and true;", "true", true},
    {"Boolean OR", "false or true;", "true", true},
    {"Boolean NOT", "not false;", "true", true},
    
    {"Comparison Equal", "5 == 5;", "true", true},
    {"Comparison Not Equal", "5 != 3;", "true", true},
    {"Comparison Less", "3 < 5;", "true", true},
    {"Comparison Greater", "7 > 3;", "true", true},
    {"Comparison Less Equal", "5 <= 5;", "true", true},
    {"Comparison Greater Equal", "5 >= 3;", "true", true},
};

const size_t basic_tests_count = sizeof(basic_tests) / sizeof(basic_tests[0]);

// =============================================================================
// VARIABLE TESTS - Variable declaration, assignment, scoping
// =============================================================================

const TestCase variable_tests[] = {
    {"Variable Declaration", "var x = 42; x;", "42", true},
    {"Variable Assignment", "var x = 1; x = 5; x;", "5", true},
    {"Multiple Variables", "var a = 1; var b = 2; a + b;", "3", true},
    {"Variable Shadowing", "var x = 1; { var x = 2; } x;", "1", true},
    {"String Variable", "var name = \"Mobius\"; name;", "Mobius", true},
    {"Boolean Variable", "var flag = true; flag;", "true", true},
    {"Nil Variable", "var empty = nil; empty;", "nil", true},
    
    {"Variable in Expression", "var x = 10; var y = x * 2; y;", "20", true},
    {"Chained Assignment", "var a = 1; var b = a; var c = b; c;", "1", true},
    
    // Block scoping
    {"Block Variable", "{ var x = 42; x; }", "42", true},
    {"Block Scope Isolation", "var x = 1; { var y = 2; } x;", "1", true},
};

const size_t variable_tests_count = sizeof(variable_tests) / sizeof(variable_tests[0]);

// =============================================================================
// CONTROL FLOW TESTS - if, while, for statements
// =============================================================================

const TestCase control_flow_tests[] = {
    {"Simple If True", "if (true) { 42; }", "42", true},
    {"Simple If False", "if (false) { 42; } nil;", "nil", true},
    {"If Else True", "if (true) { 1; } else { 2; }", "1", true},
    {"If Else False", "if (false) { 1; } else { 2; }", "2", true},
    
    {"If Variable", "var x = 5; if (x > 3) { 99; }", "99", true},
    {"If Complex Condition", "var a = 10; var b = 5; if (a > b) { a - b; }", "5", true},
    
    {"Nested If", "if (true) { if (true) { 42; } }", "42", true},
    
    {"While Loop Simple", "var i = 0; while (i < 3) { i = i + 1; } i;", "3", true},
    {"While Loop Sum", "var sum = 0; var i = 1; while (i <= 5) { sum = sum + i; i = i + 1; } sum;", "15", true},
    
    {"Nested Control Flow", "var x = 0; if (true) { while (x < 2) { x = x + 1; } } x;", "2", true},
};

const size_t control_flow_tests_count = sizeof(control_flow_tests) / sizeof(control_flow_tests[0]);

// =============================================================================
// DATA STRUCTURE TESTS - Arrays and tables
// =============================================================================

const TestCase data_structure_tests[] = {
    {"Array Literal", "var arr = [1, 2, 3]; arr;", "Array", true},
    {"Array Index", "var arr = [10, 20, 30]; arr[1];", "20", true},
    {"Array First Element", "var arr = [5, 10, 15]; arr[0];", "5", true},
    {"Array Last Element", "var arr = [1, 2, 3]; arr[2];", "3", true},
    
    {"Empty Array", "var arr = []; arr;", "Array", true},
    {"Single Element Array", "var arr = [42]; arr[0];", "42", true},
    
    {"Mixed Type Array", "var arr = [1, \"hello\", true]; arr[1];", "hello", true},
    
    {"Table Literal", "var table = {a: 1, b: 2}; table;", "Table", true},
    {"Table String Index", "var table = {name: \"test\", value: 42}; table[\"value\"];", "42", true},
    {"Table Key Access", "var table = {x: 10, y: 20}; table[\"x\"];", "10", true},
    
    {"Empty Table", "var table = {}; table;", "Table", true},
    {"Table Single Entry", "var table = {key: \"value\"}; table[\"key\"];", "value", true},
    
    {"Array and Table Mix", "var arr = [1, 2]; var table = {x: 5}; arr[0] + table[\"x\"];", "6", true},
    
    {"Nested Array", "var arr = [[1, 2], [3, 4]]; arr[1];", "Array", true},
    {"Nested Table", "var table = {data: {x: 42}}; table[\"data\"];", "Table", true},
};

const size_t data_structure_tests_count = sizeof(data_structure_tests) / sizeof(data_structure_tests[0]);

// =============================================================================
// FUNCTION TESTS - Function declarations and calls (when implemented)
// =============================================================================

const TestCase function_tests[] = {
    // Note: These tests are for future function implementation
    // Currently they will fail as expected
    {"Function Declaration", "function add(a, b) { return a + b; } add(2, 3);", "5", false},
    {"Simple Function", "function greet() { return \"hello\"; } greet();", "hello", false},
    {"Recursive Function", "function fact(n) { if (n <= 1) return 1; else return n * fact(n-1); } fact(5);", "120", false},
};

const size_t function_tests_count = sizeof(function_tests) / sizeof(function_tests[0]);

// =============================================================================
// ERROR TESTS - Expected failures
// =============================================================================

const TestCase error_tests[] = {
    {"Division by Zero", "5 / 0;", "error", false},
    {"Undefined Variable", "undefined_var;", "error", false},
    {"Invalid Array Index", "var arr = [1, 2]; arr[10];", "nil or error", true}, // Out of bounds returns nil
    {"Type Error", "\"hello\" + 42;", "error", false},
    {"Invalid Operation", "true * false;", "error", false},
    {"Parse Error", "var x = ;", "error", false},
    {"Syntax Error", "if (true { 42; }", "error", false},
};

const size_t error_tests_count = sizeof(error_tests) / sizeof(error_tests[0]);

// =============================================================================
// PERFORMANCE TESTS - Computationally intensive tests for benchmarking
// =============================================================================

const TestCase performance_tests[] = {
    {"Large Loop", 
     "var sum = 0; var i = 0; while (i < 1000) { sum = sum + i; i = i + 1; } sum;", 
     "499500", true},
     
    {"Nested Loops", 
     "var total = 0; var i = 0; while (i < 50) { var j = 0; while (j < 20) { total = total + 1; j = j + 1; } i = i + 1; } total;", 
     "1000", true},
     
    {"Array Operations", 
     "var arr = [1, 2, 3, 4, 5]; var sum = 0; var i = 0; while (i < 5) { sum = sum + arr[i]; i = i + 1; } sum;", 
     "15", true},
     
    {"Table Operations", 
     "var table = {a: 1, b: 2, c: 3, d: 4, e: 5}; table[\"a\"] + table[\"b\"] + table[\"c\"] + table[\"d\"] + table[\"e\"];", 
     "15", true},
     
    {"Complex Expression", 
     "var x = 10; var y = 20; var z = 30; (x + y) * z - (x * y) / 2;", 
     "800", true},
     
    {"Fibonacci Iterative", 
     "var a = 0; var b = 1; var i = 2; while (i <= 10) { var temp = a + b; a = b; b = temp; i = i + 1; } b;", 
     "55", true},
};

const size_t performance_tests_count = sizeof(performance_tests) / sizeof(performance_tests[0]);

// =============================================================================
// INTEGRATION TESTS - Complex real-world scenarios
// =============================================================================

const TestCase integration_tests[] = {
    {"Calculator Program", 
     "var a = 15; var b = 7; var op = \"+\"; var result = 0; if (op == \"+\") { result = a + b; } else if (op == \"-\") { result = a - b; } result;", 
     "22", true},
     
    {"Grade Calculator", 
     "var score = 85; var grade = \"F\"; if (score >= 90) { grade = \"A\"; } else if (score >= 80) { grade = \"B\"; } else if (score >= 70) { grade = \"C\"; } grade;", 
     "B", true},
     
    {"Array Statistics", 
     "var numbers = [10, 20, 30, 40, 50]; var sum = 0; var i = 0; while (i < 5) { sum = sum + numbers[i]; i = i + 1; } var avg = sum / 5; avg;", 
     "30", true},
     
    {"Inventory System", 
     "var inventory = {apples: 50, oranges: 30, bananas: 25}; var total_fruits = inventory[\"apples\"] + inventory[\"oranges\"] + inventory[\"bananas\"]; total_fruits;", 
     "105", true},
     
    {"Game Score System", 
     "var player = {name: \"Player1\", score: 0, level: 1}; var bonus = 100; if (player[\"level\"] > 0) { player = {name: player[\"name\"], score: player[\"score\"] + bonus, level: player[\"level\"]}; } player[\"score\"];", 
     "100", true},
     
    {"Math Library Simulation", 
     "var pi = 3.14159; var radius = 5; var area = pi * radius * radius; var circumference = 2 * pi * radius; area;", 
     "78.53975", true},
};

const size_t integration_tests_count = sizeof(integration_tests) / sizeof(integration_tests[0]);

// =============================================================================
// ALL TESTS COMBINED
// =============================================================================

const TestCase* all_test_suites[] = {
    basic_tests,
    variable_tests,
    control_flow_tests,
    data_structure_tests,
    function_tests,
    error_tests,
    performance_tests,
    integration_tests
};

const size_t* all_test_counts[] = {
    &basic_tests_count,
    &variable_tests_count,
    &control_flow_tests_count,
    &data_structure_tests_count,
    &function_tests_count,
    &error_tests_count,
    &performance_tests_count,
    &integration_tests_count
};

const char* all_test_names[] = {
    "Basic Tests",
    "Variable Tests", 
    "Control Flow Tests",
    "Data Structure Tests",
    "Function Tests",
    "Error Tests",
    "Performance Tests",
    "Integration Tests"
};

const size_t total_test_suites = sizeof(all_test_suites) / sizeof(all_test_suites[0]);

size_t get_total_test_count(void) {
    size_t total = 0;
    for (size_t i = 0; i < total_test_suites; i++) {
        total += *all_test_counts[i];
    }
    return total;
}
