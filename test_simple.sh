#!/bin/bash

# Simple test runner that shows pass/fail for each test
# Usage: ./test_simple.sh

echo

PASSED=0
FAILED=0
FAILED_TESTS=()

# Load expected failures list
EXPECTED_FAILURES=()
if [ -f "tests/expected_failures.txt" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        # Skip comments and empty lines
        if [[ ! "$line" =~ ^[[:space:]]*# ]] && [[ -n "${line// }" ]]; then
            EXPECTED_FAILURES+=("$line")
        fi
    done < "tests/expected_failures.txt"
fi

# Function to check if a test is expected to fail
is_expected_to_fail() {
    local test_file="$1"
    for expected in "${EXPECTED_FAILURES[@]}"; do
        if [ "$test_file" = "$expected" ]; then
            return 0  # true - expected to fail
        fi
    done
    return 1  # false - expected to pass
}

should_run_test() {
    local test_file="$1"
    case "$test_file" in
        tests/modules/*.mob)
            [[ "$(basename "$test_file")" == test_* ]]
            return
            ;;
    esac
    return 0
}

# Find all .mob test files
TEST_FILES=$(find tests -name "*.mob" | sort)

for test_file in $TEST_FILES; do
    if ! should_run_test "$test_file"; then
        continue
    fi

    printf "%-50s " "$test_file"

    CMD=(./bin/mobius "$test_file")
    if [ "$test_file" = "tests/basic/test_cli_argv.mob" ]; then
        CMD+=(alpha "two words" --flag 42)
    fi

    # Run the test and capture output/exit code
    if timeout 10 "${CMD[@]}" >/dev/null 2>&1; then
        # Test succeeded (exit code 0)
        if is_expected_to_fail "$test_file"; then
            echo "❌ FAIL (expected to fail but passed)"
            ((FAILED++))
            FAILED_TESTS+=("$test_file")
        else
            echo "✅ PASS"
            ((PASSED++))
        fi
    else
        # Test failed (non-zero exit code)
        if is_expected_to_fail "$test_file"; then
            echo "✅ PASS (failed as expected)"
            ((PASSED++))
        else
            echo "❌ FAIL"
            ((FAILED++))
            FAILED_TESTS+=("$test_file")
        fi
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
    echo "❌ Success Rate: $SUCCESS_RATE%"
fi

