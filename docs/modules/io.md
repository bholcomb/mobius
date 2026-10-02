# `io` Module

Import:

```mobius
import "io"
```

[← Module reference](index.md)

The `io` module provides **streams**: open files and the standard streams,
read and written incrementally. Use it for files too large to hold in memory,
for reading standard input, and for writing to standard error. The global
`readfile`, `writefile`, `appendfile` and `readlines` remain the simplest way to
handle a whole file at once.

```mobius
import "io"

var out = io.open("report.txt", "w")
out:write("total: " + str(42) + "\n")
out:close()

var f = io.open("report.txt")
for (var line in f:lines()) {
    print(line)
}
f:close()
```

## Opening files

`io.open(path [, mode])` opens a file and returns a stream. The default mode is
`"r"`.

| Mode | Meaning |
|------|---------|
| `"r"` | Read. The file must exist. |
| `"w"` | Write. Creates the file, or truncates it if it exists. |
| `"a"` | Append. Creates the file if needed; every write goes to the end. |
| `"r+"` | Read and write. The file must exist. |
| `"w+"` | Read and write. Creates or truncates. |
| `"a+"` | Read anywhere, append writes. Creates if needed. |
| `"x"`, `"x+"` | Create a new file (write, or read and write); an error if it exists. |

Files are always opened in binary mode: bytes pass through unchanged on every
platform, with no newline translation. Mobius strings are bytes, so there is no
text encoding layer.

## Standard streams

`io.stdin`, `io.stdout` and `io.stderr` are streams for the process's standard
input, output and error.

```mobius
import "io"

for (var line in io.stdin:lines()) {
    io.stdout:write(upper(line) + "\n")
}
io.stderr:write("done\n")
```

`print` and `io.stdout` share one output buffer, so their output appears in
the order it was written. Closing a standard stream flushes it and marks the
stream object closed, but leaves the process's stream open, so `print` and
child processes keep working.

## Stream methods

| Method | Returns | Description |
|--------|---------|-------------|
| `s:read(n)` | buffer or `nil` | Up to `n` bytes (`n` must be positive); `nil` at end of file. |
| `s:read_into(buffer)` | int | Read up to the buffer's size into it; the count read, `0` at end of file. |
| `s:read_line()` | string or `nil` | The next line without its `"\n"` or `"\r\n"`; `nil` at end of file. An empty line is `""`, and a last line without a newline is still returned. |
| `s:lines()` | iterator | The remaining lines, for `for (var line in s:lines())`. |
| `s:read_all()` | buffer | Everything left (empty at end of file). |
| `s:read_text()` | string | Everything left, as a string (`""` at end of file). |
| `s:write(data)` | int | Write a string or buffer completely; the number of bytes written. |
| `s:flush()` | `nil` | Push buffered output to the operating system (not a guarantee that it reached the disk). |
| `s:seek(offset [, origin])` | int | Move to `offset` from `"set"` (default), `"cur"` or `"end"`; the new position. An error on streams that can't seek, such as pipes. |
| `s:tell()` | int | The current position. |
| `s:close()` | `nil` | Flush and release the stream. Closing again does nothing. |
| `s:is_closed()` | bool | Whether the stream is closed. |
| `s:name()` | string | The path, or `"<stdin>"`, `"<stdout>"`, `"<stderr>"`. |

Reads and writes are byte-exact: `read_line` keeps NUL bytes inside the line.
A stream opened for reading and writing (`"r+"`, `"w+"`, `"a+"`) can switch
between the two freely; the stream takes care of the flush or seek the C
library requires in between.

A stream that is no longer referenced is flushed and closed automatically, but
closing explicitly releases the file at a predictable point.

## Copying

`io.copy(source, destination)` copies everything remaining in `source` to
`destination` in fixed-size chunks, so memory use stays small however large the
data is. It returns the number of bytes copied.

```mobius
var src = io.open("input.bin")
var dst = io.open("output.bin", "w")
print(io.copy(src, dst), "bytes copied")
src:close()
dst:close()
```

## Errors

Failures raise an error that names the operation, the stream and the reason,
for example `io.open: config.toml: No such file or directory` or
`stream:read: data.csv: stream is closed`. Reading a stream opened only for
writing (or the reverse) is also an error.

## Fibers

A stream can be shared between fibers: each call is atomic, so two fibers
writing lines to one stream never interleave within a line. The order of calls
from different fibers is up to the scheduler. A call that waits on the
operating system (reading standard input, for example) blocks its worker
thread while it waits.
