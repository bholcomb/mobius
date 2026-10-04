# `process` Module

Import:

```mobius
import "process"
```

[← Module reference](index.md)

The `process` module runs other programs. Programs run **directly**, with their
arguments passed exactly as given, not through a shell. You choose what happens
to each standard stream, and can capture output, feed input, set the
environment and working directory, and stop a program that runs too long.

(`os.system` and `os.exec` remain for quick shell commands.)

On Windows there are no signals: `kill` (any signal) and `timeout_ms` end the
program at once, with exit code 1. `shell: true` runs the command with
`cmd.exe /c`.

```mobius
import "process"

var result = process.run(["git", "status", "--short"], {cwd: project_dir})
if (result.exit_code == 0) {
    print(result.stdout)
} else {
    print("git failed:", result.stderr)
}
```

## `process.run(args [, options])`

Runs a program to completion and returns a result table:

| Field | Description |
|-------|-------------|
| `exit_code` | The exit status, or `-N` if the program was stopped by signal `N` (`-15` for SIGTERM, `-9` for SIGKILL). |
| `stdout` | Captured output as a string (a buffer with `binary: true`), or `nil` if not captured. |
| `stderr` | Captured error output, likewise. |
| `timed_out` | `true` if `timeout_ms` passed and the program was stopped. |

`args` is an array of strings: the program, then its arguments. A program name
without a `/` is looked up in the `PATH` the program will see. Each argument is
passed exactly as written: spaces, quotes, `$` and `*` have no special meaning.

A program that runs and exits with a nonzero status is a normal result, not an
error; check `exit_code`, or pass `check: true` to make it an error. Failing to
start the program at all (not found, not executable, a missing `cwd`) is
always an error.

By default `run` captures stdout and stderr, and the program shares the
script's standard input. Output is read and input written together, so large
amounts of both never deadlock.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `stdin`, `stdout`, `stderr` | see below | Where each stream goes; see [Streams](#streams). |
| `input` | none | A string or buffer to write to the program's standard input, which is then closed. Makes `stdin` a pipe. |
| `binary` | `false` | Return captured output as buffers instead of strings. |
| `check` | `false` | Raise an error if the program exits nonzero or times out. |
| `timeout_ms` | none | Stop the program after this many milliseconds: SIGTERM, then SIGKILL a second later if it is still running. Output read so far is kept. Only the program itself is signaled, not processes it started. |
| `cwd` | current | Working directory for the program. |
| `env` | none | Variables to set, as a table of strings. They are added to the script's environment; a `nil` value removes a variable. |
| `env_clear` | `false` | Start from an empty environment, so the program sees only `env`. |
| `shell` | `false` | Run `args` (a single string) with `/bin/sh -c` (`cmd.exe /c` on Windows). Only when you need shell features: quoting is then your responsibility. |

## Streams

Each of `stdin`, `stdout` and `stderr` can be:

| Setting | Meaning |
|---------|---------|
| `"inherit"` | Share the script's stream. |
| `"capture"` or `"pipe"` | Connect a pipe: `run` captures it into the result; `start` gives you an `io` stream. |
| `"null"` | Discard (or, for stdin, read nothing). |
| `{file: path}` | Read from a file (stdin), or write to it, replacing it (stdout, stderr). |
| `{file: path, append: true}` | Append output to a file. |
| an `io` stream | Use an open stream from the [`io` module](io.md). Anything already written to it is flushed first, so output stays in order. |
| `"stdout"` | (stderr only) Send error output to wherever stdout goes. |

`run` defaults to capturing stdout and stderr and inheriting stdin. `start`
inherits all three.

```mobius
// Errors and output in one log file:
process.run(["make", "all"], {stdout: {file: "build.log"}, stderr: "stdout"})

// Feed input, read binary output:
var png = process.run(["convert", "-", "png:-"], {input: svg_text, binary: true}).stdout
```

## `process.start(args [, options])`

Starts a program and returns immediately with a **child** object. It takes the
same options as `run` except `input`, `binary`, `check` and `timeout_ms`.

| Member | Description |
|--------|-------------|
| `child.pid` | The process id. |
| `child.stdin`, `child.stdout`, `child.stderr` | `io` streams for streams set to `"pipe"`, otherwise `nil`. |
| `child:wait([timeout_ms])` | Wait for the program to exit and return its exit code; with a timeout, `nil` if it is still running when the time is up. |
| `child:poll()` | The exit code if it has finished, otherwise `nil`. Does not wait. |
| `child:kill([signal])` | Send `"term"` (default), `"kill"`, `"int"` or `"hup"`. `false` if it had already exited. |
| `child:communicate([input [, timeout_ms]])` | Write `input` to stdin (if piped) and close it, read stdout and stderr (if piped) to the end, wait for the exit. Returns a result like `run`. The pipe streams are used up (closed) by this. |

```mobius
var sorter = process.start(["sort"], {stdin: "pipe", stdout: "pipe"})
for (var name in names) {
    sorter.stdin:write(name + "\n")
}
sorter.stdin:close()          // EOF: sort can finish
for (var line in sorter.stdout:lines()) {
    print(line)
}
sorter:wait()
```

When you use the pipe streams directly, close stdin when you are done writing,
so the program sees end of input; and read its output while it runs, or a
program writing a lot can fill the pipe and wait for you. `communicate` does
both for you.

A child object that is dropped while its program is still running does not
stop the program.

## Fibers

`run`, `wait` and `communicate` wait without holding a thread: a fiber waiting
on a child is set aside until the child writes, reads or exits, and other
fibers run meanwhile. Many fibers can each run and wait on their own child at
once. Pipe streams (`child.stdout` and the others) wait the same way, as
described in [io](io.md#fibers).

`fiber.cancel` interrupts a fiber waiting in `run`, `wait` or `communicate`
with a `CancellationError`. A child started by `run` is then killed, because
nothing else can reach it. A child from `start` keeps running; the script still
holds it and can `kill` it.
