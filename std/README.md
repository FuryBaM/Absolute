# Absolute standard library (`absolute.std`)

Source tree for the Absolute `std.*` namespaces.

| | |
|--|--|
| Package | `absolute.std` **0.4.0** (`abspackage.json`) |
| Policy | [`docs/standard-library.md`](../docs/standard-library.md) |

## Quick import

```absolute
import std.core; // common application modules
import std.fs;
import std.env;
import std.collections.vector;
import std.collections.deque;
import "std/string.abs"; // defines namespace std.text
```

Launch arguments are available through `std.env.argsCount()`,
`std.env.argAt()`, `std.env.args()`, `std.env.flag()`, and
`std.env.parameter()`.

Resource-owning standard types are cleaned up automatically through
`destroy()`. `close()` and `dispose()` remain available when deterministic,
early cleanup is useful.

Filesystem paths can be composed without string concatenation:

```absolute
std.fs.Path* asset = std.fs.path("assets");
asset.push("textures");
asset.push("wall.png");
```

## Modules

See the namespace ↔ file map in `docs/standard-library.md`.

## Versioning

- SemVer on the package: breaking Stable APIs bump **MINOR** while `0.x`, **MAJOR** from `1.0.0`.
- Prefer `^0.4.0` only if you accept 0.x preview churn; pin tighter for production.
