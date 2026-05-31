# Binary Data

Mobius works with raw bytes through two cooperating features:

- **Buffers** — mutable byte sequences for I/O and protocol work.
- **Struct views** — typed layouts that read and write fields directly over a
  buffer's bytes, with no copying.

[← Documentation home](../index.md) · [Guide: Collections](collections.md)

---

## Buffers

Create a buffer with a size (optionally filled), or from a string's bytes:

```mobius
var buf   = buffer_create(4)         // [0, 0, 0, 0]
var magic = buffer_create(4, 255)    // [255, 255, 255, 255]
var hello = buffer_from_string("PING")
```

Buffers created in scripts are **growable**. (Fixed-size buffers that wrap
external memory can be produced by host applications via the embedding API; for
those, `resize`/`reserve`/`append` raise an error — check `buf:is_fixed()`.)

Buffers support bracket indexing and the `:` method syntax.

### Buffer methods

| Method                         | Description                                                   |
|--------------------------------|---------------------------------------------------------------|
| `buf:size()`                 | Number of bytes                                               |
| `buf:get(index)`               | Byte at index (`nil` if out of bounds)                        |
| `buf:set(index, byte)`         | Write a byte in `[0, 255]` at index                           |
| `buf:append(value)`            | Append a byte, string, or buffer to the end (growable only)   |
| `buf:resize(size [, fill])`    | Resize the buffer, padding new bytes with `fill` (growable)   |
| `buf:reserve(capacity)`        | Pre-grow capacity without changing length (growable)          |
| `buf:slice(start, end)`        | Return a **new** buffer copy of the bytes in `[start, end)`    |
| `buf:copy()`                   | Return a full independent copy of the buffer                  |
| `buf:to_string()`              | Return the raw bytes as a string                              |
| `buf:is_fixed()`               | `true` if the buffer is fixed-size (cannot grow)              |
| `buf:address()`                | Raw memory address of the buffer's data as a `uint64`         |
| `buf:view_as(layout [, off])`  | A single struct view over the bytes (see below)               |
| `buf:array_view_as(layout [, off [, count]])` | An indexed array of struct views               |

The global [`size(buf)`](../reference/standard-library.md#strings)
also returns the byte length.

```mobius
var buf = buffer_from_string("AB")
buf:append("CD")
print(buf:to_string())    // "ABCD"
print(buf:size())       // 4
print(buf[0])             // 65  ('A')
```

> Note: `buf:slice(...)` returns an independent **copy** of the bytes — the same
> copy-vs-view distinction as arrays, where `arr:slice` copies and
> [`arr:span`](concurrency.md#array-spans) returns an aliasing view.

---

## Struct views

A `struct` declaration describes a binary layout. It does not allocate memory —
it is a *layout* you overlay on a buffer to read and write typed fields directly.

### Declaring a layout

```mobius
struct Vec2 packed {
    x: float32
    y: float32
}

struct Header packed {
    tag: uint16
    version at 4: uint16     // pinned to byte offset 4
}

struct Alias packed {
    union {                  // members overlap at the same offset
        first: uint32
        second: uint32
    }
}
```

Rules:

- `packed` uses byte-packed layout; `native` uses natural alignment and rounds
  the total size up to the layout's alignment.
- `field at <offset>: <type>` pins a field to an explicit byte offset.
- `union { … }` overlays all nested members at the same base offset.
- Array fields use `<type>[<count>]`.
- Field types may be builtin scalars or previously declared struct layouts.

Builtin scalar types: `int8`, `uint8`, `byte`, `int16`, `uint16`, `int32`,
`uint32`, `int64`, `uint64`, `float32`, `float64`, `bool`, `bool8`.

A layout evaluates to an object with metadata: `Layout.name`, `Layout.size`,
`Layout.align`, `Layout.packed`.

### Creating views over a buffer

```mobius
struct Vec2 packed { x: float32; y: float32 }

var buf = buffer_create(Vec2.size)
var v = buf:view_as(Vec2)
v.x = 1.5
v.y = 2.5
print(v.x)          // 1.5
```

A view exposes `view.field` (read/write), plus `view.buffer`, `view.offset`,
`view.layout`, and `view.size` (the layout's size **in bytes**). Nested struct
fields return nested views; array fields return indexed array views.

### Array views

`buf:array_view_as(Layout [, offset [, count]])` overlays an array of structs. If
`count` is omitted, it is inferred from the remaining bytes:

```mobius
struct Pixel packed {
    union {
        rgba: uint32
        bytes: uint8[4]
    }
}

var framebuffer = buffer_create(Pixel.size * 640 * 480)
var pixels = framebuffer:array_view_as(Pixel)

for (var i = 0; i < pixels.size; i++) {
    pixels[i].bytes[3] = 255    // set alpha
}

print(pixels[0].bytes[3])       // 255
print(pixels.size)            // 307200
```

An array view exposes `array_view[index]`, `array_view.size`,
`array_view.layout`, `array_view.buffer`, and `array_view.offset`. Here
`array_view.size` is the **element count** (consistent with `arr:size()` and
`tbl:size()`); total bytes is `array_view.size * array_view.layout.size`.

### Nested structs

```mobius
struct Vec2 packed { x: float32; y: float32 }

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

Layouts and views are dedicated runtime objects, not plain tables.

---

Next: [Concurrency](concurrency.md).
