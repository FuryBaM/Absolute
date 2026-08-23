# Multi-algorithm language benchmark

This suite compares Absolute with C++, C#, Java, JavaScript, and Python across
five different CPU workloads. Every implementation must produce the checksum
shown below before its timing is accepted.

| Algorithm | Workload | Checksum |
|---|---|---:|
| `mix` | 100,000,000 32-bit LCG/xor iterations | 634231978 |
| `primes` | Trial division through 2,000,000 | 148933 |
| `collatz` | Total stopping steps for seeds 1 through 2,000,000 | 277182223 |
| `gcd` | 5,000,000 Euclidean GCD calculations | 68856518 |
| `floating` | 100,000,000 dependent `double` iterations | 115032218 |

## Environment and methodology

- CPU: Intel Core i5-13420H (8 cores, 12 logical processors)
- OS: Windows 11, build 26200
- Absolute and C++: LLVM/Clang 18.1.3, `-O3 -march=native`
- Rust: `rustc -C opt-level=3 -C target-cpu=native -C overflow-checks=off`
- C++ floating-point contraction: disabled to match Absolute's separate
  multiply and add semantics
- C#: .NET SDK 10.0.103, `Release`
- Java: Temurin OpenJDK 25.0.1
- JavaScript: Node.js 22.14.0
- Python: CPython 3.13.2
- Timing: end-to-end process wall time, including runtime startup and JIT
  compilation
- Samples: median of 11 runs for optimized/JIT implementations and 3 runs for
  Python

## Median time in seconds

| Algorithm | Absolute | C++ | C# | Java | JavaScript | Python |
|---|---:|---:|---:|---:|---:|---:|
| Integer mix | 0.147244 | 0.145990 | 0.169030 | 0.187732 | 0.181477 | 19.739199 |
| Primes | 0.134144 | 0.135308 | 0.161116 | 0.194967 | 0.183608 | 5.068812 |
| Collatz | 0.256813 | 0.254125 | 0.470755 | 0.511586 | 2.680235 | 20.385931 |
| GCD | 0.333992 | 0.344401 | 0.351811 | 0.396375 | 0.393157 | 3.250100 |
| Floating point | 0.410738 | 0.403130 | 0.205822 | 0.253350 | 0.265703 | 4.510869 |

## Relative time versus Absolute

Lower is faster. The geometric mean gives every algorithm equal weight.

| Language | Mix | Primes | Collatz | GCD | Floating | Geometric mean |
|---|---:|---:|---:|---:|---:|---:|
| Absolute | 1.00x | 1.00x | 1.00x | 1.00x | 1.00x | 1.00x |
| C++ | 0.99x | 1.01x | 0.99x | 1.03x | 0.98x | 1.00x |
| C# | 1.15x | 1.20x | 1.83x | 1.05x | 0.50x | 1.06x |
| Java | 1.27x | 1.45x | 1.99x | 1.19x | 0.62x | 1.22x |
| JavaScript | 1.23x | 1.37x | 10.44x | 1.18x | 0.65x | 1.68x |
| Python | 134.06x | 37.79x | 79.38x | 9.73x | 10.98x | 33.62x |

This recorded suite predates the array backend. Memory traversal, random access,
and sorting are measured separately in the
[`array-suite`](../array-suite/README.md). Allocation, strings, async work, and
larger application workloads still need their own benchmarks.

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

The floating-point loop is written as a separate multiply and add, for the
same reason the C++ build passes `-ffp-contract=off`: Absolute emits the two
instructions, and a fused multiply-add would measure different arithmetic
rather than a faster compiler.
