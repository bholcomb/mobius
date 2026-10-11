#!/bin/bash
# Run the test suite: tools/run_tests.mob, which works on every platform
# (bin/mobius tools/run_tests.mob on Windows).
exec "$(dirname "$0")/bin/mobius" "$(dirname "$0")/tools/run_tests.mob"
