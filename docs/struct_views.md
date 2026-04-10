# Struct Views

Mobius now supports language-level `struct` declarations for describing binary layouts over `buffer` memory.

## Declaration Syntax

Use `struct <Name> [packed|native] { ... }`.

```mob
struct Vec2 packed {
    x: float32
    y: float32
}

struct Header packed {
    tag: uint16
    version at 4: uint16
}

struct Alias packed {
    union {
        first: uint32
        second: uint32
    }
}
```

Rules:

- `packed` uses byte-packed layout.
- `native` uses natural alignment and rounds the final size up to the layout alignment.
- `field at <offset>: <type>` pins a field to an explicit byte offset.
- `union { ... }` overlays all nested members at the same base offset.
- Array fields use `<type>[<count>]`.
- Field types can be builtin scalars or previously defined struct layouts.

Builtin scalar types:

- `int8`, `uint8`, `byte`
- `int16`, `uint16`
- `int32`, `uint32`
- `int64`, `uint64`
- `float32`, `float64`
- `bool`, `bool8`

## Runtime API

Struct declarations evaluate to layout objects with direct metadata:

- `Layout.size`
- `Layout.align`
- `Layout.packed`
- `Layout.name`

Views are created from buffers:

- `buffer:view_as(Layout[, offset])` returns a single struct view
- `buffer:array_view_as(Layout[, offset[, count]])` returns an indexed array view

Struct view behavior:

- `view.field` reads or writes a field directly
- Nested struct fields return nested views
- Array fields return indexed array views
- `view.size` returns the layout size in bytes
- `view.buffer` returns the underlying buffer
- `view.offset` returns the byte offset into the underlying buffer

Array view behavior:

- `array_view[index]` returns the element view or scalar element
- `array_view.length` returns the number of elements
- `array_view.layout` returns the element layout
- `array_view.buffer` returns the underlying buffer
- `array_view.offset` returns the starting byte offset
- When `count` is omitted, `array_view_as` uses the remaining bytes in the buffer

Notes:

- Layouts and views are dedicated native runtime objects exposed through the language. They are not plain tables.
- The old helper-style globals such as `struct_new(...)` and `struct_get(...)` are not part of the API.

Example:

```mob
struct RGBA packed {
    union {
        rgba: uint32
        bytes: uint8[4]
        struct {
            r: uint8
            g: uint8
            b: uint8
            a: uint8
        }
    }
}

var framebuffer = buffer_create(RGBA.size * 640 * 480)
var pixels = framebuffer:array_view_as(RGBA)

for (var i = 0; i < pixels.length; i++) {
    pixels[i].r = 0
    pixels[i].g = 0
    pixels[i].b = 0
    pixels[i].a = 255
}

print(pixels[0].bytes[1])
```

Nested structs:

```mob
struct Vec2 packed {
    x: float32
    y: float32
}

struct Vertex packed {
    position: Vec2
    intensity: float32
}

var storage = buffer_create(Vertex.size)
var vertex = storage:view_as(Vertex)
vertex.position.x = 3.25
vertex.position.y = 9.5
vertex.intensity = 7.0
```
