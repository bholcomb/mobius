# Experimental Packages

[← Module reference](index.md)

Beyond the built-in modules and the documented [`sqlite`](sqlite.md) package,
the repository ships several **experimental** native packages. Their APIs are
still in flux (each carries a `TODO.md`), so they are previewed rather than
fully documented here. They are built from `src/packages/<name>/` and staged
under `packages/<name>/`.

Like any package, they must be installed into a `modules/` root before
`import` will resolve them — see
[Modules and Packages](../guide/modules-and-packages.md#packages).

| Package    | Version | Import        | What it provides |
|------------|---------|---------------|------------------|
| `glfw`     | 0.4.0   | `import "glfw"`     | GLFW windowing and input bindings — windows, monitors, events, input callbacks, clipboard, timers, and Vulkan surface helpers. |
| `monstro`  | 0.1.0   | `import "monstro"`  | Monstro immediate-mode UI: a UI context, windows, layout, widgets, and input plumbing (helpers like `monstro.vec2`, `monstro.rect`, `monstro.color`). |
| `vulkease` | 0.1.0   | `import "vulkease"` | VulkEase Vulkan graphics bootstrap: instance/device context creation, physical-device enumeration, and capability queries. |

These are most interesting together — for example, a `glfw` window feeding a
`vulkease` device with a `monstro` UI on top. Because the surfaces are evolving,
treat the `.mob` companion scripts in each package directory
(`packages/<name>/<name>.mob`) and their `module.yaml` as the current source of
truth, and expect breaking changes.

If you build something on these and want them documented to the same standard as
the core modules, that is a good signal to promote them.
