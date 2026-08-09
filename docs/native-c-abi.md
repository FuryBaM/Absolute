# Native C ABI

Status: normative for language-level interop. Absolute officially supports only
the **C ABI** at `extern "C"` / `export "C"` boundaries. Direct C++ ABI (name
mangling, overloads, classes, C++ exceptions) is **not** supported.

## Calling convention and symbols

- LLVM calling convention is platform C (`ccc`).
- The native symbol is the **unmangled** function name (last segment after
  namespaces). `namespace N { export "C" int32 foo() }` still exports `foo`.
- On Windows, `export "C"` is marked `dllexport`. On other targets the symbol
  has default external visibility.
- Overloads, generics, default parameters, `async`, extension methods, and
  class/struct members are rejected on C ABI callables.
- C ABI symbols cannot be stored in Absolute `func` values.

## Allowed types

| Absolute type | C / LLVM | Notes |
|---------------|----------|--------|
| `int8` / `uint8` / `char` | `int8_t` / `uint8_t` / `char` → `i8` | |
| `int16` / `uint16` | fixed 16-bit → `i16` | |
| `int32` / `uint32` | fixed 32-bit → `i32` | Prefer these over C `int`/`long` in headers |
| `int64` / `uint64` | fixed 64-bit → `i64` | |
| `float` / `double` | IEEE → `float` / `double` | |
| `bool` | C `_Bool` → **`i8`** at the C boundary | Internal Absolute bool remains `i1` |
| `void` | `void` | Return only |
| `string` | `char*` / `const char*` → `ptr` | Caller owns the buffer; no automatic free |
| `raw T*` | pointer → `ptr` | Including `raw void*` |
| enum | fixed `i32` | C enum size is platform-dependent; expose `int32` in C headers |

## Rejected types

| Absolute type | Reason | Diagnostic |
|---------------|--------|------------|
| Managed `T*` / `weak T*` | Generation handle is not a C pointer | `E_C_ABI_MANAGED` |
| Absolute `T[]` | Array descriptor is not a C array | `E_C_ABI_ARRAY` |
| `T&` / `const T&` | Absolute value-reference ABI | `E_VALUE_REF_C_ABI` |
| `func<...>` | Absolute closure convention | `E_C_ABI_FUNC_TYPE` |
| `task<...>` | Task isolate ABI | `E_C_ABI_TASK` |
| `struct` / `class` / `interface` by value | Layout is Absolute, not `repr(C)` | `E_C_ABI_AGGREGATE` |
| `auto` / `dynamic` / `void` parameters | Not C-compatible | `E_C_ABI_TYPE` |

## Arrays and buffers

There is no automatic array interop. The portable pattern is:

```absolute
extern "C" int32 process_bytes(raw uint8* data, int32 length);
```

Safe Absolute arrays must be lowered explicitly (for example copy into a
contiguous buffer, or pass `raw` only from unsafe code). See also
`docs/array-ownership.md`: unsafe/FFI export is outside the safe collection model.

## Strings

`string` at the C boundary is a raw C string pointer:

- Absolute → C: pass the internal UTF-8 `char*` (caller retains ownership).
- C → Absolute: treat returned `char*` as a borrowed or documented native
  lifetime; do not assume Absolute will free native heap unless a companion
  free function is part of the API.

Prefer length-aware APIs (`raw int8*`, `int32 length`) when embedded nulls or
non-terminated buffers are possible.

## Bridging raw and managed ownership

The ownership bridge follows Absolute's deterministic unique-owner model:

- `borrowRaw(rawPointer)` returns a non-owning `raw T*` view and never changes
  the native pointer's lifetime;
- `adoptRaw(rawPointer)` consumes a `raw T*` and creates the unique managed
  `T*` owner;
- `adoptRaw(rawPointer, deleter)` additionally records a C-compatible deleter,
  which runs exactly once when the managed allocation is destroyed;
- `retainRaw`, `share`, and `shared T*` are deliberately rejected because the
  runtime does not implement reference-counted shared ownership.

After a successful adoption the source raw binding is moved-from and must not
be freed or reused as an owner. A null raw pointer adopts as a null managed
handle. A custom deleter must be safe to call outside the runtime's slot-table
lock.

## Aggregates and interfaces

Absolute `struct` layout (including the 16-byte internal value ABI described in
`docs/value-type-abi.md`) is **not** guaranteed to match any platform C
struct ABI. Therefore by-value aggregates are rejected at the language C
boundary.

