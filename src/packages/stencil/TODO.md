# Stencil Package TODO

User documentation lives in `docs/modules/stencil.md`.

## Speed

Templates are parsed once and cached, then tree-walked on every render
(about 28 ms for a 1,000-row table page). Next steps:

- Compile the parsed template into a tree of closures so rendering doesn't
  dispatch on node types.
- Share the parse cache across fibers. Today `web` gives each worker fiber
  its own copy of the app, and so its own cache.

## Implementation notes

- Mobius functions have fixed arity, so built-in filters take
  `(value, args)` with declared min/max argument counts, and `range` is a
  wrapped built-in dispatched by `__eval_call`.
- Safe strings and wrapped built-ins are tagged with a field
  (`__stencil_safe`, `__stencil_builtin`) rather than a metatable, because a
  copy into another fiber gets a different metatable table.
- The compiler rejects a function whose return statements have different
  literal types (`[]` vs `{}`), so some cases live in small helpers.
