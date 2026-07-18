# Pointer and object benchmark

This suite measures four pointer-heavy workloads in Release mode:

- `managed-deref`: 50 million read/modify/write operations through one pointer or object reference. Absolute uses its generation-checked managed pointer.
- `heap-nodes`: allocates 131,072 separate heap nodes, performs 20 million pseudo-random mutations, then releases native nodes where the language exposes deterministic release.
- `arena-graph`: builds a shuffled cycle of 1,048,576 nodes in primitive arenas and follows five million links. Absolute and C++ use raw pointer arithmetic for the arena loads.
- `object-tree`: builds 131,071 linked objects in a `Node -> BinaryNode -> AddNode/XorNode` hierarchy and repeatedly traverses it through virtual/dynamic calls.

The Absolute version uses the same five-class hierarchy, raw `Node*` links, constructors, inherited fields, recursive cleanup, and virtual calls through a generated vtable. It does not substitute arrays for objects.

## Run on Windows

From the repository root:

```bat
benchmarks\pointer-object-suite\run.bat
```

Arguments are `samples`, `warmups`, and `include Python`:

```bat
benchmarks\pointer-object-suite\run.bat 15 3 1
benchmarks\pointer-object-suite\run.bat 10 2 0
```

The runner requires WSL with LLVM/CMake, Visual Studio C++ Build Tools, .NET 10, Java, and Node.js. Python is required only when the third argument is `1`. It reuses the array-suite Release compiler build when available, compiles all native code with LLVM `-O3 -march=native`, validates a shared checksum for every implementation, alternates language order between samples, and writes a timestamped CSV under `results/`.

The CSV includes median/min/max duration, speed relative to the winner, work-unit count, and approximate million work units per second. The stopwatch includes process startup, graph construction/allocation, traversal, and cleanup. This matches the existing benchmark suites, but it means JIT startup and GC policy are part of the result. `heap-nodes` also compares different ownership models: raw native nodes in Absolute/C++ versus garbage-collected object references in C#/Java/JavaScript/Python. Use `arena-graph` for the closest representation-level comparison.
