# `compression` Module

Import:

```mobius
import "compression"
```

[← Module reference](index.md)

The `compression` module provides a high-level Mobius API over native
compression/archive primitives.

Public API:

| Function | Description |
|---|---|
| `compression.inspect(path, options?)` | Inspect an archive or compressed stream and return metadata. |
| `compression.list(path, options?)` | List archive entries without extracting them. |
| `compression.extract(path, destination, options?)` | Extract an archive into a destination directory. |
| `compression.create(output_path, inputs, options?)` | Create an archive from files/directories. |
| `compression.compress(input_path, output_path, options?)` | Compress a single file stream. |
| `compression.decompress(input_path, output_path, options?)` | Decompress a single file stream. |

Supported formats:

- Archives: `zip`, `tar`
- Compressed streams: `gzip`, `zstd`
- Combined archive flows: `tar.gz`, `tar.zst`
- `.mz` is intended to be zip-based for package creation/install flows.

Useful options:

| Option | Meaning |
|---|---|
| `format` | Override format inference, such as `"gzip"`, `"zstd"`, `"zip"`, `"tar"`, `"tar.gz"`, or `"tar.zst"`. |
| `overwrite` | Output behavior: `"error"` (default) or `"replace"`. |
| `compression_level` | Compression level when supported by the selected backend. |
| `root_in_archive` | Prefix all created archive entries with a root directory. |
| `strip_components` | Drop leading path components during extraction. |

Example:

```mobius
import "compression"
import "os"

var input = os.tmpfile()
var gzip_output = input + ".gz"
var roundtrip = input + ".out"

writefile(input, "hello from compression")
compression.compress(input, gzip_output, { format: "gzip" })
var info = compression.inspect(gzip_output)
print(info.compression_format)

compression.decompress(gzip_output, roundtrip)
print(readfile(roundtrip))
```
