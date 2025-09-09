#!/bin/bash

# Simple test runner that shows pass/fail for each test
# Usage: ./test_simple.sh [ast|bytecode]

MODE=${1:-bytecode}

if [ "$MODE" = "ast" ]; then
    FLAG=""
    echo "=== Running tests with AST backend ==="
else
    FLAG="--bytecode"
    echo "=== Running tests with Bytecode backend ==="
fi

echo

PASSED=0
FAILED=0
FAILED_TESTS=()

# Find all .mob test files
TEST_FILES=$(find tests -name "*.mob" | sort)

for test_file in $TEST_FILES; do
    printf "%-50s " "$test_file"
    
    # Run the test and capture output/exit code
    if timeout 5 ./bin/mobius $FLAG "$test_file" >/dev/null 2>&1; then
        echo "✅ PASS"
        ((PASSED++))
    else
        echo "❌ FAIL"
        ((FAILED++))
        FAILED_TESTS+=("$test_file")
    fi
done

echo
echo "=== Test Summary ==="
echo "Total Tests:  $((PASSED + FAILED))"
echo "Passed:       $PASSED"
echo "Failed:       $FAILED"

if [ $FAILED -gt 0 ]; then
    echo
    echo "Failed tests:"
    for failed_test in "${FAILED_TESTS[@]}"; do
        echo "  - $failed_test"
    done
fi

echo
if [ $FAILED -eq 0 ]; then
    echo "🎉 All tests passed!"
    SUCCESS_RATE="100"
else
    SUCCESS_RATE=$((PASSED * 100 / (PASSED + FAILED)))
    if [ "$MODE" = "bytecode" ]; then
        echo "❌ Bytecode Success Rate: $SUCCESS_RATE%"
    else
        echo "❌ AST Success Rate: $SUCCESS_RATE%"
    fi
fi

