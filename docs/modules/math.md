# `math` Module

Import:

```mobius
import "math"
```

[← Module reference](index.md)

The `math` module provides extended functions and constants. Core numeric helpers
such as `abs`, `sqrt`, `pow`, `floor`, `ceil`, `round`, `min`, and `max` are
available without importing this module.

Constants:

- `math.pi`
- `math.e`
- `math.tau`

Functions:

| Group | Members |
|---|---|
| Trigonometric | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2` |
| Hyperbolic | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh` |
| Logs and exponentials | `log`, `log10`, `log2`, `log1p`, `exp`, `exp2`, `expm1` |
| Angles and utility | `deg2rad`, `rad2deg`, `sign`, `clamp`, `hypot`, `cbrt`, `trunc` |
| Number theory | `factorial`, `gcd`, `lcm` |

Notes:

- `math.factorial(n)` returns an `int64` and accepts `0 <= n <= 20`.
- `math.trunc(x)` truncates toward zero and returns a `float64`.

Example:

```mobius
import "math"

var angle = math.deg2rad(45)
print(math.sin(angle))
print(math.sign(-12))
print(math.hypot(3, 4))
print(math.trunc(-3.9))
print(math.gcd(42, 30))
print(math.pi)
```
