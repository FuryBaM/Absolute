# Absolute language speed benchmark

This benchmark runs the same 32-bit integer workload in Absolute, C++, C#,
Java, JavaScript, and Python. Each measured process performs 100,000,000 LCG
and xor-mixing iterations and must print the checksum `634231978`.

## Test machine

- CPU: Intel Core i5-13420H (8 cores, 12 logical processors)
- OS: Windows 11, build 26200
- Absolute: LLVM 18 IR compiled to a Windows executable with Clang 18 `-O3`
- C++: MSVC 14.50, `/O2 /Ob3 /DNDEBUG`
- C#: .NET SDK 10.0.103, `Release`
- Java: Temurin OpenJDK 25.0.1
- JavaScript: Node.js 22.14.0
- Python: CPython 3.13.2

## Results

The table reports end-to-end process wall time. It therefore includes process
startup and JIT compilation. Absolute, C++, C#, Java, and JavaScript use the
median of 15 runs; Python uses the median of 3 runs because it is much slower.

| Language | Median | Minimum | Relative to Absolute | Million iterations/s |
|---|---:|---:|---:|---:|
| Absolute | 0.143199 s | 0.142394 s | 1.00x | 698.33 |
| C++ | 0.144312 s | 0.142866 s | 1.01x | 692.94 |
| C# | 0.167570 s | 0.165366 s | 1.17x | 596.77 |
| JavaScript | 0.178157 s | 0.175912 s | 1.24x | 561.30 |
| Java | 0.189978 s | 0.182741 s | 1.33x | 526.38 |
| Python | 26.723751 s | 25.560200 s | 186.62x | 3.74 |

These results only characterize this tight integer loop. They are not a broad
language ranking: allocation, function calls, arrays, strings, I/O, and larger
programs can produce a different order.
