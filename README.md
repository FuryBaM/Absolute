# Absolute

Absolute is an experimental C++20 compiler frontend. The repository currently
contains a lexer, parser/AST, a two-pass semantic analyzer, an LLVM IR backend,
and the `absolutec` command-line driver.

## Quick start

Build the compiler once, then create either an application or a library:

```powershell
.\absolute build-compiler --bootstrap

.\absolute new Demo --type app
.\absolute run Demo

.\absolute new Math --type lib
.\absolute build Math
```

Inside a project, the manifest path is optional. `absolute build`, `run`,
`clean`, and `info` find the nearest `.absproj` automatically. Application
artifacts and native libraries are placed under the project's `build`
directory; `absolute clean` removes only that directory.

Project types:

- `app` creates an executable project with `src/main.abs`;
- `lib` creates a native shared library with `src/lib.abs` and an exported C
  ABI function.

## Build

The shortest native Windows workflow is:

```powershell
# First build; downloads the portable LLVM SDK once.
.\absolute build-compiler --bootstrap

# Later compiler rebuilds.
.\absolute build-compiler

# Build and run the complete test suite.
.\absolute build-compiler --test
```

The root `absolute.bat` launcher finds the compiler in the repository build
directories and initializes the MSVC environment when native linking needs it.
Set `ABSOLUTEC` to use a compiler executable from another location.
The convenient `build-compiler` command skips tests unless `--test` is passed.

The underlying build commands remain available:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

LLVM development libraries and an LLVM CMake package are required. If CMake
cannot locate them automatically, pass `-DLLVM_DIR="$(llvm-config --cmakedir)"`.
The frontend can still be built without LLVM using
`-DABSOLUTE_ENABLE_LLVM=OFF`.

For a complete native Windows build without WSL, run:

```bat
build-windows.bat --bootstrap
```

This installs a portable LLVM 18.1.8 SDK under the ignored `.absolute`
directory, builds the compiler and DLL plugins with MSVC, and runs the native
tests. Later builds use `build-windows.bat` without `--bootstrap`. See
[`docs/windows-build.md`](docs/windows-build.md) for toolchain overrides and
benchmark commands. The host platform matrix and CI coverage are summarized in
[`docs/platforms.md`](docs/platforms.md).

On multi-config generators such as Visual Studio, pass `--config Debug` to the
build command.

Release presets are available for WSL and native Windows:

```bash
# Run inside WSL. Build artifacts stay on the Linux filesystem.
cmake --preset wsl-release
cmake --build --preset wsl-release --parallel 4
ctest --preset wsl-release --parallel 4

# Use this variant after installing Ninja in WSL.
cmake --preset wsl-release-ninja
```

```powershell
# Windows presets use Ninja + MSVC (same layout as build-windows.bat).
# Run from an x64 Native Tools / Developer PowerShell so cl.exe is on PATH.
# Prefer build-windows.bat when you want vswhere + vcvars handled for you.
# Bootstrapped ninja: .absolute\toolchains\ninja\ninja.exe
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
```

Set `-DABSOLUTE_USE_COMPILER_CACHE=ON` to enable `sccache` or `ccache` when one
of them is installed. To compare clean, incremental, and PCH build times, run:

```bat
benchmarks\build-suite\run.bat
```

The native Windows backend is the default. Use
`benchmarks\build-suite\run.bat 4 linux wsl` for the older WSL measurement.

## Run

Compile a source file to a native executable, or compile and run it immediately:

```powershell
.\absolute compile hello.abs
.\absolute hello.abs
.\absolute compile hello.abs --output bin\hello.exe
.\absolute run hello.abs
.\absolute run hello.abs -- first-argument "second argument"
```

Arguments before `--` belong to the compiler; arguments after it are passed to
the compiled program. Project files work the same way, but can be omitted while
the current directory is inside a project:

```powershell
absolute build
absolute run -- argument-for-demo
absolute info
absolute clean
```

Standalone file artifacts stay under the nearby hidden `.absolute/out`
directory instead of cluttering the source directory.

An `.abs` or `.absproj` path can be used directly as a shorthand for
`compile`. Use `.\absolute compiler ...` to pass arguments directly to
`absolutec`, for example `.\absolute compiler code.abs --parse-only`. Run
`.\absolute --help` for all developer commands.

The compiler can also be invoked directly:

```bash
absolutec --version
./build/Debug/absolutec code.abs --parse-only
```

Application files may use script-style top-level code without declaring
`main` explicitly:

```absolute
int32 value = 40;
value += 2;
println(format("value={}", value));
```

The compiler moves top-level executable statements and their variable
declarations into a hidden `int32 main()` and appends `return 0`. Functions,
types, namespaces, imports, and plugin declarations remain at module scope. An
explicit `main` keeps the traditional mode; combining it with executable
top-level statements is a compile-time error instead of creating a second
entry point. Module-level declarations may still accompany an explicit `main`.

