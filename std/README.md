# Absolute standard library (`absolute.std`)

Source tree for the Absolute `std.*` namespaces.

| | |
|--|--|
| Package | `absolute.std` **0.1.0** (`abspackage.json`) |
| Policy | [`docs/standard-library.md`](../docs/standard-library.md) |

## Quick import

```absolute
import std.fs;
import std.collections.vector;
import "std/string.abs"; // defines namespace std.text
```

## Modules

See the namespace ↔ file map in `docs/standard-library.md`.

## Versioning

- SemVer on the package: breaking Stable APIs bump **MINOR** while `0.x`, **MAJOR** from `1.0.0`.
- Prefer `^0.1.0` only if you accept 0.x preview churn; pin tighter for production.
