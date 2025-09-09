# RISC-V Compilation Examples for Mobius

This document shows how Mobius language constructs compile to RISC-V instructions.

## Register Usage Convention

```c
// RISC-V registers allocated for Mobius VM
x0  (zero)    - Always zero (hardware)
x1  (ra)      - Return address (hardware)
x2  (sp)      - Stack pointer (hardware)
x3  (gp)      - Global pointer (hardware)
x4  (tp)      - Thread pointer (hardware)

// Mobius VM specific register allocation
x18 (s2)      - Mobius value stack pointer
x19 (s3)      - Current environment pointer  
x20 (s4)      - Constant pool pointer
x21 (s5)      - Temporary register for type checking
x22 (s6)      - Temporary register for reference counting

// Argument passing (follows RISC-V ABI)
x10-x17 (a0-a7) - Function arguments and return values
x5-x7   (t0-t2) - Temporary registers for operations
x28-x31 (t3-t6) - Additional temporary registers
```

## Basic Arithmetic Operations

### Mobius Code:
```mobius
var result = a + b;
```

### Generated RISC-V Assembly:
```assembly
# Load variable 'a' from environment
lw    t0, 0(s3)          # Load environment pointer
lw    t1, 4(t0)          # Load 'a' value pointer
lw    t2, 0(t1)          # Load 'a' value type
addi  t3, zero, 2        # VAL_INTEGER = 2
bne   t2, t3, type_error # Check if 'a' is integer
lw    a0, 8(t1)          # Load 'a' integer value

# Load variable 'b' from environment  
lw    t1, 8(t0)          # Load 'b' value pointer
lw    t2, 0(t1)          # Load 'b' value type
bne   t2, t3, type_error # Check if 'b' is integer
lw    a1, 8(t1)          # Load 'b' integer value

# Perform addition
add   a0, a0, a1         # result = a + b

# Create new Value for result
jal   ra, mobius_alloc_value  # Allocate new Value
sw    t3, 0(a0)          # Set type to VAL_INTEGER
sw    a0, 8(a0)          # Store result value

# Store in variable 'result'
sw    a0, 12(s3)         # Store in environment slot for 'result'
```

## Function Call

### Mobius Code:
```mobius
func add(x, y) {
    return x + y;
}

var result = add(5, 10);
```

### Generated RISC-V Assembly:
```assembly
# Function definition: add(x, y)
add_function:
    # Function prologue
    addi  sp, sp, -16        # Allocate stack frame
    sw    ra, 12(sp)         # Save return address
    sw    s0, 8(sp)          # Save frame pointer
    addi  s0, sp, 16         # Set frame pointer
    
    # Load parameters x and y from argument registers
    # a0 = x, a1 = y (already loaded by caller)
    
    # Perform addition: x + y
    add   a0, a0, a1         # result = x + y
    
    # Function epilogue
    lw    ra, 12(sp)         # Restore return address
    lw    s0, 8(sp)          # Restore frame pointer
    addi  sp, sp, 16         # Deallocate stack frame
    jalr  zero, ra, 0        # Return to caller

# Function call: add(5, 10)
main:
    # Load immediate values
    addi  a0, zero, 5        # First argument: 5
    addi  a1, zero, 10       # Second argument: 10
    
    # Call function
    jal   ra, add_function   # Call add(5, 10)
    
    # Result is in a0, store in 'result' variable
    sw    a0, 0(s3)          # Store result in environment
```

## Control Flow (If Statement)

### Mobius Code:
```mobius
if (x > 5) {
    print("x is greater than 5");
} else {
    print("x is 5 or less");
}
```

### Generated RISC-V Assembly:
```assembly
# Load variable x
lw    t0, 0(s3)          # Load x from environment
addi  t1, zero, 5        # Load immediate 5

# Compare x > 5
slt   t2, t1, t0         # t2 = (5 < x) ? 1 : 0
beq   t2, zero, else_branch  # If x <= 5, go to else

# Then branch: x > 5
then_branch:
    # Load string constant "x is greater than 5"
    lw    a0, 0(s4)          # Load from constant pool
    jal   ra, mobius_print   # Call print function
    jal   zero, end_if       # Jump to end

# Else branch: x <= 5  
else_branch:
    # Load string constant "x is 5 or less"
    lw    a0, 4(s4)          # Load from constant pool
    jal   ra, mobius_print   # Call print function

end_if:
    # Continue execution
```

## Array Operations

### Mobius Code:
```mobius
var arr = [1, 2, 3];
var element = arr[1];
```

