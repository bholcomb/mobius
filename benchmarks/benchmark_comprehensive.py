"""Comprehensive Python benchmark, semantically identical to
benchmark_comprehensive.mob and benchmark_comprehensive.lua.

Every benchmark returns an integer checksum; run_benchmarks.py refuses to
report timings unless all three languages agree on every checksum. If you
change a workload here, change it in all three.

Note on operators: Mobius `/` truncates and `%` is a C-style remainder, while
Python `//` and `%` floor. They agree only on non-negative operands, so every
intermediate below is kept non-negative and integer division is avoided.

Output format, one line per benchmark, consumed by run_benchmarks.py:
    BENCH <id> <seconds> <checksum>
"""

import time

print("=== Python Comprehensive Performance Benchmark ===")
print("Benchmark model: arrays = dense integer storage, tables = string-key hash maps")

clock = time.perf_counter


def report(bench_id, start, checksum):
    print(f"BENCH {bench_id} {clock() - start:.9f} {checksum}")


# ============================================================================
# 1. Arithmetic (integer)
# ============================================================================
def benchmark_arithmetic():
    start = clock()
    r = 0
    for i in range(10000000):
        r = (r + i * 2 + 1) % 1000003
        r = (r * 3 + 7) % 1000003
    report("arith", start, r)
    return r


# ============================================================================
# 2. Function calls — recursive fibonacci
# ============================================================================
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)


def benchmark_function_calls():
    start = clock()
    result = fibonacci(30)
    report("fib", start, result)
    return result


# ============================================================================
# 3. Array operations (dense numeric storage). List is preallocated.
# ============================================================================
def benchmark_arrays():
    n = 1000000
    arr = [0] * n                      # setup: outside the timer
    start = clock()

    for i in range(n):
        arr[i] = i * 2
    total = 0
    for i in range(n):
        total = total + arr[i]

    report("array", start, total)
    return total


# ============================================================================
# 4. Table operations (string-key hash map). Keys built outside the timer.
# ============================================================================
def build_table_keys(count):
    return ["key_" + str(i) for i in range(count)]


def benchmark_tables():
    n = 50000
    keys = build_table_keys(n)         # setup: outside the timer
    start = clock()

    tbl = {}
    for i in range(n):
        tbl[keys[i]] = i * 3

    total = 0
    for _ in range(10):
        for i in range(n):
            total = total + tbl[keys[i]]

    report("table", start, total)
    return total


# ============================================================================
# 5. String operations
# ============================================================================
def benchmark_strings():
    base = "Hello"
    start = clock()

    total = 0
    for i in range(200000):
        result = base + " World " + str(i)
        total = total + len(result)
        upper_result = result.upper()
        total = total + len(upper_result)

    report("string", start, total)
    return total


# ============================================================================
# 6. Nested loops
# ============================================================================
def benchmark_nested_loops():
    start = clock()

    total = 0
    for i in range(3000):
        for j in range(3000):
            total = total + i * j

    report("nested", start, total)
    return total


# ============================================================================
# 7. Object creation / destruction
# ============================================================================
def benchmark_object_lifecycle():
    start = clock()

    total = 0
    for i in range(300000):
        temp_array = [1, 2, 3, 4, 5]
        temp_table = {"a": 1, "b": 2, "c": 3}
        temp_str = "temp" + str(i)
        total = total + temp_array[0] + temp_table["a"] + len(temp_str)

    report("objlife", start, total)
    return total


# ============================================================================
# 8. Mixed workload
# ============================================================================
def benchmark_mixed():
    n = 100000
    start = clock()

    data = []
    for i in range(n):
        record = {
            "id": i,
            "value": i * 2,
            "name": "item_" + str(i),
        }
        data.append(record)

    total = 0
    for i in range(n):
        record = data[i]
        total = total + record["value"] + len(record["name"])

    report("mixed", start, total)
    return total


# ============================================================================
# Run all benchmarks
# ============================================================================
overall_start = clock()

benchmark_arithmetic()
benchmark_function_calls()
benchmark_arrays()
benchmark_tables()
benchmark_strings()
benchmark_nested_loops()
benchmark_object_lifecycle()
benchmark_mixed()

print(f"BENCH total {clock() - overall_start:.9f} 0")
