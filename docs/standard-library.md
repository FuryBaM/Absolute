# Absolute standard library

This document defines the **stable package layout**, **namespace map**,
**semantic versioning**, and **stability tiers** for the Absolute standard
library (`std/`).

It is the normative reference for what may ship under the `absolute.std`
package and how dependents should constrain versions.

## Package identity

| Field | Value |
|-------|--------|
| Package name | `absolute.std` |
| Manifest | [`std/abspackage.json`](../std/abspackage.json) |
| Root on disk | repository path `std/` |
| Current version | see manifest `version` (SemVer `MAJOR.MINOR.PATCH`) |
| Package type | `lib` |

Dependents declare:

```json
{
  "dependencies": {
    "absolute.std": "^0.1.0"
  }
}
```

When the package is resolved from a registry, the lockfile records the exact
resolved version in `abspackage.lock` under `versions["absolute.std"]`.

The compiler also accepts **source imports** of files under `std/` without a
package dependency (the in-tree workflow used by tests and examples). Package
constraints apply when `absolute.std` is listed as a dependency.

## Layout rules

1. **One primary Absolute module file per logical module**, under `std/`.
2. **Namespace = import path with dots**, with the exceptions table below.
3. **Subdirectories** group multi-file modules (`std/collections/`, `std/concurrent/`).
4. **File names are lowercase** ASCII; namespaces use `std.<module>` camelCase only when matching existing APIs (today: all lower).
5. **No plugin-only symbols** live in `std/`. GPU, math, and shader APIs belong in plugins (`absolute.desktop`, `absolute.math`, `absolute.shader`).
6. **Runtime C helpers** used by `std` use the `absolute_*` prefix and are not part of the public Absolute surface.

### Directory tree

```
std/
  abspackage.json          # package identity + version
  assert.abs               # std.assert
  binary.abs               # std.binary
  concurrent.abs           # std.concurrent (atomics, mutex)
  concurrent/
    capsule.abs            # std.concurrent transfer capsule helpers
  collections/
    vector.abs             # std.collections.Vector
    map.abs                # std.collections.Map
    set.abs                # std.collections.Set
    channel.abs            # std.collections.Channel
    algorithms.abs         # std.collections algorithms
  datetime.abs             # std.datetime
  env.abs                  # std.env
  fs.abs                   # std.fs
  http.abs                 # std.http
  json.abs                 # std.json
  log.abs                  # std.log
  net.abs                  # std.net
  process.abs              # std.process
  random.abs               # std.random
  string.abs               # std.text (StringBuilder + Unicode helpers)
  task.abs                 # std.task
  testing.abs              # std.testing
  time.abs                 # std.time (+ legacy Time.*)
  uri.abs                  # std.uri
```

### Namespace ↔ file map

| Namespace | File | Notes |
|-----------|------|--------|
| `std.assert` | `std/assert.abs` | Soft assertions (`throw`); built-in `assert` remains aborting |
| `std.binary` | `std/binary.abs` | `BinaryReader` / `BinaryWriter` |
| `std.collections` | `std/collections/*.abs` | Split across files; import any file that defines the needed type |
| `std.concurrent` | `std/concurrent.abs`, `std/concurrent/capsule.abs` | Atomics, mutex, isolate capsules |
| `std.datetime` | `std/datetime.abs` | Calendar / timezone |
| `std.env` | `std/env.abs` | Environment variables |
| `std.fs` | `std/fs.abs` | Paths and `File` |
| `std.http` | `std/http.abs` | HTTP client/server |
| `std.json` | `std/json.abs` | JSON |
| `std.log` | `std/log.abs` | Leveled logging |
| `std.net` | `std/net.abs` | TCP/UDP + DNS helpers |
| `std.process` | `std/process.abs` | Process / spawn |
| `std.random` | `std/random.abs` | `Rng`, entropy |
| `std.task` | `std/task.abs` | Task scheduling queries |
| `std.testing` | `std/testing.abs` | Lightweight test suite |
| `std.time` | `std/time.abs` | Wall / mono clocks; legacy `Time.*` wrappers |
| `std.text` | `std/string.abs` | **Name exception:** file is `string.abs`, namespace is `std.text` |
| `std.uri` | `std/uri.abs` | URI parse/encode |

**Import styles** (all supported):

```absolute
import std.fs;                 // namespace-style
import "std/fs.abs";           // path-style from repo root / search path
import "./std/fs.abs";         // relative path
import std.collections.vector; // dotted path into subdirectory
```

Prefer **namespace imports** in application code; path imports remain for tests
and tooling.

## Stability tiers

Every public `std.*` API falls into one of three tiers. The tier is documented
per module in this file; breaking changes are gated by SemVer rules below.

