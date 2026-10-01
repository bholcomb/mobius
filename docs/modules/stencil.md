# `stencil` Package

```mobius
import "stencil"
```

[← Module reference](index.md)

Text templates for Mobius: HTML pages, emails, config files, generated code.
Templates use Mobius expression syntax, support layouts and reusable
components, and treat mistakes as errors with a file, line, and column
instead of rendering empty strings. `stencil` is a **package** written in
pure Mobius. It must be installed into a `modules/` root (see
[Modules and Packages](../guide/modules-and-packages.md#packages)) before
`import "stencil"` will resolve.

```mobius
import "stencil"

var env = stencil.env({ root: "templates", escape: stencil.escape_html })
print(env:render("user.html", { user: { name: "Bob" }, items: ["a", "b"] }))

print(stencil.render_string("Hello {{ name:upper() }}!", { name: "bob" }))   // Hello BOB!
```

---

## Syntax

| Delimiter    | Meaning                         |
|--------------|---------------------------------|
| `{{ expr }}` | Output the value of `expr`      |
| `{% tag %}`  | A control tag                   |
| `{# ... #}`  | A comment; produces no output   |

**Whitespace.** A line that contains only one `{% %}` tag or `{# #}` comment
(plus spaces or tabs) is removed entirely, newline included. Block structure
therefore leaves no blank lines behind:

```
<ul>
{% for x in xs %}
  <li>{{ x }}</li>
{% endfor %}
</ul>
```

renders as just the `<ul>`, the `<li>` lines, and `</ul>`. `{{ }}` output is
never removed this way. For finer control, a `-` just inside a delimiter
(`{{-`, `-}}`, `{%-`, `-%}`, `{#-`, `-#}`) trims all whitespace on that side,
newlines included: `a {{- x -}} b` renders `a` and `b` with no space around
the value.

---

## Expressions

Expressions are a subset of Mobius expressions and behave the same way:

| Form | Notes |
|------|-------|
| `nil`, `true`, `false`, numbers, `"strings"` | Strings use double quotes; `\n`, `\t`, `\"`, `\\` escapes work |
| `[a, b]`, `{ key: value, ["k"] = v }` | Array and table literals |
| `name`, `a.b`, `a[expr]` | Lookups; missing names, fields, and indexes are errors (see [Strict undefined](#strict-undefined)) |
| `f(args)` | Call a function passed in the data or registered as a global |
| `x:filter(args)` | Call a [filter](#filters) |
| `-x`, `not x`, `!x` | |
| `*` `/` `%` `+` `-` | `int / int` is integer division; `+` concatenates when either side is a string |
| `<` `<=` `>` `>=` `==` `!=` | |
| `and` `&&` `or` `\|\|` | Short-circuit, returning an operand |
| `cond ? a : b` | Only the chosen branch is evaluated |

Only `false` and `nil` are falsy, so `0` and `""` count as true.

Templates read data but don't change it, so assignment, `++`/`--`, bitwise
operators, function literals, and `spawn`/`await` are not available.
`{% set %}` is the way to bind a name. A few mistakes get specific errors:
`x | upper` points you to `x:upper()`, and `'text'` to double quotes.

The only built-in function is `range`: `range(n)` returns `[0, n)` and
`range(a, b)` returns `[a, b)`.

---

## Filters

A filter is called like a method, with no spaces around the `:`:
`{{ title:upper() }}`, `{{ text:truncate(40, "…") }}`. Filters chain:
`{{ name:trim():lower() }}`.

A spaced `:` is never a filter. Inside a ternary it belongs to the ternary,
so `{{ ok ? a : f(x) }}` chooses between `a` and `f(x)`, while
`{{ ok ? a:upper() : b }}` filters `a`. Anywhere else `x : upper()` is an
error.

`x:name()` always calls a filter, never a method of the data, so templates
can't call `items:push()` or similar. An unknown filter name is an error.

| Group | Filters |
|-------|---------|
| Strings | `upper` `lower` `trim` `capitalize` `title` `replace(old, new)` `truncate(n [, suffix])` `repeat(n)` `split(sep)` `startswith(s)` `endswith(s)` |
| Case | `pascal` `camel` `snake` `kebab` |
| Collections | `size` `first` `last` `reverse` `sort` `keys` `join([sep])` `contains(x)` |
| Missing values | `default(fallback)` `defined()` |
| Escaping | `escape` `escape_url` `json([indent])` `raw` |
| Conversion | `str` `int` `float` |

- **Case filters** split words on non-alphanumeric characters and case
  changes: `"parseHTTPServer":snake()` is `parse_http_server`, and
  `"hello world":pascal()` is `HelloWorld`.
- **`truncate`** counts bytes but never cuts a UTF-8 character in half; the
  default suffix is `...`.
- **`reverse` and `sort`** return copies and leave the original alone.
  `keys` returns a table's keys sorted.
- **`json`** sorts table keys, so output is stable from run to run.

**Custom filters** are ordinary functions that receive the value first:

```mobius
env:filter("money", func(v) { return "$" + str(v) })
env:filter("wrap", func(v, left, right) { return left + v + right })
```

```
{{ price:money() }}  {{ name:wrap("[", "]") }}
```

A filter is called with exactly the arguments the template passes, so the
function's parameter count must match. `raw`, `default`, and `defined` can't
be replaced.

---

## Strict undefined

Stencil treats these as errors instead of printing nothing:

- an undefined variable;
- a missing field (`user.nmae`) or out-of-range index;
- outputting `nil`, a table, or an array with `{{ }}`. Use `:default()`,
  `:json()`, or `:join()` to say what you want.

To allow a value to be missing, use `default` or `defined`. For these two
filters a failed lookup counts as `nil` instead of an error:

```
{{ user.nickname:default(user.name) }}
{% if user.admin:default(false) %}admin{% endif %}
{% if user.avatar:defined() %}<img src="{{ user.avatar }}">{% endif %}
```

---

## Tags

### if

```
{% if score > 90 %}A{% elif score > 75 %}B{% else %}C{% endif %}
```

### for

```
{% for item in items %}
  {{ loop.number }}. {{ item }}
{% else %}
  Nothing here.
{% endfor %}
```

| Form | Iterates |
|------|----------|
| `for x in array` | elements in order |
| `for i, x in array` | index and element |
| `for k in table` | keys |
| `for k, v in table` | keys and values |

Tables are iterated in **sorted key order**, because Mobius table order is
randomized per process and a template should render the same way every
time. The `else` branch runs when the collection is empty.

Inside the loop, `loop` holds:

| Field | Meaning |
|-------|---------|
| `loop.index` | 0-based position |
| `loop.number` | 1-based position |
| `loop.first` | `true` on the first iteration |
| `loop.last` | `true` on the last iteration |
| `loop.length` | number of items |

### set

```
{% set total = 0 %}
{% for item in cart %}{% set total = total + item.price %}{% endfor %}
Total: {{ total }}
```

`set` updates the nearest existing variable with that name, or creates a new
one in the current block. `if` and `for` bodies are blocks, as in Mobius, so
a variable first created inside one is gone after it. Set it before the
block to use it afterwards, as above. The render data is never modified;
setting a name that came from the data creates a new variable that hides it.

### raw

```
{% raw %}{{ this is output literally }}{% endraw %}
```

### include

```
{% include "partials/nav.html" %}
```

The included template sees the current variables. Variables it sets stay
local to it.

### extends and block

A layout defines blocks; a page extends it and replaces some of them:

```
{# base.html #}
<title>{% block title %}My Site{% endblock %}</title>
<main>{% block body %}{% endblock %}</main>
```

```
{# page.html #}
{% extends "base.html" %}
{% block title %}About · {{ super() }}{% endblock %}
{% block body %}<p>Hello!</p>{% endblock %}
```

- `extends` must come first in the template.
- At its top level, a template that extends another may contain only
  blocks, components, and imports. Anything else is an error rather than
  output that silently disappears.
- Layouts can extend other layouts. The most specific version of each block
  wins, and `{{ super() }}` renders the version it replaces.
- Overriding a block that no parent defines (for example a misspelled
  `{% block titel %}`) is an error.

---

## Components

Components are reusable pieces of markup with parameters and slots.

```
{# ui.html #}
{% component card(title, tone = "plain") %}
<div class="card {{ tone }}">
  <h2>{{ title }}</h2>
  {% slot body %}{% endslot %}
  {% slot footer %}<small>No footer</small>{% endslot %}
</div>
{% endcomponent %}
```

```
{# page.html #}
{% import "ui.html" %}
{% use card({ title: "Welcome" }) %}
  {% fill body %}<p>Hello, {{ user.name }}.</p>{% endfill %}
{% enduse %}
```

**Defining.** `{% component name(params) %}` goes at the top level of any
template. A definition produces no output and can be used anywhere in that
template, before or after it. Parameters can have defaults, which may refer
to earlier parameters. With no parameters the parentheses are optional.

**Using.** `{% use name(args) %}` passes its arguments as one table, since
Mobius has no named arguments. A missing argument with no default, or an
argument the component doesn't declare, is an error.

**Scope.** A component's body sees only its parameters and the env's
globals, never the caller's variables, so the same component behaves the
same everywhere.

**Slots.**

- `{% slot name %}default{% endslot %}` declares a slot with default
  content. `{% slot %}` with no name declares the slot called `default`.
- In a `use` body, each `{% fill name %}` fills one slot. Fills run in the
  *caller's* scope, so they can use the caller's variables (like `user`
  above).
- A `use` body with no `fill` tags fills the `default` slot:
  `{% use box %}Hello{% enduse %}`. Mixing loose content with `fill` tags is
  an error.
- A slot that isn't filled renders its default content, and filling a slot
  the component doesn't declare is an error.

**Imports.** `{% import "file" %}` (top level only) makes the components
defined in that file available. Components defined in the template itself
take priority over imported ones, a name defined in two imported files is
an error, and imports don't carry over: importing `a.html` doesn't give you
what `a.html` imports.

Components can use other components and can pass a slot through to a
component they use. Jinja's `macro` and `call` tags are not supported;
components replace them.

---

## Escaping

Nothing is escaped unless you ask:

| Filter | Function | Escapes for |
|--------|----------|-------------|
| `escape` | `stencil.escape_html(s)` | HTML text and attribute values (`&`, `<`, `>`, `"`, `'`) |
| `escape_url` | `stencil.escape_url(s)` | URL components (percent-encoding) |
| `json` | | JavaScript and JSON contexts |

For HTML it's usually better to escape everything by default. Pass an
escaper when creating the env and every `{{ }}` output goes through it,
except values marked with `:raw()`:

```mobius
var env = stencil.env({ root: "templates", escape: stencil.escape_html })
```

```
{{ comment.text }}           {# escaped #}
{{ trusted_html:raw() }}     {# not escaped #}
```

Any function that takes and returns a string can be the escaper.
Component output and `super()` are already escaped and aren't escaped
twice.

---

## Errors

Every stencil error is thrown as a table:

| Field | Contents |
|-------|----------|
| `message` | The full error, ready to print, e.g. `page.html:12:5: undefined variable 'usr'` followed by the include/component/extends chain that led there |
| `reason` | The message without the location |
| `template` | The template name |
| `line`, `col` | 1-based position |
| `trace` | Array of "included from …" style entries, innermost first |
| `stencil` | Always `true` |

```mobius
try {
    print(env:render("page.html", data))
} catch err {
    print(stencil.format_error(err))
}
```

`stencil.format_error(err)` returns `err.message` for a stencil error and
`str(err)` for anything else.

### Checking a template directory

`env:check()` loads every file under `root` (so keep only templates there)
and returns an array of error tables, empty when everything is fine. It reports all problems at once:

- syntax errors and unknown filters;
- `include`, `extends`, and `import` targets that don't exist;
- block overrides that no parent defines;
- `use` of an unknown component, fills of slots the component doesn't
  declare, and missing or unknown arguments.

Targets and component arguments are only checked when they're written
literally (`{% include "nav.html" %}`, `{% use card({ title: "x" }) %}`);
anything computed is checked when it renders. Register custom filters
before calling `check()`. It's meant for startup or a test:

```mobius
var problems = env:check()
for (var e in problems) {
    print(stencil.format_error(e))
}
if (size(problems) > 0) { exit(1) }
```

---

## Environment

```mobius
var env = stencil.env({
    root: "templates",            // directory templates are loaded from
    escape: stencil.escape_html,  // default escaper (optional)
    globals: { site: "Example" }, // visible in every template (optional)
    cache: true,                  // keep parsed templates (default true)
    reload: true                  // re-parse edited files (default true)
})
```

All options are optional; `stencil.env(nil)` gives an env with no root,
usable with `from_string`.

| Method | Description |
|--------|-------------|
| `env:render(name, data)` | Load (or reuse) the template and render it with `data` |
| `env:get(name)` | Return the template object; render it with `tpl:render(data)` |
| `env:from_string(src, name)` | Build a template from a string; `name` is used in error messages |
| `env:filter(name, fn)` | Register a custom filter |
| `env:global(name, value)` | Add a global variable |
| `env:check()` | Validate every template under `root` (see above) |

| Function | Description |
|----------|-------------|
| `stencil.render_string(src, data)` | Render a string with default options |
| `stencil.escape_html(s)` | HTML-escape a string |
| `stencil.escape_url(s)` | Percent-encode a string |
| `stencil.format_error(err)` | Printable text for an error |

Names are looked up in this order: variables set in the template (including
loop variables), then the render data, then globals.

**Template names** are paths relative to `root`. Absolute paths and `..` are
rejected, so templates can't reach outside their directory.

**Reloading.** With `reload` on (the default), editing a template takes
effect on the next render without restarting. That includes included files,
layouts, and component files. Each load compares the file's modification
time (to the second) with the cached copy and re-parses only when it
changed. Within one render each file is checked once, so the cost is small.
Turn it off with `reload: false` if the templates never change while the
program runs.

---

## Using stencil with `web`

The [`web`](web.md) module renders through any object with a
`render(name, data)` method, and a stencil env is one:

```mobius
import "web"
import "stencil"

var app = web.app()
app:templates(stencil.env({ root: "templates", escape: stencil.escape_html }))

app:get("/users/:id", func(ctx) {
    return ctx:render("user.html", { id: ctx:param("id") })
})
```

`web` handles each connection on a worker fiber with its own copy of the
app, so each worker keeps its own template cache.

---

## Performance

Templates are parsed once and cached; each render walks the parsed
template. As a rough guide, a page with a layout, an imported component,
and a table renders in about 0.3 ms for 10 rows and about 28 ms for 1,000
rows.
