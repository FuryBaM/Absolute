# Collection language benchmark

This suite compares the Absolute standard collection library (`std/collections/vector.abs` and `std/collections/hash_map.abs`) with standard dynamic array / list and hash map implementations in C++, C#, Java, JavaScript, and Python. A run is accepted only when it produces the exact checksum listed below.

| Algorithm | Workload | Checksum |
|---|---|---:|
| `vector-push-sum` | Push 1,000,000 `int32` elements into a vector, calculate sum, pop 500,000 elements, sum length | 8499995000000 |
| `vector-sort` | Insertion-sort 100,000 pseudorandom `int32` elements in-place | 43156608257 |
| `hashmap-insert-lookup` | Insert 200,000 key-value pairs into HashMap, perform 200,000 lookups, calculate sum of values | 260000100000 |

## Environment and methodology

- CPU: Intel Core i5-13420H (8 cores, 12 logical processors)
- OS: Windows 11
- Absolute: Compiled with `absolutec --emit-llvm`, Clang 18.1.8 `-O3 -march=native`, linked against `Absolute-Runtime.lib`
- C++: MSVC `cl.exe` (`/O2 /Ob3 /GL /DNDEBUG /LTCG`) using `std::vector` and `std::unordered_map`
- C#: .NET 10.0 Release (`List<T>`, `Dictionary<K,V>`)
- Java: OpenJDK (`ArrayList<Integer>`, `HashMap<Integer,Integer>`)
- JavaScript: Node.js v22 (`Array`, `Map`)
- Python: CPython 3.13 (`list`, `dict`)
- Timing: End-to-end process execution wall time (including runtime startup / JIT compilation)

The table also reports `Absolute unchecked` for `vector-sort`. That variant
reserves the final capacity and holds one `Vector.unsafeData()` raw view while
sorting, matching the unchecked indexing contract of C++
`std::vector::operator[]`. The ordinary `Absolute` row remains the safe
bounds-checked collection API.

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

`HashMap` is the standard one with the hasher a Rust program gets by default.
It is DoS-resistant and therefore slower than the one C++ hands out; swapping
in a faster third-party hasher would measure a library the language does not
ship.

## Benchmark Source Files

- Absolute: [`absolute/vector-push-sum.abs`](absolute/vector-push-sum.abs), [`absolute/vector-sort.abs`](absolute/vector-sort.abs), [`absolute/vector-sort-unsafe.abs`](absolute/vector-sort-unsafe.abs), [`absolute/hashmap-insert-lookup.abs`](absolute/hashmap-insert-lookup.abs)
- C++: [`benchmark.cpp`](benchmark.cpp)
- Rust: [`benchmark.rs`](benchmark.rs)
- C#: [`Program.cs`](Program.cs)
- Java: [`Benchmark.java`](Benchmark.java)
- JavaScript: [`benchmark.js`](benchmark.js)
- Python: [`benchmark.py`](benchmark.py)

## Run the benchmark

From PowerShell or Command Prompt:

```bat
rem Run 11 samples per optimized language, 2 warm-ups, skip Python
benchmarks\collection-suite\run.bat 11 2 0

rem Run with Python included (takes several minutes)
benchmarks\collection-suite\run.bat 11 2 1
```
