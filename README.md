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
  "plugins": ["plugins/absolute-unless.dll"],
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

## Syntax plugins

New keywords and syntax that can be expressed using existing Absolute constructs
can live in native plugins instead of the lexer, parser, analyzer, or LLVM
backend. A plugin registers one or more trigger keywords through the versioned C
ABI in `Absolute-Parser/include/plugin_api.h`. Its adapter receives the token
stream beginning at that keyword, consumes its syntax, and returns ordinary
Absolute source. The compiler tokenizes that result again before building the
AST, so plugins compose and all normal semantic checks still apply.

The included `absolute.unless` example adds this syntax without changing the
core grammar:

```absolute
unless (ready) {
    initialize();
}
```

It lowers to `if (!(ready)) { initialize(); }`. Build and load it with:

```bash
cmake --build build --config Release --target Absolute-Unless-Plugin
absolutec program.abs --plugin path/to/absolute-unless.dll
```

On Linux the library uses the `.so` extension. `--plugin` may be repeated. A
project can instead list paths relative to its `.absproj` file:

```json
{
  "name": "PluginDemo",
  "entry": "src/main.abs",
  "sources": ["src"],
  "plugins": ["plugins/absolute-unless.dll"]
}
```

To create another adapter, copy `plugins/unless`, implement an
`AbsoluteSyntaxExpandV1` callback, declare its `AbsoluteSyntaxRuleV1` entries,
and export `absolute_syntax_plugin_init_v1`. Callback-owned replacement and
error strings must remain valid until that adapter is invoked again; the host
copies them before the next invocation. No C++ AST objects or allocator-owned
memory cross the DLL boundary.

A plugin may additionally export `absolute_syntax_plugin_prelude_v1`. The
returned Absolute source is parsed as a module before project sources. This is
useful for libraries that add declarations and implementations without an
artificial marker keyword. The included `absolute.math` plugin uses this hook to
provide `Math.abs`, `Math.sqrt`, `Math.sin`, `Math.cos`, `vec2`, `vec3`, `mat3`,
`mat4`, `Math.projection`, and `Math.lookAt`. Vector and matrix
type aliases are available without `Math.`, including plugin-defined `+`, `-`,
scalar multiplication, and matrix multiplication.
See `plugins/math/README.md` for the complete API and matrix conventions.

Plugins cannot replace core keywords, duplicate another plugin's keyword, or
load with a different ABI version. Recursive expansion is bounded and produces
a compiler error. Plugins are native code and should only be loaded from trusted
sources. Syntax plugins deliberately lower into the core language; operations
that require native behavior can pair the syntax adapter with an `extern "C"`
library while keeping analyzer and code generation unchanged.

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

Managed owners use scope-based RAII. Ownership is a compile-time role: a managed
variable is either an owner or a non-owning subscriber and cannot switch between
those roles. An explicit `delete` still works:

```absolute
int32* value = new int32(42);
delete value;
```

Use `raw T*` only for C interop or C++-style address operations. Raw pointers do
not participate in generation checks or automatic lifetime management:

```absolute
int32 value = 41;
raw int32* address = &value;
*address = 42;

int32* tracked = new int32(42);
raw int32* unsafe = new raw int32(11);
```

The expected pointer type chooses the allocation model: `new T(...)` assigned to
`T*` creates a tracked RAII owner, while `new raw T(...)` assigned to `raw T*`
creates an unsafe native allocation. A raw allocation created directly in a
local variable must be passed to `delete` on every path before its scope is left,
including `return`, `break`,
and `continue`. Missing cleanup, overwriting a live raw owner, and double deletion
produce stable diagnostic codes (`E_RAW_DELETE_REQUIRED`, `E_RAW_OVERWRITE`, and
`E_RAW_DOUBLE_DELETE`) through the analyzer API for IDE integrations. Borrowed raw
addresses and raw values returned by external/native functions remain explicitly
unsafe and are not automatically reclaimed.

