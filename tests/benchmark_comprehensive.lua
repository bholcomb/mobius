-- Comprehensive Lua benchmark aligned with benchmark_comprehensive.mob
-- Arrays benchmark dense numeric storage.
-- Tables benchmark hash-map usage with string keys.

print("=== Lua Comprehensive Performance Benchmark ===\n")
print("Benchmark model: arrays = dense integer storage, tables = string-key hash maps\n")

local function build_table_keys(count)
    local keys = {}
    for i = 1, count do
        keys[i] = "key_" .. tostring(i - 1)
    end
    return keys
end

-- ============================================================================
-- 1. Arithmetic Operations
-- ============================================================================
local function benchmark_arithmetic()
    print("1. Arithmetic Operations...")
    local start = os.clock()
    local result = 0
    for i = 0, 999999 do
        result = result + i * 2 - 1
        result = result // 2
        result = result % 1000
    end
    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Ops/sec:  %d", math.floor(1000000 / duration)))
    return result
end

-- ============================================================================
-- 2. Function Calls
-- ============================================================================
local function fibonacci(n)
    if n <= 1 then
        return n
    end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

local function benchmark_function_calls()
    print("\n2. Function Calls (Fibonacci)...")
    local start = os.clock()
    local result = 0
    for _ = 1, 10 do
        result = fibonacci(20)
    end
    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Result:  %d", result))
    return result
end

-- ============================================================================
-- 3. Array Operations (dense numeric storage)
-- ============================================================================
local function benchmark_arrays()
    print("\n3. Array Operations (Dense Numeric Storage)...")
    local start = os.clock()

    local arr = {}
    for i = 1, 10000 do
        arr[i] = (i - 1) * 2
    end

    local sum = 0
    for i = 1, 10000 do
        sum = sum + arr[i]
    end

    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Sum:  %d", sum))
    return sum
end

-- ============================================================================
-- 4. Table Operations (hash map string keys)
-- ============================================================================
local function benchmark_tables()
    print("\n4. Table Operations (String-Key Hash Map)...")
    local keys = build_table_keys(5000)
    local start = os.clock()

    local tbl = {}
    for i = 1, 5000 do
        tbl[keys[i]] = (i - 1) * 3
    end

    local sum = 0
    for i = 1, 5000 do
        sum = sum + tbl[keys[i]]
    end

    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Sum:  %d", sum))
    return sum
end

-- ============================================================================
-- 5. String Operations
-- ============================================================================
local function benchmark_strings()
    print("\n5. String Operations...")
    local start = os.clock()

    local base = "Hello"
    local result = ""
    for i = 0, 999 do
        result = base .. " World " .. tostring(i)
        local len_result = #result
        local upper_result = string.upper(result)
        if len_result == 0 or upper_result == "" then
            error("unreachable")
        end
    end

    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Final length:  %d", #result))
    return result
end

-- ============================================================================
-- 6. Nested Loops
-- ============================================================================
local function benchmark_nested_loops()
    print("\n6. Nested Loops...")
    local start = os.clock()

    local sum = 0
    for i = 0, 499 do
        for j = 0, 499 do
            sum = sum + i * j
        end
    end

    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Sum:  %d", sum))
    return sum
end

-- ============================================================================
-- 7. Object Creation/Destruction
-- ============================================================================
local function benchmark_object_lifecycle()
    print("\n7. Object Creation/Destruction...")
    local start = os.clock()

    for i = 0, 9999 do
        local temp_array = {1, 2, 3, 4, 5}
        local temp_table = {a = 1, b = 2, c = 3}
        local temp_str = "temp" .. tostring(i)
        if temp_array[1] == 0 or temp_table.a == 0 or temp_str == "" then
            error("unreachable")
        end
    end

    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print("   Objects created: 30000")
    return 10000
end

-- ============================================================================
-- 8. Mixed Workload
-- ============================================================================
local function benchmark_mixed()
    print("\n8. Mixed Workload...")
    local start = os.clock()

    local data = {}
    for i = 1, 1000 do
        local idx = i - 1
        local record = {
            id = idx,
            value = idx * 2,
            name = "item_" .. tostring(idx)
        }
        data[i] = record
    end

    local total = 0
    for i = 1, 1000 do
        total = total + data[i].value
    end

    local duration = os.clock() - start
    print(string.format("   Time:  %f s", duration))
    print(string.format("   Total:  %d", total))
    return total
end

-- ============================================================================
-- Run All Benchmarks
-- ============================================================================
print("Starting benchmarks...\n")
local overall_start = os.clock()

benchmark_arithmetic()
benchmark_function_calls()
benchmark_arrays()
benchmark_tables()
benchmark_strings()
benchmark_nested_loops()
benchmark_object_lifecycle()
benchmark_mixed()

local overall_duration = os.clock() - overall_start

print("\n=== Overall Results ===")
print(string.format("Total time:  %f s", overall_duration))
print("Benchmark complete!")
