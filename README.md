# Absolute

Absolute is an experimental C++20 compiler frontend. The repository currently
contains a lexer, parser/AST, an early semantic analyzer, and the `absolutec`
command-line driver.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

On multi-config generators such as Visual Studio, pass `--config Debug` to the
build command.

## Run

```bash
./build/Debug/absolutec code.abs
```

For a Release build, replace `Debug` with `Release`.

## Test

```bash
ctest --test-dir build --output-on-failure
```