The analyzer also performs control-flow dataflow for definite assignment and
pointer validity. Branch and loop states are merged by `SymbolId`; managed
subscribers retain their owner identity and become `Expired` when that owner is
deleted. It reports reads before initialization, missing returns, null/deleted/
expired dereferences, deleting subscribers, and operations on pointers that are
only valid on some paths. `ExpressionInfo` exposes `InitializationState`,
`PointerValidity`, and `pointerOwner` for IDE hover and diagnostics.

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

## Async tasks and parallel execution

An `async` function can be scheduled on the runtime worker pool with `spawn`.
The returned `task<T>` is a one-shot handle whose result is consumed by
`await`:

```absolute
async int32 calculate(int32 value) {
    return value * 2;
}

async int32 main() {
    task<int32> first = spawn calculate(20);
    task<int32> second = spawn calculate(1);
    int32 result = await first + await second;
    return result;
}
```

`await` is only valid inside an `async` function. Every local task must be
awaited on every control-flow path before its scope is left, including through
`return`, `break`, and `continue`; a task cannot be copied, reassigned, or
awaited twice. The analyzer exposes `TaskState` in `ExpressionInfo` and emits
stable `E_TASK_*` diagnostics for IDE integrations.

The LLVM backend packs primitive arguments into an owned task context and emits
a private thunk for each spawn site. `Absolute-Runtime` executes these thunks
on a shared native thread pool, and `await` suspends the calling OS thread until
the result is ready. This first concurrency milestone supports primitive and
pointer-shaped ABI values; cancellation, channels, async I/O, methods, and
compile-time data-race checking are planned separately.

## Arrays

Local arrays support fixed or runtime dimensions, rectangular literals,
inferred literal sizes, multidimensional row-major indexing, and element
assignment:

```absolute
int32 values[4] = {10, 20, 30, 40};
values[2] += 12;

int32 matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
matrix[1][0] = values[2];

int32 inferred[] = {7, 8, 9};
int32 length = 16;
int32 buffer[length];
```

Array storage is zero-initialized. Dimensions must be positive, every access
must provide the complete index list, and generated code checks each index at
runtime. An invalid size or out-of-bounds index prints a diagnostic and exits
with a nonzero status. Literal shapes are checked statically for rank,
rectangularity, and exact fixed dimensions.

Code generation removes checks that are already true at compile time and emits
`inbounds` addressing after every successful check. Native object and executable
generation runs LLVM's `O3` pipeline for the host CPU, allowing loop and scalar
evolution passes to remove further redundant checks while preserving the
runtime failure path for indexes that cannot be proven safe.

Arrays can be passed to functions as compact data-and-dimensions descriptors.
The canonical parameter and return syntax is `T[]` (the older `T name[]`
parameter spelling remains accepted):

```absolute
int32 sum(int32[] values) {
    int32 result = 0;
    foreach (int32 value in values) {
        result += value;
    }
    return result;
}

int32[] tail(int32[] values) {
    return values[1:];
}
```

One-dimensional slices are half-open: `values[from:to]` includes `from` and
excludes `to`. Either bound can be omitted, and `values[]` creates a view of the
complete array. Slices do not copy their elements, so writes through a slice
modify its source and the source must remain alive while the slice is used.
Slice bounds are checked at runtime. `foreach` currently iterates
one-dimensional arrays by value.

Global arrays use the same sized or inferred literal declarations as local
storage:

```absolute
int32 primes[4] = {2, 3, 5, 7};
int32[] flags = {1, 0, 1};
```

Their dimensions and initializer values must be compile-time primitive
constants. Returning an array makes a heap-backed copy, including when the
source is a local array or a slice; automatic reclamation of these returned
buffers is not implemented yet.

The backend also supports primitive values, functions, local variables, calls,
casts, arithmetic/comparison operators, assignments, `return`, `if`, `for`,
`while`, `do-while`, `foreach`, `break`, and `continue`. User-defined classes
support fields, local value instances, constructors, raw or managed allocation,
single inheritance, instance methods, and `virtual`/`override` dispatch. Multiple
inheritance, automatic destructor calls, interfaces, and static class members are
not emitted yet. Base-constructor chaining is also not automatic. Raw object
graphs that own child nodes must release them explicitly before `delete`.

For a Release build, replace `Debug` with `Release`.

## Test

```bash
ctest --test-dir build --output-on-failure
```
