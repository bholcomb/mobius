# Stencil — Design

Stencil is a text templating package for Mobius, inspired by Jinja but not a
reimplementation of it. It is written in pure Mobius (no native plugin), and
it optimizes for **correctness and predictability**, not speed.

```mobius
import "stencil"

var env = stencil.env({ root: "templates", escape: stencil.escape_html })
print(env:render("user.html", { user: u, items: rows }))

stencil.render_string("Hello {{ name:upper() }}!", { name: "bob" })   // "Hello BOB!"
```

## Goals

1. **Correct by default.** Mistakes are errors with a file, line and column,
   never silently empty output.
2. **One expression language.** Template expressions are a subset of Mobius
   expressions. There is no third syntax to learn.
3. **Few concepts.** Tags, filters, inheritance and components. Everything
   else is left out.
4. **Predictable whitespace and escaping.** Each is a single rule you can
   explain in one sentence.

## Non-goals

- Speed. Templates are parsed to an AST and tree-walked on every render.
- Context-aware (HTML/attribute/URL/JS) automatic escaping. It is too easy to
  get wrong in ways users can't work around. Escaping is explicit, plus one
  optional default (see below).
- Sandboxing untrusted template authors.
- Jinja compatibility: Python-isms, `is` tests, `call` blocks, i18n, async.

---

## Syntax

| Delimiter    | Meaning                         |
|--------------|---------------------------------|
| `{{ expr }}` | Output the value of `expr`      |
| `{% tag %}`  | Control tag                     |
| `{# ... #}`  | Comment (produces no output)    |

A `-` just inside a delimiter (`{{-`, `-}}`, `{%-`, `-%}`, `{#-`, `-#}`) trims
all whitespace on that side, newlines included.

### Whitespace rule

> A line that contains only a single tag or comment (plus surrounding spaces
> or tabs) is removed entirely, including its newline.

This is Mustache's "standalone tag" rule. It makes block structure invisible
in the output without Jinja's `trim_blocks`/`lstrip_blocks` options:

```
<ul>
{% for x in xs %}
  <li>{{ x }}</li>
{% endfor %}
</ul>
```

renders with no blank lines. `{{ }}` output tags are never standalone, and
`-` markers still work when you need more control.

### Expressions

Expressions are a strict subset of Mobius expression syntax, with Mobius
semantics:

| Supported                                 | Notes                                          |
|-------------------------------------------|------------------------------------------------|
| `nil` `true` `false`, ints, floats, `"strings"` | Strings use double quotes, as in Mobius  |
| `[a, b]`, `{ key: value }`                | Array and table literals                       |
| `name`, `a.b`, `a[expr]`                  | Lookup (strict, see below)                     |
| `f(args)`                                 | Call a function from the data or globals       |
| `x:filter(args)`                          | Filter call (see below)                        |
| `-x` `not x` `!x`                         |                                                |
| `*` `/` `%` `+` `-`                       | `int / int` is integer division, as in Mobius; `+` concatenates if either side is a string |
| `<` `<=` `>` `>=` `==` `!=`               |                                                |
| `and` `&&` `or` `\|\|`                    | Short-circuit; return an operand, as in Mobius |
| `cond ? a : b`                            | Lazy and right-associative, as in Mobius       |

Left out on purpose: assignment, `++`/`--`, bitwise operators, `spawn` /
`await` / `atomic`, and function literals. Templates read data; they don't
mutate it or start fibers. `{% set %}` is the only way to bind a name.

Mobius has no optional chaining and no named arguments, so neither do
templates. Use `:default()` and table arguments instead.

Spacing tells filters and ternaries apart, as in Mobius: a filter is written
without spaces around the `:` (`x:upper()`), and a spaced `:` in a ternary's
true branch belongs to the ternary. So `ok ? a : f(x)` is a ternary and
`ok ? a:upper() : b` filters `a`. A spaced filter call such as `x : upper()`
is an error.

Truthiness follows Mobius: only `false` and `nil` are falsy, so `0` and `""`
are true.

The only built-in global is `range(n)` / `range(a, b)`, which returns the
array `[a, b)`.