### Generated RISC-V Assembly:
```assembly
# Create array [1, 2, 3]
create_array:
    # Allocate array structure
    addi  a0, zero, 3        # Array length = 3
    jal   ra, mobius_alloc_array  # Allocate array
    mv    t0, a0             # Save array pointer
    
    # Store elements
    addi  t1, zero, 1        # Element 0: 1
    sw    t1, 0(t0)          # arr[0] = 1
    addi  t1, zero, 2        # Element 1: 2  
    sw    t1, 4(t0)          # arr[1] = 2
    addi  t1, zero, 3        # Element 2: 3
    sw    t1, 8(t0)          # arr[2] = 3
    
    # Store array in variable 'arr'
    sw    t0, 0(s3)          # Store in environment

# Array indexing: arr[1]
array_access:
    lw    t0, 0(s3)          # Load array pointer
    addi  t1, zero, 1        # Index = 1
    
    # Bounds checking
    lw    t2, -4(t0)         # Load array length (stored before data)
    sltu  t3, t1, t2         # Check if index < length
    beq   t3, zero, bounds_error  # If index >= length, error
    
    # Calculate offset and load element
    slli  t1, t1, 2          # index * 4 (word size)
    add   t0, t0, t1         # array_ptr + offset
    lw    a0, 0(t0)          # Load arr[1]
    
    # Store in variable 'element'
    sw    a0, 4(s3)          # Store in environment
```

## String Operations

### Mobius Code:
```mobius
var greeting = "Hello, " + name;
```

### Generated RISC-V Assembly:
```assembly
# String concatenation: "Hello, " + name
string_concat:
    # Load first string "Hello, "
    lw    a0, 0(s4)          # Load from constant pool
    
    # Load variable 'name'
    lw    a1, 0(s3)          # Load name from environment
    
    # Check if name is a string
    lw    t0, 0(a1)          # Load type
    addi  t1, zero, 5        # VAL_STRING = 5
    bne   t0, t1, type_error # Check type
    
    # Get string data pointers
    lw    a0, 8(a0)          # Get "Hello, " data pointer
    lw    a1, 8(a1)          # Get name data pointer
    
    # Call string concatenation runtime function
    jal   ra, mobius_string_concat  # Returns new string in a0
    
    # Store result in 'greeting'
    sw    a0, 4(s3)          # Store in environment
```

## Advanced: Function with Closure

### Mobius Code:
```mobius
func make_counter(start) {
    func counter() {
        start = start + 1;
        return start;
    }
    return counter;
}

var count = make_counter(0);
var result = count();  // Returns 1
```

### Generated RISC-V Assembly:
```assembly
# make_counter function
make_counter:
    # Prologue
    addi  sp, sp, -16
    sw    ra, 12(sp)
    sw    s0, 8(sp)
    addi  s0, sp, 16
    
    # Create closure environment
    jal   ra, mobius_alloc_env   # Allocate environment
    mv    t0, a0                 # Save environment pointer
    
    # Store 'start' parameter in closure environment
    sw    a0, 0(t0)              # env[0] = start
    
    # Create function object for 'counter'
    la    t1, counter_function   # Load counter function address
    jal   ra, mobius_create_function  # Create function with closure
    
    # Return function object
    mv    a0, a0                 # Function object in a0
    
    # Epilogue
    lw    ra, 12(sp)
    lw    s0, 8(sp)
    addi  sp, sp, 16
    jalr  zero, ra, 0

# Inner counter function (closure)
counter_function:
    # Prologue
    addi  sp, sp, -16
    sw    ra, 12(sp)
    sw    s0, 8(sp)
    addi  s0, sp, 16
    
    # Access closure environment
    lw    t0, 0(s3)              # Load closure environment
    lw    t1, 0(t0)              # Load 'start' value
    
    # Increment: start = start + 1
    addi  t1, t1, 1              # start + 1
    sw    t1, 0(t0)              # Store back to closure
    
    # Return incremented value
    mv    a0, t1                 # Return value
    
    # Epilogue
    lw    ra, 12(sp)
    lw    s0, 8(sp)
    addi  sp, sp, 16
    jalr  zero, ra, 0
```

## Runtime System Integration

### Mobius Runtime Functions (implemented in C):
```c
// Memory management
Value* mobius_alloc_value(void);
Array* mobius_alloc_array(size_t length);
Environment* mobius_alloc_env(void);

// String operations
RefCountedString* mobius_string_concat(RefCountedString* a, RefCountedString* b);

// Function management
MobiusFunction* mobius_create_function(uint32_t* code, Environment* closure);

// Type checking and conversion
bool mobius_check_type(Value* value, ValueType expected);
Value* mobius_convert_type(Value* value, ValueType target);

// Garbage collection
void mobius_gc_trigger(void);
void mobius_gc_mark_value(Value* value);

// Error handling
void mobius_runtime_error(const char* message);
void mobius_type_error(ValueType expected, ValueType actual);
```

## Performance Characteristics

### RISC-V Bytecode Benefits:
1. **Native Speed**: Can run directly on RISC-V processors
2. **Efficient Encoding**: 32-bit instructions, compact representation
3. **Register Allocation**: Efficient use of 32 RISC-V registers
4. **Pipeline Friendly**: RISC-V's simple instruction format
5. **Extensible**: Can add custom instructions for Mobius operations

### Compilation Modes:
1. **Interpreted Mode**: RISC-V VM running on any architecture
2. **JIT Mode**: Dynamic compilation to native RISC-V code
3. **AOT Mode**: Ahead-of-time compilation to RISC-V binaries
4. **Hybrid Mode**: Hot code compilation with interpreter fallback

This RISC-V bytecode system would make Mobius incredibly fast and suitable for embedded systems, IoT devices, and high-performance computing applications running on RISC-V processors.
