#!/bin/bash

# Mobius Test Runner
# Runs all test scripts with both AST and bytecode backends

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

MOBIUS_BIN="./bin/mobius"
TESTS_DIR="tests"
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Check if mobius binary exists
if [ ! -f "$MOBIUS_BIN" ]; then
    echo -e "${RED}Error: $MOBIUS_BIN not found. Please run 'make' first.${NC}"
    exit 1
fi

echo -e "${BLUE}🎯 Mobius Dual-Backend Test Runner${NC}"
echo -e "${BLUE}===================================${NC}"
echo ""

# Function to run a single test with both backends
run_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .mob)
    
    echo -e "${YELLOW}Testing: $test_name${NC}"
    
    local ast_success=true
    local bytecode_success=true
    local ast_output=""
    local bytecode_output=""
    
    # Run with AST backend
    if ast_output=$($MOBIUS_BIN "$test_file" 2>&1); then
        echo -e "  AST:      ${GREEN}✓ PASS${NC} -> $ast_output"
    else
        echo -e "  AST:      ${RED}✗ FAIL${NC} -> $ast_output"
        ast_success=false
    fi
    
    # Run with bytecode backend
    if bytecode_output=$($MOBIUS_BIN --bytecode "$test_file" 2>&1); then
        echo -e "  Bytecode: ${GREEN}✓ PASS${NC} -> $bytecode_output"
    else
        echo -e "  Bytecode: ${RED}✗ FAIL${NC} -> $bytecode_output"
        bytecode_success=false
    fi
    
    # Check if outputs match
    if [ "$ast_success" = true ] && [ "$bytecode_success" = true ]; then
        if [ "$ast_output" = "$bytecode_output" ]; then
            echo -e "  Result:   ${GREEN}✓ CONSISTENT${NC}"
            ((PASSED_TESTS++))
        else
            echo -e "  Result:   ${RED}✗ MISMATCH${NC} (AST: '$ast_output' vs Bytecode: '$bytecode_output')"
            ((FAILED_TESTS++))
        fi
    else
        echo -e "  Result:   ${RED}✗ EXECUTION FAILURE${NC}"
        ((FAILED_TESTS++))
    fi
    
    echo ""
    ((TOTAL_TESTS++))
}

# Find and run all test files
if [ ! -d "$TESTS_DIR" ]; then
    echo -e "${RED}Error: Tests directory '$TESTS_DIR' not found.${NC}"
    exit 1
fi

# Run tests in numerical order
for test_file in $(find "$TESTS_DIR" -name "*.mob" 2>/dev/null | sort); do
    run_test "$test_file"
done

# Print summary
echo -e "${BLUE}=======================================${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}=======================================${NC}"
echo "Total Tests:  $TOTAL_TESTS"
echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}🎉 ALL TESTS PASSED! 100% SUCCESS RATE!${NC}"
    exit 0
else
    success_rate=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    echo -e "${RED}❌ Some tests failed. Success rate: $success_rate%${NC}"
    exit 1
fi