### Filters use method syntax

`x:name(a, b)`, written without spaces around the `:`, always means "call the
filter `name` with `(x, a, b)`". It
never calls a method on the data itself, so templates can't call `items:push()`.
Filters come from the built-in set plus anything registered with
`env:filter(name, fn)`. An unknown filter name is an error.

### Strict undefined

- An undefined variable is an error.
- `a.b` where `a` has no `b` is an error, and so is an index out of range.
- Outputting `nil`, a table or an array with `{{ }}` is an error. Use
  `:default()`, `:json()` or `:join()` to say what you want.

The escape hatches are `x:default(fallback)` and `x:defined()`. These two
filters receive the receiver "softly": a failed lookup becomes `nil` instead
of an error.

```
{{ user.nickname:default(user.name) }}
{% if user.admin:default(false) %}...{% endif %}
```

---

## Tags

### if

```
{% if expr %} ... {% elif expr %} ... {% else %} ... {% endif %}
```

### for

```
{% for item in items %} ... {% else %} (empty) {% endfor %}
{% for key, value in table %} ... {% endfor %}
{% for i, item in items %} ... {% endfor %}
```

- **Arrays** iterate in order.
- **Tables** iterate in **sorted key order**. Mobius table order is randomized
  per process, and templates must render deterministically. With one loop
  variable, a table yields its keys, as `for (var k in tbl)` does in Mobius.
- **Two variables over an array** give the index and the element.
- **`loop`:** inside the body, `loop.index` (0-based, like Mobius),
  `loop.number` (1-based), `loop.first`, `loop.last` and `loop.length` are
  available.
- **`else`** runs when the collection is empty.

### set

```
{% set total = total + item.price %}
```

`set` assigns to the nearest enclosing binding of that name, or declares it
in the current block if none exists. This is block scoping, like Mobius `var`.
Unlike Jinja, a `set` inside a loop *can* update a variable declared outside
it, with no `namespace()` workaround. Because `if` and `for` bodies are
blocks, a name first set inside one is gone after it; set it before the
block to use it afterwards. The render data is never mutated: a `set` of a
data name declares a new binding that shadows it.

### raw

```
{% raw %}{{ this is not parsed }}{% endraw %}
```

### include

```
{% include "partials/nav.html" %}
```

The included template sees the current variables. Its own `set`s stay local
to it. Includes nest at most 64 deep, which catches accidental recursion.

### extends / block

```
{# base.html #}
<title>{% block title %}Site{% endblock %}</title>
<body>{% block body %}{% endblock %}</body>

{# page.html #}
{% extends "base.html" %}
{% block title %}My page{% endblock %}
{% block body %}...{% endblock %}
```

- `extends` must be the first non-whitespace, non-comment content of a
  template.
- A child template may contain only `block`s, `component`s and `import`s
  (plus whitespace and comments) at its top level. Anything else is an
  error, not silently dropped output.
- A top-level block in a child must exist in some ancestor. A misspelled
  override (`{% block haed %}`) is an error instead of silently never
  rendering. Blocks nested inside an override may introduce new names for
  further children.
- Inheritance can be multi-level, and the most-derived block wins.
  `{{ super() }}` inside a block renders the next definition up the chain.
- Circular `extends` is an error.

### component / slot / use / fill / import

Components replace Jinja's macros, `call` blocks and `caller()`:

```
{# components/card.html #}
{% component card(title) %}
<div class="card">
  <h2>{{ title }}</h2>
  <div class="body">{% slot body %}{% endslot %}</div>
  {% slot footer %}<small>default footer</small>{% endslot %}
</div>
{% endcomponent %}

{# page.html #}
{% import "components/card.html" %}
{% use card({ title: "Hello" }) %}
  {% fill body %}Body text{% endfill %}
{% enduse %}
```

- **Definitions:** `{% component name(a, b = default) %}` at the top level of
  any template. A definition produces no output and can be used anywhere in
  that template, before or after it. Defaults may refer to earlier
  parameters. Parentheses are optional when there are no parameters.
