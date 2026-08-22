# Pointer and object benchmark

This suite measures four pointer-heavy workloads in Release mode:

- `managed-deref`: 50 million read/modify/write operations through one pointer or object reference. Absolute uses its generation-checked managed pointer. For a local owner whose lifetime is proven stable, codegen resolves the slot once and reuses the checked address; subscribers and unknown lifetimes retain checked dereference.
- `heap-nodes`: allocates 131,072 separate heap nodes, performs 20 million pseudo-random mutations, then releases native nodes where the language exposes deterministic release.
- `arena-graph`: builds a shuffled cycle of 1,048,576 nodes in primitive arenas and follows five million links. Absolute and C++ use raw pointer arithmetic for the arena loads.
- `object-tree`: builds 131,071 linked objects in a `Node -> BinaryNode -> AddNode/XorNode` hierarchy and repeatedly traverses it through virtual/dynamic calls.

The Absolute version uses the same five-class hierarchy, raw `Node*` links, constructors, inherited fields, recursive cleanup, and virtual calls through a generated vtable. It does not substitute arrays for objects.

## Run on Windows

From the repository root:

```bat
benchmarks\pointer-object-suite\run.bat
```

Arguments are `samples`, `warmups`, `include Python`, and an optional backend:

```bat
benchmarks\pointer-object-suite\run.bat 15 3 1
benchmarks\pointer-object-suite\run.bat 10 2 0
benchmarks\pointer-object-suite\run.bat 10 2 0 wsl
```

The default backend is native Windows and requires the portable LLVM SDK from
`build-windows.bat --bootstrap`, Visual Studio C++ Build Tools, .NET 10, Java,
and Node.js. Python is required only when the third argument is `1`. The
optional `wsl` backend preserves the older cross-compilation path. Every run
validates a shared checksum, alternates language order between samples, and
writes a timestamped CSV under `results/`.

The CSV includes median/min/max duration, speed relative to the winner, work-unit count, and approximate million work units per second. The stopwatch includes process startup, graph construction/allocation, traversal, and cleanup. This matches the existing benchmark suites, but it means JIT startup and GC policy are part of the result. `heap-nodes` also compares different ownership models: raw native nodes in Absolute/C++ versus garbage-collected object references in C#/Java/JavaScript/Python. Rust sits with the first group and is the closest comparison of the three: a `Box<Cell>` is one owner released at a known point, which is what an Absolute `Cell*` is. Use `arena-graph` for the closest representation-level comparison.

## Rust

Rust is measured with `rustc -C opt-level=3 -C target-cpu=native -C
overflow-checks=off --edition 2021` -- the same three things the C++ build is
given: optimize fully, target this machine, and let integer arithmetic wrap.
The last is written out rather than left to a profile default so the
arithmetic does not change with how `rustc` was invoked, and the ports use the
explicit `wrapping_*` operations wherever the C++ relies on wraparound.

The port follows [`benchmark.cpp`](benchmark.cpp) line for line rather than
being written idiomatically. A suite compares one algorithm across languages,
so a version that reaches the same answer by a different route measures a
different thing. Every implementation is accepted only when it produces the
checksum listed above.

### What `managed-deref` measures, and what it does not

`managed-deref` no longer measures a dereference in any of the three native
languages, and its numbers should not be quoted as if it did. The loop is an
affine recurrence -- `v = (v * 1664525 + 1013904223 + i) & 0x7fffffff` -- over
a value behind one owning pointer, and LLVM proves the pointer is not aliased,
keeps the value in a register, and strength-reduces the recurrence. Neither the
C++ nor the Absolute binary contains a load or a store in the hot loop; both
multiply in registers.

What is left is a difference in how far that transformation went from two
frontends' IR. The Rust loop advances eight steps per iteration with a single
composed multiplier (`0x6d025688`, which is 1664525 raised to a power modulo
2^32); the C++ and Absolute loops keep the per-step multiplier and unroll
around it. That is why Rust reads roughly 3.7x faster here. It is a real
measurement of the emitted code and it says nothing about the cost of an
owning pointer.

Confirmed by disassembly rather than inferred: the constant in the Rust hot
loop is not 1664525, and the loop counter advances by eight. The time also
scales linearly with the iteration count, so the work is not being skipped --
it is being done in a closed form.

`arena-graph` is the one place the Rust is not the same code. The C++ chases
the arena through raw pointer arithmetic with no bounds check; the safe Rust
indexes, and pays a compare and a branch per step because the index comes out
of the array and the compiler cannot prove it is in range. Writing it with
`get_unchecked` would erase the difference and measure a Rust nobody writes,
so the number below includes the check and this paragraph says so.
