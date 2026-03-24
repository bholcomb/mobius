"""Profiling-oriented Python benchmark.
Loops all benchmarks 100 times for stable measurement."""

import time

print("=== Python Comprehensive Profiling Benchmark ===\n")

PROFILE_ITERATIONS = 100

# 1. Arithmetic Operations
def benchmark_arithmetic():
    result = 0
    for i in range(1000000):
        result = result + i * 2 - 1
        result = result // 2
        result = result % 1000
    return result

# 2. Function Calls
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

def benchmark_function_calls():
    result = 0
    for _ in range(10):
        result = fibonacci(20)
    return result

# 3. Array Operations
def benchmark_arrays():
    arr = []
    for i in range(10000):
        arr.append(i * 2)
    total = 0
    for i in range(10000):
        total = total + arr[i]
    return total

# 4. Table Operations
def build_table_keys(count):
    return ["key_" + str(i) for i in range(count)]

def benchmark_tables():
    keys = build_table_keys(5000)
    tbl = {}
    for i in range(5000):
        tbl[keys[i]] = i * 3
    total = 0
    for i in range(5000):
        total = total + tbl[keys[i]]
    return total

# 5. String Operations
def benchmark_strings():
    base = "Hello"
    result = ""
    for i in range(1000):
        result = base + " World " + str(i)
        len_result = len(result)
        upper_result = result.upper()
    return len(result)

# 6. Nested Loops
def benchmark_nested_loops():
    total = 0
    for i in range(500):
        for j in range(500):
            total = total + i * j
    return total

# 7. Object Creation/Destruction
def benchmark_object_lifecycle():
    last = 0
    for i in range(10000):
        temp_array = [1, 2, 3, 4, 5]
        temp_table = {"a": 1, "b": 2, "c": 3}
        temp_str = "temp" + str(i)
        last = i
    return last

# 8. Mixed Workload
def benchmark_mixed():
    data = []
    for i in range(1000):
        data.append({"id": i, "value": i * 2, "name": "item_" + str(i)})
    total = 0
    for i in range(1000):
        total = total + data[i]["value"]
    return total

def run_suite():
    checksum = 0
    checksum += benchmark_arithmetic()
    checksum += benchmark_function_calls()
    checksum += benchmark_arrays()
    checksum += benchmark_tables()
    checksum += benchmark_strings()
    checksum += benchmark_nested_loops()
    checksum += benchmark_object_lifecycle()
    checksum += benchmark_mixed()
    return checksum

print(f"Iterations: {PROFILE_ITERATIONS}")
print("Starting profiling workload...\n")

overall_start = time.perf_counter()
checksum = 0

for iteration in range(PROFILE_ITERATIONS):
    checksum += run_suite()

overall_duration = time.perf_counter() - overall_start

print("=== Profiling Results ===")
print(f"Total time:  {overall_duration:.5f} s")
print(f"Average iteration time:  {overall_duration / PROFILE_ITERATIONS:.5f} s")
print(f"Iterations per second:  {int(PROFILE_ITERATIONS / overall_duration)}")
print(f"Checksum:  {checksum}")
