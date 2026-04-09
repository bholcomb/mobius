# `crypto` Module

Import:

```mobius
import "crypto"
```

The `crypto` module provides dependency-free hashing, encoding, checksums, UUID,
and secure random helpers.

Functions:

| Function | Description |
|---|---|
| `crypto.sha256(text)` | SHA-256 digest as lowercase hex. |
| `crypto.sha1(text)` | SHA-1 digest as lowercase hex. |
| `crypto.md5(text)` | MD5 digest as lowercase hex. |
| `crypto.hmac_sha256(key, text)` | HMAC-SHA256 as lowercase hex. |
| `crypto.crc32(text)` | CRC32 checksum as an integer. |
| `crypto.base64_encode(text)` | Base64-encode a string. |
| `crypto.base64_decode(text)` | Base64-decode a string. |
| `crypto.uuid4()` | Generate a random UUIDv4 string. |
| `crypto.random_hex(bytes)` | Generate secure random bytes encoded as hex. |
| `crypto.random_int(min, max)` | Generate a secure random integer in the inclusive range. |

Notes:

- `md5` and `sha1` are included for compatibility, not for new security-sensitive designs.
- `base64_decode(...)` returns a Mobius string, so decoded data containing NUL bytes is rejected.

Example:

```mobius
import "crypto"

print(crypto.sha256("hello"))
print(crypto.uuid4())
print(crypto.random_hex(16))
```
