# Large value reference benchmark

This suite compares the by-value ABI for a 128-byte resource-free struct with
the implemented `const T&` value-reference ABI and an unsafe raw-address
control. All workloads perform 20 million calls to an identical `@noinline`
read-only function and validate the same checksum.

`const T&` is the supported borrowing contract: it is non-null, non-escaping,
read-only, and accepts a temporary for the duration of the call. The raw mode
is retained only as a lower-level control without those language guarantees.

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

The runner reports by-value, `const T&`, and raw-view medians. The language
design and escape rules are recorded in
[`docs/value-references.md`](../../docs/value-references.md).
