# Absolute

Absolute is an experimental C++20 compiler frontend. The repository currently
contains a lexer, parser/AST, a two-pass semantic analyzer, an LLVM IR backend,
and the `absolutec` command-line driver.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

LLVM development libraries and an LLVM CMake package are required. If CMake
cannot locate them automatically, pass `-DLLVM_DIR="$(llvm-config --cmakedir)"`.
The frontend can still be built without LLVM using
`-DABSOLUTE_ENABLE_LLVM=OFF`. The prebuilt Windows LLVM installer does not ship
the C++ CMake development package, so Windows builds need a full LLVM development
build and its `LLVM_DIR`; CI keeps validating the Windows frontend separately.

On multi-config generators such as Visual Studio, pass `--config Debug` to the
build command.

## Run

```bash
./build/Debug/absolutec code.abs --parse-only
```

Create and build a project:

```bash
absolutec new Demo
absolutec build Demo/Demo.absproj
absolutec build Demo/Demo.absproj --emit-llvm -o Demo.ll
lli Demo.ll
```

An `.absproj` file describes the entry source and directories compiled into one
module:

```json
{
  "name": "Demo",
  "entry": "src/main.abs",
  "sources": ["src"]
}
```

Projects support recursive file imports and namespace imports:

```absolute
import "../shared/math.abs";
import Demo.Math;

namespace Demo.Math {
    int32 add(int32 left, int32 right) { return left + right; }
}
```

Every `.abs` file under `sources` is compiled automatically. A quoted import can
bring in an additional file relative to the importing source. Namespace imports
allow short references such as `add(20, 22)`; fully-qualified calls such as
`Demo.Math.add(20, 22)` also work.

Emit verified textual LLVM IR:

```bash
./build/Debug/absolutec tests/llvm-basic.abs --emit-llvm -o output.ll
```

Without `--parse-only`, the compiler resolves declarations and checks every
variable, function, type, field, and method use before LLVM IR generation.
Semantic diagnostics return a failure status, so invalid objects cannot reach
the backend.

The runtime built-ins can be used without declarations:

```absolute
print("before=", value, " ");
println(format("after={}", value));
string text = toString(value);
assert(value == 42, "unexpected value");
```

`print` and `println` accept any number of scalar values. `format` uses `{}`
placeholders (`{{` and `}}` produce literal braces) and currently requires a
literal template. They lower to libc calls in LLVM IR, which makes the emitted
module directly runnable with `lli`.

The first backend milestone supports primitive values, functions, local
variables, calls, casts, arithmetic/comparison operators, assignments,
`return`, `if`, `for`, `while`, `do-while`, `break`, and `continue`. Classes,
user-defined instances, arrays, and `foreach` report explicit codegen errors.

For a Release build, replace `Debug` with `Release`.

## Test

```bash
ctest --test-dir build --output-on-failure
```
