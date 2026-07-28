# Differential compiler testing

`absolutec` accepts `-O0`, `-O1`, `-O2`, and `-O3` for LLVM object and native
or WebAssembly executable generation. O3 remains the default for object code,
preserving the compiler's previous release behavior. Supplying an optimization
level with `--emit-llvm` also runs that pipeline before writing the IR; without
an explicit level, emitted IR remains unoptimized for diagnostics and existing
IR contracts.

The `absolute.optimization-differential` CTest generates a deterministic corpus
from a fixed seed. Each source contains an independently generated checksum
oracle. The runner builds and executes the identical source at all four
optimization levels, then verifies:

- successful compilation and execution;
- the checksum against the generated oracle;
- identical complete stdout across O0, O1, O2, and O3.

Generated sources, executables, and `failure.json` are retained under
`<build>/differential/optimization/<configuration>`. The Linux CI matrix runs
the same test using both Debug and Release builds of the compiler, separating
compiler-build differences from optimization-pipeline differences.

The `absolute.native-wasm-differential` CTest consumes the same seed, generator,
and checksum oracle. It builds each case as an O3 host executable and as a
`wasm32-unknown-unknown` module, executes the latter through
`tools/absolute-wasm-host.js`, and compares the complete normalized output from
both backends with the oracle. Its sources, native executables, wasm modules,
Node runner, and mismatch report live under
`<build>/differential/native-wasm/<configuration>`.

Run the focused test through a configured build:

```text
ctest --test-dir <build> -R absolute.optimization-differential --output-on-failure
ctest --test-dir <build> -R absolute.native-wasm-differential --output-on-failure
```

Or invoke the runner directly:

```text
python tools/testing/optimization_differential.py \
  --compiler <path-to-absolutec> \
  --work-dir <artifact-directory>
```