Create application and library projects:

```powershell
absolute new Demo --type app
absolute new Math --type lib
```

An `.absproj` file describes the entry source and directories compiled into one
module:

```json
{
  "name": "Demo",
  "type": "app",
  "entry": "src/main.abs",
  "sources": ["src"],
  "runArgs": ["--mode=fast"],
  "plugins": ["plugins/absolute-unless.dll"],
  "nativeLibraries": ["native/MyLibrary.lib"],
  "nativeSearchPaths": ["native"]
}
```

`type` is optional for older projects and defaults to `app`. Library projects
use `"type": "lib"`; `absolute build` produces `Name.dll` on Windows,
`libName.so` on Linux, or `libName.dylib` on macOS. Use `export "C"` functions
as the public library API. Application `runArgs` are passed by `absolute run`
and exposed without the executable path through `std.env.argsCount()`,
`std.env.argAt()`, `std.env.flag()`, and `std.env.parameter()`.

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

## Overloads and extension methods

Functions and class methods may share a name when their parameter type lists
are different. Calls prefer exact type matches, then compatible conversions; a
tie is reported as an ambiguous overload instead of choosing silently.

```absolute
int32 clamp(int32 value, int32 minimum, int32 maximum) { /* ... */ }
double clamp(double value, double minimum, double maximum) { /* ... */ }

class Calculator {
    public int32 add(int32 left, int32 right) { return left + right; }
    public double add(double left, double right) { return left + right; }
}
```

An extension method is a global or namespace function marked `extension`. Its
first parameter is the receiver and is omitted at the call site:

```absolute
extension int32 squared(int32 value) {
    return value * value;
}

int32 value = 7;
println(value.squared());
```

Global extensions are immediately visible. Namespace extensions become visible
after `import NamespaceName;`. Real instance methods take precedence over
extensions with the same name.

## Switch, match, and enums

Enum members are typed constants and use qualified names. `switch` and `match`
cases do not fall through, so `break` is unnecessary:

```absolute
enum Direction { North, East, South, West }

int32 score(Direction direction) {
    match (direction) {
    case Direction.North:
        return 10;
    case Direction.East:
        return 20;
    case Direction.South:
        return 30;
    case Direction.West:
        return 40;
    }
}
```

`match` must be exhaustive. Boolean matches require both `true` and `false`,
enum matches require every member, and integer/character matches require a
`default` branch. Ordinary `switch` may be partial; its optional `default`
branch handles values not listed by a case. Case labels must be compile-time
boolean, integer, character, or enum constants, and duplicates are rejected.

## Error model

Absolute uses typed, unchecked exceptions for exceptional failures. All thrown
objects derive from the managed `Error` base class; throwing transfers ownership
to the exception runtime, and a matching catch takes that owner. A future
`Result<T, E>` is an ordinary generic library type for expected failures, not a
hidden propagation ABI. Native C++ exceptions may not cross an `extern "C"` or
plugin boundary, and an Absolute error must not escape an `export "C"` body.
The accepted syntax, cleanup rules, portable LLVM/runtime ABI, and async behavior
are specified in [docs/error-model.md](docs/error-model.md).

The compiler accepts `throw expression;`, `throw;`, ordered typed `catch`
clauses, and optional `finally`. `finally` runs on normal completion,
propagation, `return`, `break`, and `continue`; the first version rejects a new
control transfer from inside `finally`. Unhandled errors reaching `main` are
reported and return a non-zero process status. Exceptions raised by an async
task are restored by `await` and can be caught normally.

`defer` registers either a statement or block for LIFO execution when the
current scope exits. It runs for normal completion and every control-transfer
path, and can satisfy raw-owner deletion and task-await lifetime checks. See
[docs/defer.md](docs/defer.md) for syntax and ordering rules.

## Attributes

Declarations and opaque plugin blocks can carry compile-time attributes such
as `@inline`, `@noinline`, `@deprecated("message")`, or qualified plugin
metadata like `@shader.stage(Vertex)`. Arguments are constant positional or
named values. Qualified attributes are passed to opaque plugins during
validation and LLVM emission. See [docs/attributes.md](docs/attributes.md) for
the target and argument rules.

## Generics

Functions, structs, and classes support compile-time type parameters. Function
arguments may be inferred or written explicitly, while every used aggregate
specialization receives its own native layout and methods. CodeGen emits only
used monomorphizations, without runtime boxing. See
[docs/generics.md](docs/generics.md) for syntax and current constraint limits.

## Tuples and variadic parameters

