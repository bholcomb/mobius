# Simple Embedding Example

A minimal example showing how to embed Mobius in a C++ application.

## Files

- `simple_embedding.cpp` - Minimal application with embedded Mobius interpreter

## Features Demonstrated

- **Basic Embedding** — creating and initializing a Mobius interpreter state
- **Script Execution** — running Mobius scripts from C strings
- **Error Handling** — checking return codes
- **Resource Management** — proper cleanup with `mobius_free_state`

## Headers Used

```cpp
#include <mobius/mobius.h>   // state lifecycle, execution, config
```

## Code Overview

```cpp
MobiusState* state = mobius_new_state(NULL);
mobius_init_stdlib(state);

int result = mobius_exec_string(state, "print(\"Hello from Mobius!\")");
if (result != MOBIUS_OK) {
    printf("Script execution failed!\n");
}

mobius_free_state(state);
```

## Building and Running

```bash
./buildy -r -n 3
./bin/simple_embedding
```

## Next Steps

After understanding this example, explore:

- [Embedding Example](../embedding_example/) — value exchange, custom functions,
  error handling
- [Game Engine Example](../game_engine/) — full game scripting integration
- [Multi Environment Demo](../multi_environment_demo/) — multiple interpreter
  instances
