# Mobius Test Suite

Behavioral tests for the Mobius interpreter, written as `.mob` scripts, plus
C API tests in `tests/embed/`, run by [`tools/run_tests.mob`](../tools/run_tests.mob).

## Running

```bash
./test_simple.sh                    # Linux and macOS
bin/mobius tools/run_tests.mob      # any platform
```

The runner:

1. Finds every `test_*.mob` file under `tests/` (sorted); helper scripts and
   fixtures are skipped.
2. Runs each with `bin/mobius` from the project root, with a 10-second timeout.
3. Treats **exit code 0 as pass** — unless the test is listed in
   [`expected_failures.txt`](expected_failures.txt), in which case a non-zero
   exit is the pass (these verify that errors are reported correctly). Exit
   code 77 means the test skipped itself because something it needs isn't
   built here (the `monstro` and `vulkease` packages).
4. Compiles each `tests/embed/test_*.c` against `bin/`'s core library with the
   system C compiler (`cc`) and runs it the same way. These use POSIX threads,
   so they are skipped on Windows.
5. Prints a line per test and a summary, and exits 1 if any test failed.

A test "passes" simply by running to completion with the expected exit status,
so tests assert their own invariants and call `exit(1)` (or `throw`) on failure.

## Categories

| Directory            | Focus |
|----------------------|-------|
| `tests/basic/`       | Core language: literals, operators, functions, stdlib, exceptions, CLI `argv` |
| `tests/types/`       | Type locking, annotations, strict mode, integer/float behavior |
| `tests/tables/`      | Tables, metatables (`__index`, arithmetic, `__newindex`), OOP patterns, `pairs` |
| `tests/functions/`   | Function declaration, closures, recursion |
| `tests/errors/`      | Error reporting and validation (mostly expected-failure tests) |
| `tests/fiber/`       | Fibers, channels, `shared`/`atomic`, cancellation, futures |
| `tests/integration/` | Plugins, userdata, and cross-feature interaction |
| `tests/modules/`     | Bundled module behavior (`json`, `regex`, `crypto`, …) |
| `tests/fixtures/`    | Shared data/helpers used by other tests |

## Writing a test

Create `tests/<category>/test_<name>.mob` and make it self-checking: print
progress, and exit non-zero (via `exit(1)`) or `throw` when an invariant fails.

```mobius
print("=== my feature ===")

var x = 2 + 2
if (x != 4) { print("FAIL: arithmetic"); exit(1) }

print("ok")
```

For tests that are *supposed* to fail (e.g. verifying a runtime error is
raised), add their path to [`expected_failures.txt`](expected_failures.txt).

## Debugging a failure

```bash
bin/mobius tests/<category>/<failing_test>.mob   # run it directly to see output
```

The CLI-argument test (`tests/basic/test_cli_argv.mob`) is invoked by the runner
with extra arguments to exercise the [`argv`](../docs/reference/standard-library.md#argv)
global.
