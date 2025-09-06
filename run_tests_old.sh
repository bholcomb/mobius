#!/bin/bash

# Mobius Language Test Runner
# Runs all tests in the tests/ directory and validates proper functionality

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# Configuration
MOBIUS_BINARY="./bin/mobius"
TEST_DIR="tests"
TEMP_DIR="/tmp/mobius_test_$$"
TIMEOUT=30  # seconds

# Create temporary directory for test outputs
mkdir -p "$TEMP_DIR"

# Cleanup function
cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

# Print header
print_header() {
    echo -e "${BLUE}================================================${NC}"
    echo -e "${BLUE}           Mobius Language Test Suite           ${NC}"
    echo -e "${BLUE}================================================${NC}"
    echo ""
}

# Print section header
print_section() {
    echo -e "${YELLOW}=== $1 ===${NC}"
}

# Print test result
print_result() {
    local test_name="$1"
    local status="$2"
    local message="$3"
    
    case "$status" in
        "PASS")
            echo -e "  ${GREEN}✓${NC} $test_name"
            ((PASSED_TESTS++))
            ;;
        "FAIL")
            echo -e "  ${RED}✗${NC} $test_name - $message"
            ((FAILED_TESTS++))
            ;;
        "SKIP")
            echo -e "  ${YELLOW}○${NC} $test_name - $message"
            ((SKIPPED_TESTS++))
            ;;
    esac
    ((TOTAL_TESTS++))
}

# Check if Mobius binary exists
check_binary() {
    if [[ ! -f "$MOBIUS_BINARY" ]]; then
        echo -e "${RED}Error: Mobius binary not found at $MOBIUS_BINARY${NC}"
        echo "Please run 'make' to build the interpreter first."
        exit 1
    fi
    
    if [[ ! -x "$MOBIUS_BINARY" ]]; then
        echo -e "${RED}Error: Mobius binary is not executable${NC}"
        exit 1
    fi
}

# Run a single test file
run_test() {
    local test_file="$1"
    local category="$2"
    local test_name=$(basename "$test_file" .mob)
    local output_file="$TEMP_DIR/${category}_${test_name}.out"
    local error_file="$TEMP_DIR/${category}_${test_name}.err"
    
    # Run the test with timeout
    if timeout "$TIMEOUT" "$MOBIUS_BINARY" "$test_file" > "$output_file" 2> "$error_file"; then
        # Test executed successfully
        
        # Check for known error patterns that should cause failure
        if grep -q "Error\|Exception\|Segmentation fault\|Aborted" "$output_file" "$error_file" 2>/dev/null; then
            print_result "$test_name" "FAIL" "Contains error output"
            return 1
        fi
        
        # Check for specific test validation based on test name
        case "$test_name" in
            *error*|*strict*)
                # Error tests should have some error output
                if [[ ! -s "$error_file" ]] && ! grep -q "Error\|Warning" "$output_file" 2>/dev/null; then
                    print_result "$test_name" "FAIL" "Expected error output but got none"
                    return 1
                fi
                ;;
            *)
                # Non-error tests should not have stderr output (except warnings)
                if [[ -s "$error_file" ]] && grep -v "Warning" "$error_file" | grep -q .; then
                    print_result "$test_name" "FAIL" "Unexpected error output"
                    return 1
                fi
                ;;
        esac
        
        # Validate specific test content
        validate_test_content "$test_file" "$output_file" "$test_name"
        
    elif [[ $? -eq 124 ]]; then
        print_result "$test_name" "FAIL" "Timeout after ${TIMEOUT}s"
        return 1
    else
        print_result "$test_name" "FAIL" "Non-zero exit code"
        return 1
    fi
}

