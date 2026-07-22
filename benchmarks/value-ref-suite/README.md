# Large value reference benchmark

This suite compares the existing by-value ABI for a 128-byte resource-free
struct with a raw pointer used only as a benchmark proxy for a future
`const ref` parameter. Both workloads perform 20 million calls to an identical
`@noinline` read-only function and validate the same checksum.

The raw-pointer source is deliberately not proposed as user-facing safe syntax:
it has no const or escape guarantee. It measures the upper bound available from
passing one borrowed address instead of materializing an isolated by-value
argument at an opaque call boundary.

Run on Windows from the repository root:

```bat
benchmarks\value-ref-suite\run.bat
```

Optional arguments select the sample and warmup counts:

```bat
benchmarks\value-ref-suite\run.bat 15 3
```

The runner alternates execution order and writes medians plus raw range data to
`results/value-ref-*.csv`.

The baseline 2026-07-22 run on an AMD Ryzen 7 5700U used 15 samples and three
warmups: by-value median `0.156551 s`, raw-borrow median `0.131116 s`, or a
`1.19x` reference-ABI speedup. The accepted language design and escape rules
are recorded in [`docs/value-references.md`](../../docs/value-references.md).
