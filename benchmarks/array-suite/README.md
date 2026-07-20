# Array language benchmark

This suite compares the new Absolute array backend with C++, C#, Java,
JavaScript, and Python. A run is accepted only when it produces the checksum
listed below.

| Algorithm | Workload | Checksum |
|---|---|---:|
| `scan` | Initialize 1,000,000 `int32` values, then update and sum them for 100 passes | 1063467781802240 |
| `random-access` | 100,000,000 LCG-selected reads, xor writes, and sums in 1,048,576 `int32` values | 268750506018432 |
| `insertion-sort` | Generate and insertion-sort 30,000 pseudorandom `int32` values | 312098439810 |

## Environment and methodology

- CPU: Intel Core i5-13420H (8 cores, 12 logical processors)
- OS: Windows 11, build 26200
- Absolute and C++: LLVM/Clang 18.1.3, `-O3 -march=native`
- Native executable stack reserve: 64 MiB, because Absolute local arrays are
  currently stack-allocated
- C#: .NET SDK 10.0.103, `Release`
- Java: OpenJDK 25.0.1
- JavaScript: Node.js 22.14.0 using `Int32Array`
- Python: CPython 3.13.2 using built-in lists
- Timing: end-to-end process wall time, including runtime startup and JIT
  compilation
- Optimized/JIT samples: median of 15 measured runs after two unmeasured runs;
  language order was rotated on every round
- Python samples: one measured run because the same workloads take much longer

The implementations use the same input generator, integer overflow behavior,
algorithm, and checksum. Their storage models are necessarily different:
Absolute and C++ use contiguous stack arrays, C#/Java use managed arrays,
JavaScript uses a typed array, and Python uses a list of Python integers.
Absolute also emits bounds checks for every array access; unchecked C++ does
not. This is a language/runtime comparison, not a claim that all containers
have identical allocation or representation costs.

## Time in seconds

Lower is faster. Optimized/JIT columns are medians; Python is a single sample.

| Algorithm | Absolute | C++ | C# | Java | JavaScript | Python |
|---|---:|---:|---:|---:|---:|---:|
| Linear scan | 0.021794 | 0.021796 | 0.072821 | 0.090617 | 0.132540 | 29.409872 |
| Random access | 0.130017 | 0.132879 | 0.169258 | 0.175350 | 0.229965 | 104.683153 |
| Insertion sort | 0.087989 | 0.066428 | 0.110120 | 0.159705 | 0.194358 | 22.793002 |

## Relative time versus Absolute

Lower is faster. The geometric mean gives every algorithm equal weight.

| Language | Linear scan | Random access | Insertion sort | Geometric mean |
|---|---:|---:|---:|---:|
| Absolute | 1.00x | 1.00x | 1.00x | 1.00x |
| C++ | 1.00x | 1.02x | 0.75x | 0.92x |
| C# | 3.34x | 1.30x | 1.25x | 1.76x |
| Java | 4.16x | 1.35x | 1.82x | 2.17x |
| JavaScript | 6.08x | 1.77x | 2.21x | 2.87x |
| Python | 1349.45x | 805.15x | 259.04x | 655.34x |

On these three workloads, Absolute is effectively tied with C++ for linear and
random access. C++ wins insertion sort by about 25% in elapsed time (Absolute
takes about 1.32x as long), while Absolute remains faster than the managed and
scripting implementations in this process-level test. Three algorithms are a
useful backend check, not a general-purpose language ranking.

## Run the benchmark

From PowerShell or Command Prompt, launch:

```bat
benchmarks\array-suite\run.bat
```

The batch file finds the Visual Studio x64 toolchain, builds an LLVM-enabled
Release version of Absolute natively on Windows, compiles every language implementation,
validates every checksum, and writes a timestamped CSV file to `results\`.
Generated compilers and executables are cached in `.benchmark-build\`, so the
next run is incremental.

Optional positional arguments are measured samples, warm-up samples, and
whether to include Python (`1` or `0`):

```bat
rem Quick run: 3 samples, 1 warm-up, skip Python
benchmarks\array-suite\run.bat 3 1 0

rem Recorded methodology: 15 samples, 2 warm-ups, include Python
benchmarks\array-suite\run.bat 15 2 1

rem Optional legacy WSL backend
benchmarks\array-suite\run.bat 15 2 1 wsl
```

The full Python run takes several minutes. Set
`ABSOLUTE_BENCHMARK_NO_PAUSE=1` before launching when the final `pause` is not
desired, for example in CI or another script.

## Sources

- Absolute programs: [`absolute/`](absolute/)
- C++: [`benchmark.cpp`](benchmark.cpp)
- C#: [`Program.cs`](Program.cs)
- Java: [`Benchmark.java`](Benchmark.java)
- JavaScript: [`benchmark.js`](benchmark.js)
- Python: [`benchmark.py`](benchmark.py)
- Runner: [`run.bat`](run.bat)