Structural `tuple<T...>` values use `(a, b, ...)` literals and expose
zero-based `item0`, `item1`, ... members plus read-only `length`/`count`.
Functions, methods, and extension methods may declare a final
`params T[] args` parameter. Expanded arguments use caller-side stack storage;
an existing array is passed directly. See
[docs/tuples-and-params.md](docs/tuples-and-params.md).

## Type aliases and const

Core type aliases use `using Name = Type;` and are transparent during semantic
analysis and LLVM lowering. Variables, fields, and parameters accept prefix
`const`; non-mutating methods use trailing `const` after their parameter list.
See [docs/type-aliases-const.md](docs/type-aliases-const.md) for the exact
shallow-const rules and examples.

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

A manifest can load ordinary Absolute source before project sources:

```json
{
  "library": "my-plugin.dll",
  "prelude": "my-plugin.prelude.abs"
}
```

Use `preludes: ["native.abs", "api.abs"]` to split a large API. These files are
resolved relative to the `.absplugin` manifest and are indexed by the Absolute
extension for completion and hover. This is useful for libraries that add
declarations and implementations without an artificial marker keyword.

The legacy `absolute_syntax_plugin_prelude_v1` native export remains supported
when a prelude truly must be generated by C++. See
`docs/plugin-manifests.md` for file preludes and `plugins/math/README.md` for
the math API.

Plugins cannot replace core keywords, duplicate another plugin's keyword, or
load with a different ABI version. Recursive expansion is bounded and produces
a compiler error. Plugins are native code and should only be loaded from trusted
sources. Syntax plugins deliberately lower into the core language; operations
that require native behavior can pair the syntax adapter with an `extern "C"`
library while keeping analyzer and code generation unchanged.

For syntax that cannot lower to ordinary Absolute source, a plugin may export
`absolute_syntax_plugin_opaque_rules_v1`. An opaque rule receives a versioned
`AbsoluteParserCursorV1` with `peek`, `consume`, and `remaining`, then returns a
plugin-owned `AbsoluteOpaqueAstNodeV1`. Its vtable controls destruction, debug
printing, semantic validation, and LLVM emission.

LLVM C++ objects do not cross the DLL boundary. The opaque emitter returns a
complete textual LLVM IR module; the compiler parses it in the destination LLVM
context, applies the target triple/data layout, verifies it, and links it into
the main module. The included `absolute.shader` example therefore parses this
block itself—there is no generated Absolute source:

```absolute
shader Vertex {
    input position;
    output clipPosition;
}
```

See `plugins/shader/README.md` and `tests/shader-plugin.abs` for the complete
opaque-AST example.

### Plugin dependencies

Use a versioned `.absplugin` manifest instead of a direct DLL/SO path when a
plugin depends on other plugins:

```json
{
  "name": "absolute.shader.opengl",
  "version": "1.2.0",
  "abi": 1,
  "library": "absolute-shader-opengl.dll",
  "dependencies": {
    "absolute.shader": ">=1.0.0 <2.0.0",
    "absolute.math": "^1.1.0"
  }
}
```

The compiler resolves dependencies recursively, checks semantic versions and
ABI, reports cycles and conflicts, and loads dependencies in topological order.
Use `--plugin-path directory` for command-line search paths, or add
`"pluginSearchPaths": ["plugins"]` to an `.absproj`. Explicit dependency paths,
capability `provides`/`requires`, and the full manifest format are documented in
`docs/plugin-manifests.md`.

### Desktop windows and graphics

The `absolute.desktop` plugin provides a native Win32 or X11 window, event loop,
keyboard/mouse input, timing, and a 32-bit software framebuffer. It is enough to
build desktop tools, 2D games, renderers, and the platform layer for a future GPU
backend without adding Win32 code to the compiler core.

```absolute
auto window = new Desktop.Window("Absolute", 800, 450, true);
while (window.poll()) {
    window.clear(Desktop.rgb(18, 22, 32));
    window.fillRect(100, 100, 200, 80, Desktop.rgb(70, 150, 255));
    window.present();
}
window.close();
```

See `plugins/desktop/README.md` and `examples/desktop/window.abs` for build and
run commands. The generated `.absplugin` manifest automatically links its native
runtime and Win32 libraries.

### Playable chess example

`examples/chess` contains a complete interactive console game implemented in
Absolute. It validates piece movement, check, checkmate, stalemate, castling,
en passant, and queen promotion. Run it on Windows with:

```bat
examples\chess\run.bat
```

Moves use coordinate notation such as `e2e4`; enter `q` to quit.

### VS Code extension

`absolute-extension` provides plugin-aware completion, hover information,
semantic highlighting, manifest diagnostics, build/run commands, and native
debugging through `cppvsdbg` or `cppdbg`. It reads `.absproj` plugin roots,
follows `.absplugin` dependencies, and consumes their safe `editor` JSON
sidecars without loading native DLL/SO code into VS Code.

