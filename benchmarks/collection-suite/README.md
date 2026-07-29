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

## Benchmark Source Files

- Absolute: [`absolute/vector-push-sum.abs`](absolute/vector-push-sum.abs), [`absolute/vector-sort.abs`](absolute/vector-sort.abs), [`absolute/vector-sort-unsafe.abs`](absolute/vector-sort-unsafe.abs), [`absolute/hashmap-insert-lookup.abs`](absolute/hashmap-insert-lookup.abs)
- C++: [`benchmark.cpp`](benchmark.cpp)
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