### Tier Stable

- Public types, functions, and methods that appear in this document’s module map
  and are covered by repository tests.
- **Contract:** may not break without a **MAJOR** bump of `absolute.std`.
- Additions (new types/methods) require only a **MINOR** bump.

Current Stable modules (0.x: *preview-stable* — see versioning note):

- `std.time`, `std.env`, `std.process`, `std.fs`
- `std.collections` (`Vector`, `Map`, `Set`, algorithms, `Channel`)
- `std.string` file → `std.text` (`StringBuilder` and string helpers)
- `std.assert`, `std.log`, `std.testing`
- `std.random`, `std.json`, `std.binary`
- `std.net`, `std.uri`, `std.http`
- `std.datetime`, `std.task`, `std.concurrent`

### Tier Experimental

- APIs marked `// experimental` in source, or modules listed as Experimental
  below.
- **Contract:** may change in any MINOR release; may be removed with a MINOR
  bump and a changelog entry. Prefer not to use in published packages without a
  tight version pin.

Experimental (today): *none reserved* — use comments in source when introducing.

### Tier Internal

- `absolute_*` C runtime symbols, private helpers, `// internal` items.
- **Contract:** no stability; Absolute may change them without package bumps if
  the Absolute language/runtime major changes.

## Semantic versioning

`absolute.std` uses **SemVer 2.0.0** (`MAJOR.MINOR.PATCH`).

| Change | Bump |
|--------|------|
| Incompatible change to a Stable API | **MAJOR** |
| New Stable module, type, or method | **MINOR** |
| Bug fix or performance improvement without API change | **PATCH** |
| Experimental API change/removal | **MINOR** (document in changelog) |
| Internal-only change | **PATCH** (or no package bump if not published) |

### Pre-1.0 policy (current)

While `MAJOR == 0`:

- `0.MINOR.PATCH` is the public preview line.
- **Breaking changes to Stable APIs are allowed only in MINOR bumps**
  (`0.1.x` → `0.2.0`), never in PATCH.
- Dependents should use caret constraints (`^0.1.0`) only if they accept 0.x
  churn; production code may pin exactly (`0.1.0`) or use
  `>=0.1.0 <0.2.0`.

When Absolute freezes the std surface, bump to **1.0.0** and apply normal
SemVer (breaking → MAJOR).

### Constraint syntax

Same as plugin packages (`docs/plugin-manifests.md`):

- exact: `1.2.3`, `=1.2.3`
- ranges: `>`, `>=`, `<`, `<=`
- caret: `^1.2.0` (compatible with 1.x)
- tilde: `~1.2.0` (compatible with 1.2.x)
- intersection: `>=1.0.0 <2.0.0`

## Compatibility with the language and runtime

`absolute.std` is versioned **independently** of the Absolute compiler, but each
release is validated against a **minimum language/runtime** pair:

| std version | Minimum Absolute (compiler + runtime) |
|-------------|----------------------------------------|
| 0.1.x | current `main` / release that ships this `std/` tree |

If a future std release requires new language features, document the floor in
the package changelog and refuse to load on older compilers when that check is
implemented.

## Module responsibilities (summary)

| Module | Responsibility |
|--------|----------------|
| `std.time` | Wall clock, monotonic time, sleep, measure/bench |
| `std.datetime` | Calendar date/time, zones, ISO-8601 |
| `std.env` / `std.process` | Environment and process control |
| `std.fs` | Filesystem |
| `std.net` / `std.uri` / `std.http` | Networking stack |
| `std.collections` | Dynamic collections + algorithms + channels |
| `std.concurrent` | Shared concurrent capabilities (atomics, mutex, capsules) |
| `std.task` | Task metadata / scheduling queries |
| `std.text` | String building and Unicode helpers |
| `std.json` / `std.binary` | Serialization |
| `std.random` | PRNG + entropy |
| `std.log` / `std.assert` / `std.testing` | Diagnostics and tests |

## Adding a new std module

1. Choose a namespace `std.<name>` and file path under `std/`.
2. Implement Absolute wrappers over `absolute_*` runtime only when needed.
3. Add semantic + runtime tests under `tests/`.
4. Document the module in this file (map + tier).
5. Bump `std/abspackage.json` version (MINOR for new Stable API).
6. Note the change in the repository changelog / release notes.

Do **not** place desktop/GPU/math/shader APIs in `std/`.

## Non-goals

- Packaging every std file as a separate NuGet-style package (one `absolute.std`
  package for the monorepo release).
- Guaranteeing binary ABI for Absolute `std` types across compiler versions
  (source compatibility is the contract; recompile dependents).
- Shipping third-party libraries under the `std.*` namespace.