Package and install the VS Code extension:

```powershell
cd absolute-extension
npx @vscode/vsce package
code --install-extension absolute-extension-0.3.3.vsix --force
```

Use `F5` to build and debug or `Ctrl+F5` to build and run. Compiler paths,
custom build commands, extra plugin roots, search paths, and debugger selection
are available under the `absolute.*` VS Code settings.

`.absproj` files are directly runnable. Use `Absolute: Open Project` to open a
project folder, or right-click an `.absproj` and choose Run/Debug Project.

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

Absolute language interop is **C ABI only**. Direct C++ ABI (mangling, classes,
overloads, C++ exceptions) is not supported; expose a thin `extern "C"` shim
from C++. The normative type and ownership rules are in
[`docs/native-c-abi.md`](docs/native-c-abi.md).

Absolute can call functions that use the stable C ABI. Declare the native
function without a body:

```absolute
extern "C" int32 native_add(int32 left, int32 right);
```

The opposite direction uses `export "C"`: Absolute supplies the body and emits
an unmangled symbol that native C or C++ code can call:

```absolute
export "C" int32 absolute_add(int32 left, int32 right) {
    return left + right;
}
```

On Windows the compiler marks this function `dllexport`; on other targets it
uses default external visibility. Exported functions cannot be overloaded or
generic and cannot have default parameters. Both `extern "C"` and `export "C"`
reject managed pointers and Absolute array descriptors at the boundary; use raw
pointers plus explicit lengths for native buffers.

C code can define that symbol directly. C++ code must expose a small C wrapper
to disable C++ name mangling:

```cpp
extern "C" __declspec(dllexport) int native_add(int left, int right) {
    return left + right;
}
```

On non-Windows platforms omit `__declspec(dllexport)`. Absolute rejects
`extern "C++"` / `export "C++"` at parse time; C++ libraries must provide a C
wrapper. Absolute `struct`/`class`/`interface` values, managed pointers, and
array descriptors cannot cross the C boundary—use `raw T*` and scalars.
C callbacks use `cfunc<Return, Params...>` (raw function pointers from
`extern`/`export "C"` only). Native handles wrap `raw void*` in a resource
`struct` with `destroy()`. See `docs/native-c-abi.md`.

Generate Absolute declarations from a C header (requires `clang`):

```bat
node tools/absolute-bindgen.js native/api.h -o native/api.abs
absolute bindgen native/api.h -o native/api.abs
```

### WebAssembly

Absolute has an experimental **wasm32** backend (`wasm-ld` from the portable
LLVM SDK or `PATH`). The runtime covers heap, managed pointers, sync and
host-backed multi-thread tasks, virtual FS, env/process, HTTP/TCP host imports,
WASI preview1 services, and optional shared-memory modules.

```bat
REM Build (host env imports: Node/browser)
absolutec tests\wasm-smoke.abs --target wasm32-unknown-unknown --build-exe -o out.wasm
node tools\absolute-wasm-run.js out.wasm
node tools\absolute-wasm-run.js out.wasm -- --mode=release input.txt

REM Developer helper (build / run / ctest -R wasm)
absolute wasm build tests\wasm-smoke.abs -o out.wasm
absolute wasm run out.wasm
absolute wasm test

REM WASI (fd_write / clock / args / env) — needs Absolute built with WASI object
set ABSOLUTE_WASM_RUNTIME=wasi
absolutec tests\wasm-smoke.abs --target wasm32-unknown-unknown --build-exe -o out-wasi.wasm
node tools\absolute-wasm-wasi-run.js out-wasi.wasm

REM Browser demo (COOP/COEP for worker + SharedArrayBuffer)
node scripts\serve-wasm-demo.mjs
```

| Runtime | How |
|---------|-----|
| **host** (default) | `env.absolute_*` imports; `tools/absolute-wasm-host.js` |
| **wasi** | `ABSOLUTE_WASM_RUNTIME=wasi` or `--runtime wasi` |
| **shared** | `ABSOLUTE_WASM_RUNTIME=shared` — imported shared memory + locked heap |

Details: [`docs/wasm-target.md`](docs/wasm-target.md), demo
[`examples/wasm/`](examples/wasm/), platforms matrix
[`docs/platforms.md`](docs/platforms.md). CI runs `ctest -R wasm` on Windows and
Linux.

Generate a native object without linking it:

```bash
absolutec build Demo.absproj --emit-object -o Demo.obj
```

Build a native shared library directly:

```bash
absolutec build Math.absproj --build-library -o Math.dll
```

On Linux use a `.so` output and on macOS use `.dylib`. The library command
also produces the platform's normal import/object artifacts alongside the
shared library, all contained by the project `build` directory when invoked
through `absolute build`.

