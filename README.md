# Absolute

Absolute is an experimental C++20 compiler frontend. The repository currently
contains a lexer, parser/AST, an early semantic analyzer, an LLVM IR backend,
and the `absolutec` command-line driver.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

LLVM development libraries and an LLVM CMake package are required. If CMake
cannot locate them automatically, pass `-DLLVM_DIR="$(llvm-config --cmakedir)"`.

On multi-config generators such as Visual Studio, pass `--config Debug` to the
build command.

## Run

```bash
./build/Debug/absolutec code.abs
```

Emit verified textual LLVM IR:

```bash
./build/Debug/absolutec tests/llvm-basic.abs --emit-llvm -o output.ll
```

The first backend milestone supports primitive values, functions, local
variables, calls, casts, arithmetic/comparison operators, assignments,
`return`, `if`, `for`, `while`, `do-while`, `break`, and `continue`. Classes,
user-defined instances, arrays, and `foreach` report explicit codegen errors.

For a Release build, replace `Debug` with `Release`.

## Test

```bash
ctest --test-dir build --output-on-failure
```