# Validate test content based on expected patterns
validate_test_content() {
    local test_file="$1"
    local output_file="$2"
    local test_name="$3"
    
    # Look for expected output patterns in the test file comments
    local expected_patterns=$(grep "// EXPECT:" "$test_file" 2>/dev/null | sed 's|.*// EXPECT: *||')
    local required_patterns=$(grep "// REQUIRE:" "$test_file" 2>/dev/null | sed 's|.*// REQUIRE: *||')
    
    # Check required patterns
    while IFS= read -r pattern; do
        if [[ -n "$pattern" ]] && ! grep -q "$pattern" "$output_file"; then
            print_result "$test_name" "FAIL" "Missing required output: '$pattern'"
            return 1
        fi
    done <<< "$required_patterns"
    
    # Basic validation based on test category
    case "$test_name" in
        hello*)
            if ! grep -q "Hello" "$output_file"; then
                print_result "$test_name" "FAIL" "Missing 'Hello' output"
                return 1
            fi
            ;;
        *arithmetic*|*math*)
            if ! grep -qE "[0-9]+\s*[+\-*/]\s*[0-9]+.*=.*[0-9]+" "$output_file"; then
                print_result "$test_name" "FAIL" "No arithmetic operations found"
                return 1
            fi
            ;;
        *table*)
            if ! grep -qE "(table|Table|\{.*\})" "$output_file"; then
                print_result "$test_name" "FAIL" "No table operations found"
                return 1
            fi
            ;;
        *type*)
            if ! grep -qE "(int8|int16|int32|int64|uint8|uint16|uint32|uint64|float32|float64)" "$output_file"; then
                print_result "$test_name" "FAIL" "No type operations found"
                return 1
            fi
            ;;
    esac
    
    print_result "$test_name" "PASS"
    return 0
}

# Run tests in a specific category
run_category_tests() {
    local category="$1"
    local test_dir="$TEST_DIR/$category"
    
    if [[ ! -d "$test_dir" ]]; then
        print_result "$category" "SKIP" "Directory not found"
        return
    fi
    
    print_section "$category Tests"
    
    local test_files=($(find "$test_dir" -name "*.mob" | sort))
    
    if [[ ${#test_files[@]} -eq 0 ]]; then
        echo "  No test files found in $category"
        return
    fi
    
    for test_file in "${test_files[@]}"; do
        run_test "$test_file" "$category"
    done
    
    echo ""
}

# Print final summary
print_summary() {
    echo -e "${BLUE}================================================${NC}"
    echo -e "${BLUE}                 Test Summary                   ${NC}"
    echo -e "${BLUE}================================================${NC}"
    echo ""
    echo -e "Total Tests:   $TOTAL_TESTS"
    echo -e "${GREEN}Passed:        $PASSED_TESTS${NC}"
    echo -e "${RED}Failed:        $FAILED_TESTS${NC}"
    echo -e "${YELLOW}Skipped:       $SKIPPED_TESTS${NC}"
    echo ""
    
    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "${GREEN}🎉 All tests passed!${NC}"
        return 0
    else
        echo -e "${RED}❌ $FAILED_TESTS test(s) failed.${NC}"
        echo ""
        echo "Check test outputs in: $TEMP_DIR"
        echo "To debug failed tests, run them individually:"
        echo "  $MOBIUS_BINARY tests/category/test_file.mob"
        return 1
    fi
}

# Main execution
main() {
    local categories=("basic" "types" "tables" "errors" "integration")
    local selected_category=""
    local verbose=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -c|--category)
                selected_category="$2"
                shift 2
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -h|--help)
                echo "Usage: $0 [options]"
                echo "Options:"
                echo "  -c, --category CATEGORY  Run tests for specific category only"
                echo "  -v, --verbose           Show detailed output"
                echo "  -h, --help              Show this help message"
                echo ""
                echo "Available categories: ${categories[*]}"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    print_header
    check_binary
    
    if [[ -n "$selected_category" ]]; then
        run_category_tests "$selected_category"
    else
        for category in "${categories[@]}"; do
            run_category_tests "$category"
        done
    fi
    
    print_summary
}

# Run main function with all arguments
main "$@"