Or let Absolute emit the object and call the C++ compiler driver to link the
project's `nativeLibraries`. On a Visual Studio build this uses the configured
`cl.exe`, including the MSVC runtime libraries:

```bash
absolutec build Demo.absproj --build-exe -o Demo.exe
Demo.exe
```

The generated `.obj` is retained next to the executable so it can be inspected
or linked manually. Current FFI types map primitive scalars directly; `string`
is passed as a C `char*`. Ownership stays with the caller. Native exceptions
must not enter `extern "C"`, and Absolute errors must be handled before leaving
an `export "C"` function.

External shared libraries that register functionality at runtime can be loaded
without adding them to the project link list:

```absolute
if (!load("./extensions/physics.dll")) {
    println("physics library was not loaded");
}
```

`load(string)` returns `true` when the `.dll`/`.so` was loaded or had already
been loaded through the same path, and `false` on failure. Loaded libraries stay
resident until process exit. On POSIX their symbols are made globally visible
with `RTLD_NOW | RTLD_GLOBAL`. Calls to ordinary `extern "C"` imports still need
`nativeLibraries` when their symbols must be resolved while the executable is
linked or started.

Use `isLoaded(path)` to query the process-local loader cache. After a failed
`load`, `loadError()` returns the thread-local platform diagnostic; a successful
load clears it:

```absolute
if (!load("./extensions/audio.so")) {
    println(loadError());
}
```

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

The native Release backend caches the pointee of a proven local managed owner
after the allocation's initial generation check. Dereferences of that unchanged
owner use the cached address directly, so hot loops contain no slot bounds or
generation/lifetime checks. This elimination is limited to analyzer-proven owners:
borrowed parameters, subscribers, and weak pointers continue through the
generation-checked fast path. `delete`, ownership transfer, and reassignment
invalidate or replace the cache, while dereference after `delete`/`move` is
rejected before code generation.

Use `weak T*` for an explicit non-owning handle that may be stored in fields or
returned from an API:

```absolute
class Node {
    public weak Node* parent;
}

Node* owner = new Node();
weak Node* observer = owner;
delete owner;
assert(!observer);
```

Weak references reuse the same generation-checked managed handle, do not keep
the object alive, and never participate in generated cleanup. Strong managed
pointers convert to weak; conversion back, direct `new` into weak, `move`, and
`delete` are rejected. See
[docs/weak-managed-references.md](docs/weak-managed-references.md).

For cyclic domain models, strong fields are containment edges and form a
unique-ownership forest; parent, peer, and cross-links must be `weak`. Deleting a
root recursively destroys strong children, while weak cycles own nothing and
expire through the existing handle generation checks. Explicit strong
back-edges are rejected at compile time. See
[docs/managed-object-graphs.md](docs/managed-object-graphs.md).

Ownership of a strong managed pointer can be transferred explicitly:

```absolute
Node* a = new Node();
Node* b = move(a);
```

`b` becomes the owner; `a` is zeroed at runtime and treated as compile-time
moved-from until assigned a fresh owner. Existing subscribers follow the new
owner for lifetime analysis. Moving a subscriber/weak/const source, discarding
the result, or moving into an ordinary borrowed pointer parameter is rejected.
See [docs/managed-pointer-move.md](docs/managed-pointer-move.md).

Managed pointer and array fields are owning resource slots. They accept a fresh
owner (`new`, `copy(...)`, or an owning function result), destroy their previous
value on reassignment, and are released automatically with the containing class
or struct. Class and interface deletion dispatches through a destructor entry in
vtable slot zero, so derived fields are cleaned even through a base pointer. See
[docs/resource-ownership.md](docs/resource-ownership.md) for the full rules.

Deep copying is explicit rather than based on C++ copy constructors. Arrays and
slices use `copy(values)`; user types expose a public zero-argument
`clone() const`, and `copy(value)` invokes it. Class/interface clone methods
return a fresh managed pointer and participate in normal virtual dispatch.
Resource-free struct assignment remains a cheap field-wise value copy. See
[docs/copy-clone.md](docs/copy-clone.md).

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

Use `--sanitize=address` with `--emit-object` or `--build-exe` to instrument unsafe
raw memory accesses with AddressSanitizer. The native test suite runs real
heap-use-after-free and double-free executables through ASan; managed allocation
leaks are additionally detected by the generation-slot runtime at process exit.

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

## Structs

`struct` declares an inline value type. Resource-free structs use ordinary value
semantics: assigning, passing, or returning them copies the complete value.
Structs containing managed pointers, owning array descriptors, or another
resource-owning aggregate are move-only; implicit assignment, by-value arguments,
and by-value returns are rejected unless ownership is transferred with `move(...)`.
Instance constructors and methods use the same syntax as class members:

