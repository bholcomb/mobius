-- Comprehensive Lua benchmark, semantically identical to
-- benchmark_comprehensive.mob and benchmark_comprehensive.py.
--
-- Every benchmark returns an integer checksum; run_benchmarks.py refuses to
-- report timings unless all three languages agree on every checksum. If you
-- change a workload here, change it in all three.
--
-- Note on operators: Mobius `/` truncates and `%` is a C-style remainder,
-- while Lua `//` and `%` floor. They agree only on non-negative operands, so
-- every intermediate below is kept non-negative and integer division is
-- avoided entirely.
--
-- Output format, one line per benchmark, consumed by run_benchmarks.py:
--   BENCH <id> <seconds> <checksum>

print("=== Lua Comprehensive Performance Benchmark ===")
print("Benchmark model: arrays = dense integer storage, tables = string-key hash maps")

local clock = os.clock

local function report(id, start, checksum)
    print(string.format("BENCH %s %.9f %d", id, clock() - start, checksum))
end

-- ============================================================================
-- 1. Arithmetic (integer)
-- ============================================================================
local function benchmark_arithmetic()
    local start = clock()
    local r = 0
    for i = 0, 9999999 do
        r = (r + i * 2 + 1) % 1000003
        r = (r * 3 + 7) % 1000003
    end
    report("arith", start, r)
    return r
end

-- ============================================================================
-- 2. Function calls — recursive fibonacci
-- ============================================================================
local function fibonacci(n)
    if n <= 1 then
        return n
    end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

local function benchmark_function_calls()
    local start = clock()
    local result = fibonacci(30)
    report("fib", start, result)
    return result
end

-- ============================================================================
-- 3. Array operations (dense numeric storage). Array is preallocated.
-- ============================================================================
local function benchmark_arrays()
    local n = 1000000
    local arr = {}
    for i = 0, n - 1 do arr[i] = 0 end     -- setup: outside the timer
    local start = clock()

    for i = 0, n - 1 do
        arr[i] = i * 2
    end
    local sum = 0
    for i = 0, n - 1 do
        sum = sum + arr[i]
    end

    report("array", start, sum)
    return sum
end

-- ============================================================================
-- 4. Table operations (string-key hash map). Keys built outside the timer.
-- ============================================================================
local function build_table_keys(count)
    local keys = {}
    for i = 0, count - 1 do
        keys[i] = "key_" .. tostring(i)
    end
    return keys
end

local function benchmark_tables()
    local n = 50000
    local keys = build_table_keys(n)       -- setup: outside the timer
    local start = clock()

    local tbl = {}
    for i = 0, n - 1 do
        tbl[keys[i]] = i * 3
    end

    local sum = 0
    for pass = 0, 9 do
        for i = 0, n - 1 do
            sum = sum + tbl[keys[i]]
        end
    end

    report("table", start, sum)
    return sum
end

-- ============================================================================
-- 5. String operations
-- ============================================================================
local function benchmark_strings()
    local base = "Hello"
    local start = clock()

    local total = 0
    for i = 0, 199999 do
        local result = base .. " World " .. tostring(i)
        total = total + #result
        local upper_result = string.upper(result)
        total = total + #upper_result
    end

    report("string", start, total)
    return total
end

-- ============================================================================
-- 6. Nested loops
-- ============================================================================
local function benchmark_nested_loops()
    local start = clock()

    local sum = 0
    for i = 0, 2999 do
        for j = 0, 2999 do
            sum = sum + i * j
        end
    end

    report("nested", start, sum)
    return sum
end

-- ============================================================================
-- 7. Object creation / destruction
-- ============================================================================
local function benchmark_object_lifecycle()
    local start = clock()

    local total = 0
    for i = 0, 299999 do
        local temp_array = {1, 2, 3, 4, 5}
        local temp_table = {a = 1, b = 2, c = 3}
        local temp_str = "temp" .. tostring(i)
        total = total + temp_array[1] + temp_table.a + #temp_str
    end

    report("objlife", start, total)
    return total
end

-- ============================================================================
-- 8. Mixed workload
-- ============================================================================
local function benchmark_mixed()
    local n = 100000
    local start = clock()

    local data = {}
    for i = 0, n - 1 do
        local record = {
            id = i,
            value = i * 2,
            name = "item_" .. tostring(i)
        }
        data[i] = record
    end

    local total = 0
    for i = 0, n - 1 do
        local record = data[i]
        total = total + record.value + #record.name
    end

    report("mixed", start, total)
    return total
end

-- ============================================================================
-- Run all benchmarks
-- ============================================================================
local overall_start = clock()

benchmark_arithmetic()
benchmark_function_calls()
benchmark_arrays()
benchmark_tables()
benchmark_strings()
benchmark_nested_loops()
benchmark_object_lifecycle()
benchmark_mixed()

print(string.format("BENCH %s %.9f %d", "total", clock() - overall_start, 0))