Patterns:

```absolute
// Opaque handle owned by native code
extern "C" raw void* window_create(int32 width, int32 height);
extern "C" void window_destroy(raw void* window);

// C-compatible buffer filled by Absolute or native code
extern "C" void fill_point(raw int32* xy); // writes x,y
```

Interfaces and class objects have no C representation; expose plain functions
and opaque `raw void*` (or typed `raw T*` to incomplete native types).

## Callbacks (`cfunc`)

C-compatible function pointers use the `cfunc` type constructor. Unlike Absolute
`func` values (closures with optional captures), a `cfunc` is a raw code pointer
with a fixed C ABI signature:

```absolute
// cfunc<Return, Param0, Param1, ...>
using Handler = cfunc<int32, int32>;

export "C" int32 absolute_double(int32 value) {
    return value * 2;
}

Handler handler = absolute_double; // export "C" / extern "C" only
assert(handler(21) == 42);
handler = null; // nullable like a raw pointer
```

Rules:

- Every parameter and the return type must be C-ABI-safe (this document).
- Only `extern "C"` / `export "C"` functions can be stored in a `cfunc`.
- Absolute functions and capturing lambdas stay as `func<...>` and cannot convert.
- Calling a `cfunc` uses the platform C calling convention; Absolute exception
  checks are not inserted after the call (same as pure `extern "C"`).
- `cfunc` may appear as a parameter/return of another C ABI function so native
  code can receive Absolute `export "C"` callbacks as function pointers.

## Safe native handles

Opaque native resources should not leak as bare `raw void*` without ownership.
The supported pattern is a resource-owning `struct` with a user `destroy()`
method (see also `docs/resource-ownership.md`):

```absolute
extern "C" raw void* window_create(int32 width, int32 height);
extern "C" void window_destroy(raw void* window);

struct Window {
    raw void* ptr;

    void destroy() {
        if (ptr != null) {
            window_destroy(ptr);
            ptr = null;
        }
    }
}
```

`destroy()` runs when the struct leaves scope, is overwritten, or is cleaned by
`move` transfer rules. Prefer returning handles with `move(...)` when the type
owns resources.

## Exceptions and errors

- Native C++ exceptions must not unwind through `extern "C"` into Absolute.
- Absolute exceptions must not escape an `export "C"` body; handle them before
  return (see `docs/error-model.md`).
- Calls to pure `extern "C"` do not run Absolute exception checks after return.

## Dynamic load vs link-time natives

| Mechanism | Use when |
|-----------|----------|
| `.absproj` `nativeLibraries` / `nativeSearchPaths` | Symbols resolved at link or process start |
| `load(path)` / `isLoaded` / `loadError` | Optional plugins; `false` on failure; cache by path |

`load` uses UTF-8 paths on Windows (`LoadLibraryW`) and
`dlopen(..., RTLD_NOW | RTLD_GLOBAL)` on POSIX. Libraries stay resident until
process exit. Ordinary `extern "C"` imports still need a link-time library if
the dynamic loader does not export the symbol globally.

## C++ libraries

Wrap C++ APIs in a small C shim:

```cpp
extern "C" int native_add(int left, int right) {
    return left + right;
}
```

Do not declare Absolute `extern "C++"`. MSVC and Itanium C++ ABIs differ; Absolute
will not bridge them at the language level.

## Header bindgen

Generate Absolute `extern "C"` stubs from a C header with the optional
clang-based tool:

```bat
node tools/absolute-bindgen.js path\to\api.h -o api.abs
absolute bindgen path\to\api.h -o api.abs -I third_party\include
```

The tool uses `clang -Xclang -ast-dump=json` (portable LLVM under
`.absolute/toolchains` or `clang` on `PATH`). It maps:

| C | Absolute |
|---|----------|
| fixed-width integers / `float` / `double` / `bool` | matching Absolute scalar |
| `void*` / opaque typedef pointers | `raw void*` or `using Handle = raw void*` |
| `const char*` | `string` (caller-owned) |
| `T*` for scalars | `raw T*` |
| function pointers | `cfunc<...>` when argument types map |
| variadic / incomplete aggregates | skipped with a comment |

Re-check `long` / `size_t` mappings against your target ABI. The generator does
not parse C++ headers or invent mangled names.

## Versioning

This document describes Absolute language revision for C interop. Changing
allowed types or `bool`/`string` lowering is an ABI break for any mixed
Absolute/native object files and requires a coordinated rebuild.