```absolute
struct Point {
    float x;
    float y;

    Point(float initialX, float initialY) {
        x = initialX;
        y = initialY;
    }

    float lengthSquared() {
        return x * x + y * y;
    }
}

Point makePoint() {
    Point result;
    result.x = 3.0;
    result.y = 4.0;
    return result;
}
```

Struct pointers use the normal ownership model. `new raw Point(...)` returns a
native address that must be deleted explicitly; `new Point(...)` returns a
managed owner that is automatically released at scope exit:

```absolute
raw Point* unsafe = new raw Point(3.0, 4.0);
Point* tracked = new Point(6.0, 8.0);
println(unsafe.lengthSquared());
delete unsafe;
```

A struct cannot contain itself by value because that would have infinite size;
use `raw T*` or `T*` for recursive links. Structs do not have inheritance or
virtual dispatch and therefore carry no class vtable field.

The generated Absolute ABI passes structs up to 16 bytes directly. Larger
structs use an isolated caller-side argument copy and a hidden result pointer,
so mutating a by-value parameter never aliases the source. This applies equally
to functions, methods, properties, and constructors. `extern "C"` declarations
continue to use the platform C ABI. The exact lowering and copy-elision rules
are documented in [docs/value-type-abi.md](docs/value-type-abi.md).

Large resource-free structs may instead use parameter-only value references:

```absolute
int64 inspect(const LargeValue& value) {
    return value.first;
}

void normalize(LargeValue& value) {
    value.first = 0;
}
```

`const T&` borrows read-only storage and accepts lvalues or a temporary valid
through the call. `T&` requires a mutable lvalue and mutates caller storage.
The spellings `const ref T` and `ref T` are source aliases normalized to the
canonical ampersand form.
Both lower to a non-null, non-capturing LLVM pointer without creating a managed
or raw pointer value. They cannot cross async/C-ABI/closure boundaries or borrow
resource-owning aggregates; overlapping mutable arguments are rejected. See
[docs/value-references.md](docs/value-references.md).

## Static members

Classes, structs, and interfaces support static fields and methods. Access them
through the type, for example `Counter.value` and `Counter.advance()`. Static
fields are LLVM globals and do not increase object size; static methods have no
hidden `this` parameter. Derived types see the same inherited static storage.

Interface static members must be public. Static interface methods require a
body, are called through the interface name, and do not enter the instance
vtable or create an implementation requirement for classes. Static abstract
interface methods are reserved for the future generic-constraint system.

Static field initializers are currently limited to constant primitive, string,
enum, or raw-pointer values. Managed/array/aggregate static storage, runtime
initializers, and static members of generic types are intentionally rejected
until module initialization and ownership are defined. See
[docs/static-members.md](docs/static-members.md) for the exact rules.

## Base constructors

A derived constructor may pass values to its direct base constructor with
`base(...)`. The call must be the first statement in the constructor body:

```absolute
class NamedNode : Node {
    public NamedNode(string name, int32 value) {
        base(value);
        label = name;
    }
}
```

When the call is omitted, the compiler inserts `base()` automatically. Classes
without a declared constructor also receive an implicit zero-argument
constructor when their inheritance chain needs one. A base constructor with
required parameters must always be called explicitly. See
[docs/base-constructors.md](docs/base-constructors.md) for the complete rules.

## Access control and runtime type checks

Class and struct members may be `public`, `protected`, or `private`. Public is
the compatibility default when no access modifier is present. Private members
are visible only inside their declaring type; protected members are visible in
the declaring class and derived classes. The same checks apply to fields,
methods, static members, constructors, base-constructor calls, and interface
implementations. Interface methods and their implementations must be public.

Class and interface pointers support runtime type tests and safe casts:

```absolute
Node* node = new AddNode(2);
assert(node is AddNode);

AddNode* add = node as AddNode;
OtherNode* missing = node as OtherNode;
assert(add != null);
assert(missing == null);
```

`is` returns false for null or an incompatible dynamic type. `as` preserves raw
or managed ownership mode and returns null on failure. Managed results are
subscribers to the original owner. Numeric `as` conversions remain available.
See [docs/access-and-runtime-types.md](docs/access-and-runtime-types.md) for the
full rules.

## Interfaces

Interfaces declare method contracts without storage. A contract may provide a
default method body. An interface may inherit other interfaces, and a class may
inherit one base class plus any number of interfaces:

```absolute
interface IEvaluable {
    int64 evaluate(int64 input);

    int64 doubled(int64 input) {
        return input * 2;
    }
}

interface INamed {
    int64 id();
}

class AddNode : BaseNode, IEvaluable, INamed {
    int64 evaluate(int64 input) {
        return input + 2;
    }

    int64 id() {
        return 42;
    }
}
```

Interfaces are reference-only types. Both raw and managed pointers preserve
dynamic dispatch without an extra wrapper allocation:

