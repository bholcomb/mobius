# `crypto` Module

```mobius
import "crypto"
```

[← Module reference](index.md)

Dependency-free hashing, HMAC, encoding, checksums, UUID, and secure random
helpers. Hash and decode functions are **buffer-first**: the raw-bytes variants
return a `buffer`, and the `_hex` variants return a lowercase hex `string`.

## Hashing

| Function                  | Returns | Description                          |
|---------------------------|---------|--------------------------------------|
| `crypto.sha256(data)`     | buffer  | SHA-256 digest as raw bytes          |
| `crypto.sha256_hex(data)` | string  | SHA-256 digest as lowercase hex      |
| `crypto.sha1(data)`       | buffer  | SHA-1 digest as raw bytes            |
| `crypto.sha1_hex(data)`   | string  | SHA-1 digest as lowercase hex        |
| `crypto.md5(data)`        | buffer  | MD5 digest as raw bytes              |
| `crypto.md5_hex(data)`    | string  | MD5 digest as lowercase hex          |

`data` may be a `string` or a `buffer`.

## HMAC and checksums

| Function                            | Returns | Description                      |
|-------------------------------------|---------|----------------------------------|
| `crypto.hmac_sha256(key, data)`     | buffer  | HMAC-SHA256 as raw bytes         |
| `crypto.hmac_sha256_hex(key, data)` | string  | HMAC-SHA256 as lowercase hex     |
| `crypto.crc32(data)`                | integer | CRC32 checksum as an integer     |

## Encoding

| Function                     | Returns | Description                            |
|------------------------------|---------|----------------------------------------|
| `crypto.base64_encode(data)` | string  | Base64-encode a string or buffer       |
| `crypto.base64_decode(text)` | buffer  | Base64-decode into raw bytes           |
| `crypto.hex_encode(data)`    | string  | Hex-encode a string or buffer          |
| `crypto.hex_decode(text)`    | buffer  | Hex-decode into raw bytes              |

## UUID and secure random

| Function                      | Returns | Description                                 |
|-------------------------------|---------|---------------------------------------------|
| `crypto.uuid4()`              | string  | Random UUIDv4 string                        |
| `crypto.random_hex(n)`        | string  | `n` secure random bytes, hex-encoded        |
| `crypto.random_bytes(n)`      | buffer  | `n` secure random bytes as a buffer         |
| `crypto.random_int(min, max)` | integer | Secure random integer in `[min, max]`       |

## Notes

- `md5` and `sha1` are included for compatibility, not for new
  security-sensitive designs.
- Decode functions (`base64_decode`, `hex_decode`) return a `buffer`, so binary
  output (including NUL bytes) is preserved. Use `buf:to_string()` for text.

## Example

```mobius
import "crypto"

print(crypto.sha256_hex("hello"))
// 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824

print(crypto.base64_encode("hi"))                  // "aGk="
print(crypto.base64_decode("aGk="):to_string())    // "hi"

print(crypto.uuid4())
print(crypto.random_hex(16))

var mac = crypto.hmac_sha256_hex("secret", "message")
print(mac)
```
