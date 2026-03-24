"""Comprehensive Python benchmark aligned with benchmark_comprehensive.mob"""

import time

print("=== Python Comprehensive Performance Benchmark ===\n")
print("Benchmark model: arrays = dense integer storage, tables = string-key hash maps\n")

def build_table_keys(count):
    return ["key_" + str(i) for i in range(count)]

# ============================================================================
# 1. Arithmetic Operations
# ============================================================================
def benchmark_arithmetic():
    print("1. Arithmetic Operations...")
    start = time.perf_counter()
    result = 0
    for i in range(1000000):
        result = result + i * 2 - 1
        result = result // 2
        result = result % 1000
    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Ops/sec:  {int(1000000 / duration)}")
    return result

# ============================================================================
# 2. Function Calls
# ============================================================================
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

def benchmark_function_calls():
    print("\n2. Function Calls (Fibonacci)...")
    start = time.perf_counter()
    result = 0
    for _ in range(10):
        result = fibonacci(20)
    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Result:  {result}")
    return result

# ============================================================================
# 3. Array Operations (dense numeric storage)
# ============================================================================
def benchmark_arrays():
    print("\n3. Array Operations (Dense Numeric Storage)...")
    start = time.perf_counter()

    arr = []
    for i in range(10000):
        arr.append(i * 2)

    total = 0
    for i in range(10000):
        total = total + arr[i]

    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Sum:  {total}")
    return total

# ============================================================================
# 4. Table Operations (hash map string keys)
# ============================================================================
def benchmark_tables():
    print("\n4. Table Operations (String-Key Hash Map)...")
    keys = build_table_keys(5000)
    start = time.perf_counter()

    tbl = {}
    for i in range(5000):
        tbl[keys[i]] = i * 3

    total = 0
    for i in range(5000):
        total = total + tbl[keys[i]]

    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Sum:  {total}")
    return total

# ============================================================================
# 5. String Operations
# ============================================================================
def benchmark_strings():
    print("\n5. String Operations...")
    start = time.perf_counter()

    base = "Hello"
    result = ""
    for i in range(1000):
        result = base + " World " + str(i)
        len_result = len(result)
        upper_result = result.upper()
        if len_result == 0 or upper_result == "":
            raise RuntimeError("unreachable")

    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Final length:  {len(result)}")
    return result

# ============================================================================
# 6. Nested Loops
# ============================================================================
def benchmark_nested_loops():
    print("\n6. Nested Loops...")
    start = time.perf_counter()

    total = 0
    for i in range(500):
        for j in range(500):
            total = total + i * j

    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Sum:  {total}")
    return total

# ============================================================================
# 7. Object Creation/Destruction
# ============================================================================
def benchmark_object_lifecycle():
    print("\n7. Object Creation/Destruction...")
    start = time.perf_counter()

    for i in range(10000):
        temp_array = [1, 2, 3, 4, 5]
        temp_table = {"a": 1, "b": 2, "c": 3}
        temp_str = "temp" + str(i)
        if temp_array[0] == 0 or temp_table["a"] == 0 or temp_str == "":
            raise RuntimeError("unreachable")

    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print("   Objects created: 30000")
    return 10000

# ============================================================================
# 8. Mixed Workload
# ============================================================================
def benchmark_mixed():
    print("\n8. Mixed Workload...")
    start = time.perf_counter()

    data = []
    for i in range(1000):
        record = {
            "id": i,
            "value": i * 2,
            "name": "item_" + str(i)
        }
        data.append(record)

    total = 0
    for i in range(1000):
        total = total + data[i]["value"]

    duration = time.perf_counter() - start
    print(f"   Time:  {duration:.6f} s")
    print(f"   Total:  {total}")
    return total

# ============================================================================
# Run All Benchmarks
# ============================================================================
print("Starting benchmarks...\n")
overall_start = time.perf_counter()

benchmark_arithmetic()
benchmark_function_calls()
benchmark_arrays()
benchmark_tables()
benchmark_strings()
benchmark_nested_loops()
benchmark_object_lifecycle()
benchmark_mixed()

overall_duration = time.perf_counter() - overall_start

print("\n=== Overall Results ===")
print(f"Total time:  {overall_duration:.6f} s")
print("Benchmark complete!")
