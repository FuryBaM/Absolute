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
  "sources": ["src"],
  "nativeLibraries": ["native/MyLibrary.lib"],
  "nativeSearchPaths": ["native"]
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

## C and C++ interop

Absolute can call functions that use the stable C ABI. Declare the native
function without a body:

```absolute
extern "C" int32 native_add(int32 left, int32 right);
```

C code can define that symbol directly. C++ code must expose a small C wrapper
to disable C++ name mangling:

```cpp
extern "C" __declspec(dllexport) int native_add(int left, int right) {
    return left + right;
}
```

On non-Windows platforms omit `__declspec(dllexport)`. Direct C++ ABI imports
(overloads, classes and exceptions) are intentionally not supported yet because
their binary names and rules differ between MSVC and Clang/GCC.

Generate a native object without linking it:

```bash
absolutec build Demo.absproj --emit-object -o Demo.obj
```

Or let Absolute emit the object and call the C++ compiler driver to link the
project's `nativeLibraries`. On a Visual Studio build this uses the configured
`cl.exe`, including the MSVC runtime libraries:

```bash
absolutec build Demo.absproj --build-exe -o Demo.exe
Demo.exe
```

The generated `.obj` is retained next to the executable so it can be inspected
or linked manually. Current FFI types map primitive scalars directly; `string`
is passed as a C `char*`. Ownership stays with the caller, and native exceptions
must not cross the `extern "C"` boundary.

## Pointers and lifetime

`T*` is a managed pointer. A value created with `new` owns a generation-checked
runtime slot; copies are non-owning subscribers. Destroying the owner explicitly
or leaving its scope invalidates every subscriber instead of leaving a dangling
address:

```absolute
int32* owner = new int32(42);
int32* subscriber = owner;
println(*subscriber);
delete owner;
assert(!subscriber);
```

Managed owners use scope-based RAII. `keep` disables automatic destruction, but
an explicit `delete` still works:

```absolute
keep int32* value = new int32(42);
delete value;
```

Use `raw T*` only for C interop or C++-style address operations. Raw pointers do
not participate in generation checks or automatic lifetime management:

```absolute
int32 value = 41;
raw int32* address = &value;
*address = 42;

int32* tracked = new int32(42);
keep int32* kept = new int32(7);
raw int32* unsafe = new int32(11);
```

`new T(...)` has one spelling. The expected pointer type chooses the allocation
model: `T*` creates a tracked owner, `keep T*` creates a tracked owner without
scope RAII, and `raw T*` creates an unsafe native allocation. The same contextual
typing applies to assignments, function arguments, and return expressions.

Pointer types are supported in function parameters and return values. Returning
a managed pointer transfers ownership to the caller, so a function may return a
fresh allocation or one of its local owners. Returning a subscriber is rejected
because its lifetime still belongs to another owner. Native `extern "C"`
signatures must use `raw T*`.

Raw pointers support `+`/`-` integer offsets, pointer differences, ordering, and
null comparisons. Managed pointers deliberately reject address arithmetic; they
support equality and null checks using slot generation validity.

The native runtime library is linked automatically by `--build-exe`. Objects
created by `--emit-object` have to be linked with `Absolute-Runtime` manually
when managed pointers are used.

The first backend milestone supports primitive values, functions, local
variables, calls, casts, arithmetic/comparison operators, assignments,
`return`, `if`, `for`, `while`, `do-while`, `break`, and `continue`. Classes,
user-defined instances, arrays, and `foreach` report explicit codegen errors.

For a Release build, replace `Debug` with `Release`.

## Test

```bash
ctest --test-dir build --output-on-failure
```