- **Arguments:** a single table, because Mobius has no named arguments:
  `{% use card({ title: "Hi" }) %}`. A missing argument with no default, or
  an argument the component doesn't declare, is an error.
- **Scope:** a component body sees only its parameters and env globals. It
  can't see the caller's variables, so components are reusable.
- **Slots:** `{% slot name %}default{% endslot %}` declares a slot;
  `{% slot %}` with no name is the slot called `default`. In a `use` body,
  each `{% fill name %}` fills one slot and renders in the *caller's* scope.
  A body with no fills fills the default slot, and mixing the two is an
  error. An unfilled slot renders its default content. Filling a slot the
  component doesn't declare is an error.
- **Imports:** `{% import "file" %}` (top level only) makes the components
  defined in that file usable. Local definitions win over imports. A name
  defined in two imports is an error. Imports aren't transitive.
- **Nesting:** components can use other components, and a component can
  forward a slot into a component it uses. Recursion is capped at 64 levels.

Jinja's `macro` and `call` are parse errors that point to components.

---

## Built-in filters

| Group | Filters |
|---|---|
| Strings | `upper` `lower` `trim` `capitalize` `title` `replace(old, new)` `truncate(n [, suffix])` `repeat(n)` `split(sep)` `startswith(s)` `endswith(s)` |
| Case | `pascal` `camel` `snake` `kebab`. Words are split on non-alphanumerics and case changes, so `"parseHTTPServer":snake()` is `parse_http_server` |
| Collections | `size` `first` `last` `reverse` `sort` `keys` `join([sep])` `contains(x)`. `reverse`/`sort` return copies; `keys` is sorted |
| Missing values | `default(fallback)` `defined()` |
| Escaping | `escape` `escape_url` `json([indent])` `raw` |
| Conversion | `str` `int` `float` |

`truncate` counts bytes but never splits a UTF-8 character. `json` sorts
keys so output is deterministic.

Custom filters are plain Mobius functions that take the value first:
`env:filter("wrap", func(v, l, r) { return l + v + r })`, used as
`{{ x:wrap("[", "]") }}`. Mobius checks arity strictly, so a filter is called
with exactly the arguments the template passes. `raw`, `default` and
`defined` can't be replaced.

## Escaping

Escaping is explicit by default. Built-in filters: `escape` (HTML text and
attributes), `escape_url` (percent-encoding) and `json`. The same functions are
exported as `stencil.escape_html`, etc.

**Default-escape switch:** `stencil.env({ escape: fn })` runs every `{{ }}`
output through `fn` unless the value was marked with `:raw()`. That's the
whole rule. It's off by default.

`:raw()` returns a *safe string* that the default escaper skips. `super()`
also returns one, since its output was already escaped. Safe strings are only
meaningful at output time; any other filter applied to one gets a plain
string back.

## Errors

Every error is thrown as a table:

```mobius
{
    stencil: true,
    template: "user.html",
    line: 12, col: 5,
    message: "user.html:12:5: undefined variable 'usr'\n  included from base.html:3:4",
    reason: "undefined variable 'usr'",
    trace: [ ... ]     // include/component/extends chain, innermost first
}
```

`message` is ready to print. `stencil.format_error(err)` handles both stencil
errors and anything else that was thrown.

## Environment API

```mobius
var env = stencil.env({            // or stencil.env(nil)
    root: "templates",            // loader directory (optional)
    escape: stencil.escape_html,  // default escaper (optional)
    globals: { site: "Example" }, // visible in every template (optional)
    cache: true,                  // cache parsed templates by name (default true)
    reload: true                  // pick up edited files (default true)
})

env:filter("money", func(v) { return "$" + str(v) })
env:global("year", 2026)

var t = env:get("page.html")        // parsed template, cached
t:render({ ... })
env:render("page.html", { ... })
env:from_string("{{ x }}", "inline") // the name is used in error messages
env:check()                          // array of error tables, empty when clean

stencil.render_string(src, data)     // one-off, default options
stencil.escape_html(s)
stencil.escape_url(s)
stencil.format_error(err)
```