```absolute
raw IEvaluable* unsafe = new raw AddNode();
IEvaluable* tracked = new AddNode();

assert(unsafe.evaluate(40) == 42);
assert(tracked.evaluate(40) == 42);
delete unsafe;
```

The analyzer verifies every required overload and its return type. A class method
has precedence over an inherited default. The same default inherited through a
diamond is shared; two different defaults with the same signature require the
class to declare a resolving implementation. Instantiating an interface,
declaring it as an inline value, omitting an abstract method/property accessor,
or implementing an incompatible signature is rejected. Interface instance
fields are rejected; interfaces may instead own public static fields and
static methods with bodies.

## Properties

Classes and structs support explicit and automatically stored properties. An
interface property contributes getter/setter contracts to the same virtual
dispatch table as methods:

```absolute
interface IValue {
    int64 Value {
        get;
        set;
    }

    int64 Answer {
        get { return 42; }
    }
}

class Box : IValue {
    public int64 Value {
        get;
        set;
    }
}
```

An accessor with `;` receives hidden zero-initialized storage in a class or
struct, but remains an abstract contract in an interface. Accessors with bodies
are ordinary checked code and setters receive the assigned value as `value`.
Properties may be read-only or write-only, accessor access may be narrowed (for
example `private set`), and `virtual`/`override`, generic types, raw/managed
receivers, compound assignment, and increment operators use normal dispatch.
Properties are not addressable, so `&object.Value` is rejected.

## Indexers

Classes, structs, and interfaces can expose indexed access with `this[...]`:

```absolute
interface ITable {
    int64 this[int32 index] {
        get;
        set;
    }
}

class Table : ITable {
    private int64 storage;

    public int64 this[int32 index] {
        get { return storage + index; }
        private set { storage = value - index; }
    }
}
```

Indexer parameters participate in overload resolution, so a type may provide,
for example, both `this[int32]` and `this[string]`. Reads, writes, compound
assignments, and increment operators call the selected accessors. Indexers use
normal class/struct, `virtual`/`override`, generic, and raw/managed interface
dispatch. Interface accessors ending in `;` are contracts; concrete class and
struct indexers require explicit bodies. Like properties, indexers are not
addressable.

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

Scheduling defaults can be attached to an async function, then selectively
overridden for one spawn site:

```absolute
@task(core = 2, priority = 1, role = "worker")
async int32 calculate(int32 value) {
    return value * 2;
}

async int32 main() {
    @spawn(priority = 3, role = "urgent")
    task<int32> result = spawn calculate(21);
    return await result;
}
```

`core = -1` means no affinity, priority uses the portable `-3..3` range, and
role creates a named scheduling lane whose label is visible through
`std/task.abs`. See
[`docs/task-scheduling.md`](docs/task-scheduling.md) for inheritance, runtime,
and validation details.

Classes and structs support static async methods and `const` instance async
methods. Instance spawns capture a stable named `const` receiver; managed
subscribers, raw pointers, temporaries, mutable bindings, and resource-owning
receiver types are rejected. Virtual class methods keep normal vtable dispatch
inside the task thunk. See [`docs/async-methods.md`](docs/async-methods.md).

`await` is only valid inside an `async` function. Every local task must be
awaited on every control-flow path before its scope is left, including through
`return`, `break`, and `continue`; a task cannot be copied, reassigned, or
awaited twice. The analyzer exposes `TaskState` in `ExpressionInfo` and emits
stable `E_TASK_*` diagnostics for IDE integrations.

The LLVM backend packs scalar/enum arguments into an owned task context and emits
a private thunk for each spawn site. `Absolute-Runtime` executes these thunks
on a shared native thread pool, and `await` suspends the calling OS thread until
the result is ready. Task arguments and results support only lifetime-independent
scalar and enum values; cancellation, channels, async I/O,
and broader compile-time data-race checking are planned separately.

## Arrays

Local arrays support fixed or runtime dimensions, rectangular literals,
inferred literal sizes, multidimensional row-major indexing, and element
assignment:

```absolute
int32 values[4] = {10, 20, 30, 40};
values[2] += 12;

int32 matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
matrix[1][0] = values[2];

// Nested groups, but one contiguous float[] with length 6.
float[] vertices = {
    {0.0, 0.5, 0.0},
    {0.5, 0.0, 1.0}
};

int32 inferred[] = {7, 8, 9};
int32 length = 16;
int32 buffer[length];
```

Array storage is zero-initialized. Dimensions must be positive, every access
must provide the complete index list, and generated code checks each index at
runtime. An invalid size or out-of-bounds index prints a diagnostic and exits
with a nonzero status. Literal shapes are checked statically for rank,
rectangularity, and exact fixed dimensions.

