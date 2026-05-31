# Mobius Examples

This directory demonstrates how to use and extend Mobius — embedding it in C/C++
hosts, writing native extensions, and writing pure-Mobius scripts.

The full documentation lives in [`../docs/`](../docs/index.md). See especially
the [Embedding Guide](../docs/embedding/embedding-guide.md) and
[Plugin Guide](../docs/embedding/plugin-guide.md).

## Building and running

Everything here is part of the Buildy workspace, so a single build compiles the
interpreter, the library, the bundled modules, and these examples:

```bash
./buildy -r
```

Compiled C/C++ examples are produced under `build/<platform>-release/bin/` and
staged into `bin/`:

```bash
bin/simple_embedding
bin/embedding_example
bin/game_engine
bin/multi_environment_demo
bin/simple_userdata_test
bin/stack_api_test
```

Run script examples with the interpreter (from the project root, so `import`
finds the bundled modules in `bin/modules/`):

```bash
bin/mobius examples/demo_scripts/entity_ai.mob
bin/mobius examples/networking/http_rest_site.mob
bin/mobius examples/performance_tests/performance_test_current.mob
```

## Embedding examples (C/C++)

| Example | File | Shows |
|---------|------|-------|
| **simple_embedding** | `simple_embedding/simple_embedding.cpp` | The minimal create → init stdlib → exec → free flow |
| **embedding_example** | `embedding_example/embedding_example.cpp` | Value exchange, custom native functions, error handling |
| **stack_api_test** | `stack_api_test/stack_api_test.cpp` | Driving the value stack directly from C |
| **game_engine** | `game_engine/game_engine.cpp` + `game_init.mob` | A host application scripted by Mobius |
| **multi_environment_demo** | `multi_environment_demo/multi_environment_demo.cpp` | Running multiple independent `MobiusState` instances |

## Extension examples

| Example | File | Shows |
|---------|------|-------|
| **text_processing_plugin** | `text_processing_plugin/text_processing_plugin.cpp` | A native plugin module (`mobius_plugin_info`) with string utilities |
| **simple_userdata_test** | `simple_userdata_test/simple_userdata_test.cpp` | Passing C pointers into scripts as userdata |
| **cpp_class_example** | `cpp_class_example/cpp_class_example.cpp` | Binding a C++ class to a Mobius userdata type |

## Script examples

| Path | Shows |
|------|-------|
| `demo_scripts/entity_ai.mob` | State machines and behavior logic |
| `demo_scripts/complete_graphics_demo.mob` | A larger feature-driven demo |
| `demo_scripts/multi_script_demo.mob` + `utils.mob` | Splitting a program across files with `load` |
| `demo_scripts/riscv_compilation_demo.mob` | Type specialization on numeric hot paths |
| `networking/http_hello_server.mob` | A minimal dynamic HTTP page |
| `networking/http_rest_site.mob` | A REST API + server-rendered HTML app |
| `networking/web_hello_server.mob` | The `web` module's router/middleware framework |
| `networking/websocket_echo_server.mob` | A plain WebSocket echo server |
| `performance_tests/` | Micro-benchmarks for the interpreter |

## Tips

- Always check return codes from the Mobius C API (`MOBIUS_OK` on success).
- Free what you allocate; attach destructors to userdata you push.
- Validate argument count and types in native functions, and report failures
  with `mobius_error()`.
- For an authoritative list of API functions, see
  [`include/mobius/mobius.h`](../include/mobius/mobius.h) and
  [`include/mobius/mobius_plugin.h`](../include/mobius/mobius_plugin.h).
