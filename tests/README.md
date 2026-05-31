# Mobius Test Suite

Behavioral tests for the Mobius interpreter, written as `.mob` scripts and run by
the [`test_simple.sh`](../test_simple.sh) runner at the project root.

## Running

```bash
./test_simple.sh
```

The runner:

1. Finds every `*.mob` file under `tests/` (sorted).
2. Runs each with `bin/mobius`, with a 10-second timeout.
3. Treats **exit code 0 as pass** — unless the test is listed in
   [`expected_failures.txt`](expected_failures.txt), in which case a non-zero
   exit is the pass (these verify that errors are reported correctly).
4. Prints a per-test ✅/❌ line and a final summary with the success rate.

> Note: for files directly under `tests/` and under `tests/modules/`, only those
> named `test_*.mob` are executed; helper scripts and fixtures are skipped.

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
