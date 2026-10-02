# `cli` Module

Import:

```mobius
import "cli"
```

[← Module reference](index.md)

The `cli` module parses command-line arguments and generates help, in the style
of Python's `argparse`. Declare the arguments your tool takes, then parse
`argv`:

```mobius
import "cli"

var parser = cli.parser({
    name: "generate",
    description: "Generate source files from templates",
    version: "1.0.0"
})
parser:argument("input", {help: "Input schema"})
parser:option("--output", {short: "-o", default: "generated", help: "Output directory"})
parser:option("--jobs", {short: "-j", type: "int", default: 4})
parser:flag("--watch", {short: "-w", help: "Rebuild when inputs change"})

var args = parser:parse_or_exit(argv)
print(args.input, args.output, args.jobs, args.watch)
```

`generate --help` prints:

```
usage: generate [options] <input>

Generate source files from templates

arguments:
  input                     Input schema

options:
  -h, --help                Show this help and exit
      --version             Show the version and exit
  -o, --output OUTPUT       Output directory (default: generated)
  -j, --jobs JOBS           (default: 4)
  -w, --watch / --no-watch  Rebuild when inputs change
```

Help lists everything in the order it was declared.

## The parser

`cli.parser([options])` creates a parser. Options: `name` (shown in usage;
default `"program"`), `description`, `epilog` (text after the help), and
`version` (adds `--version`). Every parser has `-h`/`--help`.

Declaration methods return the parser, and all of them take an optional
options table as their last argument.

### `parser:argument(name [, options])`

A positional argument.

| Option | Description |
|--------|-------------|
| `help` | Description for the help text. |
| `type` | `"string"` (default), `"int"`, `"float"` or `"bool"`. |
| `choices` | An array of allowed values. |
| `nargs` | `"?"` optional (one value or none), `"*"` any number, `"+"` one or more. Without `nargs` the argument is required and takes one value. |
| `default` | The value of an optional (`"?"`) argument that was not given. |

`"*"` and `"+"` give an array. Values are assigned in order; an optional
argument is skipped if the values left are needed by required arguments after
it.

### `parser:option(name [, options])`

An option that takes a value: `--output dir`, `--output=dir`, `-o dir` or
`-odir`.

| Option | Description |
|--------|-------------|
| `short` | A one-letter alias such as `"-o"`. |
| `help`, `type`, `choices` | As for arguments. |
| `default` | The value when the option is not given (otherwise `nil`). |
| `required` | `true` to make the option mandatory. |
| `multiple` | `true` to allow repeating it: `-I a -I b` gives `["a", "b"]` (default `[]`). |
| `metavar` | The value's name in help (default: the option name in capitals). |
| `dest` | The result field name (default: the name without dashes, `-` replaced by `_`). |

`type: "bool"` accepts `true`/`false`, `yes`/`no`, `on`/`off` and `1`/`0`.

### `parser:flag(name [, options])`

An option without a value: `true` when given, otherwise `false`. `--no-NAME`
sets it to `false` (useful to override an earlier flag or a wrapper script).

| Option | Description |
|--------|-------------|
| `short`, `help`, `dest` | As for options. |
| `count` | `true` to count repetitions instead: `-vvv` gives `3` (default `0`). |
| `negatable` | `false` to leave out `--no-NAME`. |
| `default` | The value when not given. |

Short flags can be grouped: `-wv` is `-w -v`, and `-wj4` is `-w -j 4`.

### `parser:command(name [, options])`

A subcommand, as in `git commit`. Returns the command's own parser, on which
you declare its arguments. Options: `help` (shown in the parent's help) and
`description` (shown in the command's help).

```mobius
var git = cli.parser({name: "git"})
git:flag("--quiet", {short: "-q"})
var commit = git:command("commit", {help: "Record changes"})
commit:option("--message", {short: "-m", required: true})

var args = git:parse(["-q", "commit", "-m", "fix"])
print(args.command, args.quiet, args.message)    // commit true fix
```

The parent's options come before the command name. The chosen command's name
is `args.command`, and its arguments are in the same result table. A parser
with commands requires one, and can't also have positional arguments.

## Parsing

`parser:parse(argv)` returns a table with one field per declared argument,
option and flag. Undeclared options, missing or invalid values and unknown
commands raise a usage error, a table:

```mobius
try {
    var args = parser:parse(argv)
} catch e {
    print(e.message)     // e.g. "argument --jobs: invalid int value: 'many'"
    print(e.usage)       // the usage line
}
```

`--help` (or `-h`) and `--version` don't raise and don't exit: `parse` returns
`{help: true, text}` or `{version: true, text}` for you to show.

`parser:parse_or_exit(argv)` is for standalone tools: it prints help or the
version to standard output and exits with status 0, or prints the usage line
and the error to standard error and exits with status 2.

Details:

- `--` ends option parsing: everything after it is positional.
- An argument that looks like a negative number (`-5`, `-0.5`) is a value
  unless the parser has a short option with that name.
- Option names are matched exactly; prefixes such as `--out` for `--output`
  are not accepted.

## `parser:help()`

Returns the help text, the same text `--help` produces.

Mistakes in the declarations themselves (a name without `--`, an unknown type,
a duplicate name) raise a plain string error when the declaration runs.