**Reload.** With `reload` on (the default), editing a template on disk takes
effect on the next render, with no restart. That includes included files,
parents of `extends`, and imported component files. Each load checks the
file's `mtime` (whole seconds) and re-parses only when it changed. Within a
single render each file is checked at most once, so a component used on
every row of a table costs one check, not one per row. `reload: false` skips
the checks and keeps whatever was parsed first.

Template names are relative to `root`. Absolute names and `..` are rejected,
so a template can't reach outside its root.

`env:check()` parses every file under `root` and reports, all at once:

- syntax errors and unknown filters (including in component defaults),
- `include` / `extends` / `import` targets that don't exist,
- block overrides that no ancestor defines,
- `use` of an unknown component, fills of undeclared slots, and missing or
  unknown arguments when the argument is a table literal.

Targets and imports must be string literals to be checked. Anything else is
left to render time. It's meant to be called at startup or in a test, after
registering filters.

## web integration

`web` has a duck-typed hook. Any object with `render(name, data)` works, and
`web` doesn't depend on this package:

```mobius
var app = web.app()
app:templates(stencil.env({ root: "templates", escape: stencil.escape_html }))
app:get("/", func(ctx) { return ctx:render("index.html", { user: "bob" }) })
```

## Concurrency

Stencil uses no fibers. It is ordinary synchronous code that is safe to call
from fibers. The parse cache is a plain table on the env. Because Mobius
copies values between fibers by default, an env created in one fiber and used
in another won't share cache entries. With `web`, each worker fiber gets its
own copy of the app, and so of the env and its cache. That only costs
re-parsing, and is revisited with the speed work.

## Implementation

A single file, `stencil.mob`, in four stages:

1. **Lexer:** splits source into text/output/tag/comment tokens with line and
   column. It applies the standalone-line rule and `-` trimming, and handles
   `raw`.
2. **Expression parser:** Pratt parser for the subset above, producing AST
   tables `{ type, line, col, ... }`.
3. **Template parser:** builds the node tree from tokens and validates nesting
   (`endfor` without `for`, etc.).
4. **Interpreter:** `__render_nodes(ctx, nodes, out)` walks the tree and pushes
   strings into an array that is joined once at the end.

### Mobius constraints

- **Tags, not metatables.** `web` copies the app, and the env inside it, into
  each worker fiber, and a copied metatable is a different table. So safe
  strings and wrapped built-ins are tagged with a field (`__stencil_safe`,
  `__stencil_builtin`) rather than recognized by metatable identity.
- **Fixed arity.** Mobius functions can't take optional arguments, so built-in
  filters take `(value, args)` with declared min/max counts. `range` is a
  wrapped built-in that `__eval_call` dispatches.
- **Return-type inference** rejects a function whose returns have different
  literal types (`[]` vs `{}`, `bool` vs `float`), so some cases live in
  small helpers.

## Roadmap

| Version | Scope |
|---------|-------|
| v1 | Lexer, parser, interpreter. `if` / `for` / `set` / `raw` / `include` / `extends` / `block`. Filters, strict undefined, whitespace rule, default escaper, errors, `env:check()`. **Done.** |
| v2 | Ternary, components and slots, imports, `super()`, block-override checking, `web` integration (`ctx:render`). **Done.** |
| v3 | Speed: compile the AST to a closure tree; shared parse cache across fibers. |

## Status

| Feature | State |
|---------|-------|
| Lexer, whitespace rule, `-` trimming, comments, `raw` | done |
| Expressions, filters, strict undefined | done |
| `if` / `for` / `set` | done |
| `include` / `extends` / `block` | done |
| Default escaper, `:raw()` | done |
| Ternary | done |
| `component` / `slot` / `use` / `fill` / `import` | done |
| `super()`, block-override checking | done |
| `env:check()` | done (see above) |
| `web` integration (`app:templates`, `ctx:render`) | done |
| Closure-tree compiler, shared cache | v3, not started |

Tests: `tests/modules/test_stencil_package.mob` runs the cases in
`tests/modules/stencil/cases.mob` against the package source.
