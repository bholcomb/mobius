#!/bin/bash

# Simple Mobius Test Runner
# Runs all .mob files in tests/ directory and reports pass/fail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Counters
TOTAL=0
PASSED=0
FAILED=0

# Configuration
MOBIUS="./bin/mobius"
TIMEOUT=30

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}        Simple Mobius Test Runner               ${NC}"
echo -e "${BLUE}================================================${NC}"
echo

# Check if mobius binary exists
if [[ ! -f "$MOBIUS" ]]; then
    echo -e "${RED}Error: $MOBIUS not found. Run 'make' first.${NC}"
    exit 1
fi

# Find all test files
TEST_FILES=$(find tests -name "*.mob" | sort)

if [[ -z "$TEST_FILES" ]]; then
    echo "No test files found in tests/ directory"
    exit 1
fi

echo "Running tests..."
echo

# Run each test
for test_file in $TEST_FILES; do
    test_name=$(basename "$test_file" .mob)
    category=$(basename $(dirname "$test_file"))
    
    ((TOTAL++))
    
    # Run the test with timeout, capture exit code
    if timeout "$TIMEOUT" "$MOBIUS" "$test_file" >/dev/null 2>&1; then
        echo -e "  ${GREEN}✓${NC} $category/$test_name"
        ((PASSED++))
    else
        exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            echo -e "  ${RED}✗${NC} $category/$test_name (timeout)"
        else
            echo -e "  ${RED}✗${NC} $category/$test_name (exit code: $exit_code)"
        fi
        ((FAILED++))
    fi
done

echo
echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}                 Results                        ${NC}"
echo -e "${BLUE}================================================${NC}"
echo
echo "Total Tests:  $TOTAL"
echo -e "${GREEN}Passed:       $PASSED${NC}"
echo -e "${RED}Failed:       $FAILED${NC}"
echo

if [[ $FAILED -eq 0 ]]; then
    echo -e "${GREEN}🎉 All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}❌ $FAILED test(s) failed${NC}"
    echo
    echo "To debug a failed test, run it manually:"
    echo "  $MOBIUS tests/category/test_name.mob"
    exit 1
fi