For a one-dimensional declaration (`T[]` or `T values[N]`), nested braces are
readable record groups that flatten into one contiguous row-major buffer. The
groups must be rectangular. A `T[][]` declaration still creates a true rank-2
descriptor with multidimensional indexing.

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
    return copy(values[1:]);
}
```

One-dimensional slices are half-open: `values[from:to]` includes `from` and
excludes `to`. Either bound can be omitted, and `values[]` creates a view of the
complete array. Slices do not copy their elements, so writes through a slice
modify its source and the source must remain alive while the slice is used.
Slice bounds are checked at runtime. `foreach` currently iterates
one-dimensional arrays by value.

Use `copy(arrayOrSlice)` when an independent owning buffer is required. The
result has the same array type and dimensions, but later writes no longer alias
the source. Elements are copied by value; pointer elements remain references to
the same pointees rather than recursively cloning an object graph. The backend
lowers copies of contiguous storage to `memcpy`. A local variable initialized
from `copy(...)` owns that buffer and releases it automatically at scope exit:

```absolute
int32 values[4] = {10, 20, 30, 40};
int32[] view = values[1:3];   // zero-copy
int32[] owned = copy(view);   // separate buffer
view[0] = 99;                 // changes values[1]
owned[0] = 7;                 // does not change values
```

Global arrays use the same sized or inferred literal declarations as local
storage:

```absolute
int32 primes[4] = {2, 3, 5, 7};
int32[] flags = {1, 0, 1};
```

Their dimensions and initializer values must be compile-time primitive
constants. Returning an array descriptor no longer performs an implicit copy.
A global view may therefore be returned without allocation. A local array or a
slice borrowed from a parameter must use `copy(...)` before it can escape a
function; this prevents a dangling reference until explicit slice lifetime
annotations are implemented. Returning an owning array transfers its allocation
to the caller. Slicing never changes ownership: the descriptor keeps the base
allocation separately from the possibly interior data pointer, so a returned
`copy(values)[1:3]` is released safely. Array parameters are non-owning borrows;
temporary owning results passed to a call or discarded as a statement are
released by the caller after use. See `docs/array-ownership.md` for the detailed
rules and current limitations.

The backend also supports primitive values, functions, local variables, calls,
casts, arithmetic/comparison operators, assignments, `return`, `if`, `for`,
`while`, `do-while`, `foreach`, `break`, and `continue`. User-defined classes
support fields, local value instances, constructors, raw or managed allocation,
single inheritance, interfaces, instance and static methods, static fields, and
`virtual`/`override` dispatch. Multiple class inheritance and automatic
destructor calls are not emitted. Base constructors run before the derived
constructor body, with either explicit `base(...)` arguments or an automatic
zero-argument call. Raw object graphs that own child nodes must release them
explicitly before `delete`.

Both pointer modes preserve class methods and virtual dispatch:

```absolute
raw Node* unsafe = new raw AddNode(2); // explicit delete is required
Node* tracked = new AddNode(2);        // released automatically at scope exit

assert(unsafe.evaluate(40) == 42);
assert(tracked.evaluate(40) == 42);
delete unsafe;
```

## Standard library

The in-tree standard library lives under `std/` and is packaged as
**`absolute.std`**. Namespace layout, import rules, SemVer, and stability tiers
are defined in [`docs/standard-library.md`](docs/standard-library.md). Package
identity: [`std/abspackage.json`](std/abspackage.json).

## Filesystem and TCP

`std/fs.abs` provides UTF-8 path helpers, whole-file text operations, directory
creation, copy/rename/remove, and the resource-owning `std.fs.File` stream:

```absolute
import "std/fs.abs";

std.fs.writeText("hello.txt", "hello");
std.fs.appendText("hello.txt", " world");
println(std.fs.readText("hello.txt"));

std.fs.File* file = std.fs.open("hello.txt", "rb");
println(file.readLine());
delete file;
```

`std/net.abs` provides blocking cross-platform TCP sockets. Listener port `0`
requests an ephemeral OS-assigned port:

```absolute
import "std/net.abs";

std.net.TcpListener* listener = std.net.listenLocal(0);
std.net.TcpSocket* client = std.net.connect("127.0.0.1", listener.port());
std.net.TcpSocket* server = listener.accept();
client.send("ping");
println(server.receive(4096));
delete server;
delete client;
delete listener;
```

`File` and TCP objects own opaque native handles and close them from `destroy()`.
Text returned by a stream/socket read is a borrowed UTF-8 view valid until the
next read on that same object; top-level filesystem result strings are valid
until the next top-level result-producing filesystem call on the current thread.
UDP, HTTP/URI, and async I/O are intentionally separate follow-up layers.

For a Release build, replace `Debug` with `Release`.

## Test

```bash
ctest --test-dir build --output-on-failure
```
