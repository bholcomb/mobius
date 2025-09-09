# Mobius Enum Reference

## Overview

Enums (enumerations) in Mobius allow you to define a set of named constants with optional underlying integer types. They provide type safety and improve code readability.

## Basic Syntax

### Simple Enum Declaration
```mobius
enum EnumName {
    MEMBER1,
    MEMBER2,
    MEMBER3
}
```

### Enum with Explicit Type
```mobius
enum EnumName : underlying_type {
    MEMBER1,
    MEMBER2,
    MEMBER3
}
```

### Supported Underlying Types
- `int8` - 8-bit signed integer (-128 to 127)
- `uint8` - 8-bit unsigned integer (0 to 255)
- `int16` - 16-bit signed integer (-32,768 to 32,767)
- `uint16` - 16-bit unsigned integer (0 to 65,535)
- `int32` - 32-bit signed integer (default if no type specified)
- `uint32` - 32-bit unsigned integer
- `int64` - 64-bit signed integer
- `uint64` - 64-bit unsigned integer

## Value Assignment

### Auto-Incremented Values
```mobius
enum Color {
    RED,    // 0
    GREEN,  // 1
    BLUE    // 2
}
```

### Explicit Values
```mobius
enum HttpStatus : uint16 {
    OK = 200,
    NOT_FOUND = 404,
    SERVER_ERROR = 500
}
```

### Mixed Values
```mobius
enum Priority {
    LOW,        // 0
    MEDIUM = 5, // 5
    HIGH        // 6 (auto-increment from previous)
}
```

## Usage

### Accessing Enum Members
```mobius
var color = Color.RED;
var status = HttpStatus.OK;
```

### Comparisons
```mobius
if (color == Color.RED) {
    print("It's red!");
}

if (status != HttpStatus.OK) {
    print("Error occurred");
}
```

### Switch Statements
```mobius
switch (color) {
    case Color.RED:
        print("Red color");
        break;
    case Color.GREEN:
        print("Green color");
        break;
    case Color.BLUE:
        print("Blue color");
        break;
    default:
        print("Unknown color");
}
```

### Function Parameters
```mobius
func processStatus(status) {
    switch (status) {
        case HttpStatus.OK:
            return "Success";
        case HttpStatus.NOT_FOUND:
            return "Not found";
        case HttpStatus.SERVER_ERROR:
            return "Server error";
        default:
            return "Unknown status";
    }
}

var result = processStatus(HttpStatus.OK);
```

## Advanced Examples

### Real-World Usage
```mobius
enum LogLevel : uint8 {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
}

enum FilePermission : uint8 {
    READ = 1,
    WRITE = 2,
    EXECUTE = 4,
    ALL = 7  // READ | WRITE | EXECUTE
}

func log(level, message) {
    var prefix;
    switch (level) {
        case LogLevel.DEBUG:
            prefix = "[DEBUG]";
            break;
        case LogLevel.INFO:
            prefix = "[INFO]";
            break;
        case LogLevel.WARN:
            prefix = "[WARN]";
            break;
        case LogLevel.ERROR:
            prefix = "[ERROR]";
            break;
    }
    print(prefix, message);
}

// Usage
log(LogLevel.INFO, "Application started");
log(LogLevel.ERROR, "Database connection failed");
```

### State Machines
```mobius
enum State {
    IDLE,
    RUNNING,
    PAUSED,
    STOPPED
}

var currentState = State.IDLE;

func transition(newState) {
    switch (currentState) {
        case State.IDLE:
            if (newState == State.RUNNING) {
                currentState = newState;
                print("Started");
            }
            break;
        case State.RUNNING:
            if (newState == State.PAUSED || newState == State.STOPPED) {
                currentState = newState;
                print("State changed to", newState);
            }
            break;
        case State.PAUSED:
            if (newState == State.RUNNING || newState == State.STOPPED) {
                currentState = newState;
                print("State changed to", newState);
            }
            break;
    }
}
```

## Best Practices

1. **Use descriptive names**: `HttpStatus.NOT_FOUND` is better than `Status.NF`
2. **Choose appropriate underlying types**: Use smaller types when possible to save memory
3. **Group related constants**: Create separate enums for different domains
4. **Use explicit values for stable APIs**: When values need to be stable across versions
5. **Prefer auto-increment for internal use**: Let the compiler assign values when order doesn't matter

## Memory Management

Enums in Mobius use reference counting for memory management:
- Enum definitions are reference counted
- Enum values automatically manage their definition references
- No manual memory management required

## Error Handling

- Accessing undefined enum members results in runtime errors
- Accessing members from undefined enums results in runtime errors
- The interpreter provides helpful error messages with suggestions

## Limitations

1. Enum member values must be compile-time constants
2. Enum member names must be unique within each enum
3. Enum members cannot reference other enum members in their definitions (currently)
4. Bytecode VM support is pending implementation

## Implementation Status

- ✅ **AST Mode**: Fully implemented and tested
- ⏳ **Bytecode Mode**: Pending implementation
- ✅ **Parser**: Complete syntax support
- ✅ **Type System**: Full integration
- ✅ **Switch Statements**: Pattern matching support
- ✅ **Memory Management**: Reference counting
