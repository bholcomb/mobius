-- Profiling-oriented Lua benchmark.
-- Loops all benchmarks 100 times for stable measurement.

print("=== Lua Comprehensive Profiling Benchmark ===\n")

local PROFILE_ITERATIONS = 100

-- 1. Arithmetic Operations
local function benchmark_arithmetic()
    local result = 0
    for i = 0, 999999 do
        result = result + i * 2 - 1
        result = result // 2
        result = result % 1000
    end
    return result
end

-- 2. Function Calls
local function fibonacci(n)
    if n <= 1 then return n end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

local function benchmark_function_calls()
    local result = 0
    for _ = 1, 10 do
        result = fibonacci(20)
    end
    return result
end

-- 3. Array Operations
local function benchmark_arrays()
    local arr = {}
    for i = 1, 10000 do
        arr[i] = (i - 1) * 2
    end
    local sum = 0
    for i = 1, 10000 do
        sum = sum + arr[i]
    end
    return sum
end

-- 4. Table Operations
local function build_table_keys(count)
    local keys = {}
    for i = 1, count do
        keys[i] = "key_" .. tostring(i - 1)
    end
    return keys
end

local function benchmark_tables()
    local keys = build_table_keys(5000)
    local tbl = {}
    for i = 1, 5000 do
        tbl[keys[i]] = (i - 1) * 3
    end
    local sum = 0
    for i = 1, 5000 do
        sum = sum + tbl[keys[i]]
    end
    return sum
end

-- 5. String Operations
local function benchmark_strings()
    local base = "Hello"
    local result = ""
    for i = 0, 999 do
        result = base .. " World " .. tostring(i)
        local len_result = #result
        local upper_result = string.upper(result)
    end
    return #result
end

-- 6. Nested Loops
local function benchmark_nested_loops()
    local sum = 0
    for i = 0, 499 do
        for j = 0, 499 do
            sum = sum + i * j
        end
    end
    return sum
end

-- 7. Object Creation/Destruction
local function benchmark_object_lifecycle()
    local last = 0
    for i = 0, 9999 do
        local temp_array = {1, 2, 3, 4, 5}
        local temp_table = {a = 1, b = 2, c = 3}
        local temp_str = "temp" .. tostring(i)
        last = i
    end
    return last
end

-- 8. Mixed Workload
local function benchmark_mixed()
    local data = {}
    for i = 1, 1000 do
        local idx = i - 1
        data[i] = { id = idx, value = idx * 2, name = "item_" .. tostring(idx) }
    end
    local total = 0
    for i = 1, 1000 do
        total = total + data[i].value
    end
    return total
end

local function run_suite()
    local checksum = 0
    checksum = checksum + benchmark_arithmetic()
    checksum = checksum + benchmark_function_calls()
    checksum = checksum + benchmark_arrays()
    checksum = checksum + benchmark_tables()
    checksum = checksum + benchmark_strings()
    checksum = checksum + benchmark_nested_loops()
    checksum = checksum + benchmark_object_lifecycle()
    checksum = checksum + benchmark_mixed()
    return checksum
end

print("Iterations: " .. PROFILE_ITERATIONS)
print("Starting profiling workload...\n")

local overall_start = os.clock()
local checksum = 0

for iter = 1, PROFILE_ITERATIONS do
    checksum = checksum + run_suite()
end

local overall_duration = os.clock() - overall_start

print("=== Profiling Results ===")
print(string.format("Total time:  %f s", overall_duration))
print(string.format("Average iteration time:  %f s", overall_duration / PROFILE_ITERATIONS))
print(string.format("Iterations per second:  %d", math.floor(PROFILE_ITERATIONS / overall_duration)))
print(string.format("Checksum:  %g", checksum))
