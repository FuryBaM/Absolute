# Random numbers

Import `std/random.abs` to use the deterministic generator:

```absolute
import "../std/random.abs";

std.random.Rng* rng = new std.random.Rng(42);
int32 index = rng.range(0, 10);
double probability = rng.real();
```

`Rng` uses xoshiro256** and produces the same sequence for the same `uint64`
seed on every supported target. Its API contains:

- `u64()` and `i32()` for full-width integer values;
- `range(minimum, maximum)` for an unbiased half-open integer range;
- `real()` for a `double` in `[0, 1)`;
- `boolean()` for a uniformly selected boolean.

`std.random.entropy()` obtains seed material from the platform-backed C++
`random_device`. `std.random.create()` constructs an `Rng` from that entropy.
Keep explicit seeds for tests, simulations, save files, and network protocols;
ambient entropy deliberately has a separate API. Neither API is a
cryptographic random-number generator.

The generator owns a small native state. Its `destroy()` method releases that
state automatically when the managed `Rng` owner is destroyed.
