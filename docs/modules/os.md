# `os` Module

Import:

```mobius
import "os"
```

The `os` module contains cross-platform host integration for filesystem, paths,
environment variables, processes, and basic time conversion.

Selected constants:

- `os.platform`
- `os.separator`

Function groups:

| Group | Members |
|---|---|
| Environment | `getenv`, `setenv`, `unsetenv` |
| Working directory | `getcwd`, `chdir` |
| Process and shell | `sleep`, `system`, `exec`, `getpid`, `cpu_count`, `uname` |
| Directories and files | `listdir`, `mkdir`, `mkdirp`, `rmdir`, `remove`, `rename`, `cp`, `touch` |
| File metadata | `stat`, `chmod`, `filesize`, `exists`, `is_file`, `is_dir` |
| Path helpers | `basename`, `dirname`, `extname`, `join`, `realpath`, `tmpdir`, `tmpfile` |
| Tree walking | `glob`, `walkdir` |
| Time conversion | `time`, `gmtime`, `localtime`, `strftime`, `mktime` |

Notes:

- `os.join(...)` accepts one or more string path segments.
- `os.exec(...)` captures stdout as a string.
- `os.system(...)` returns the subprocess exit code.

Example:

```mobius
import "os"

var path = os.join(os.tmpdir(), "mobius", "config.json")
print(path)

var stamp = os.strftime("%Y-%m-%d", os.time())
print(stamp)
```
