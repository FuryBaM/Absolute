# WebAssembly target

Status: **experimental MVP**. Absolute can select a WebAssembly LLVM triple for
IR and object emission. There is **no** host `--build-exe` path for wasm yet
(no `wasm-ld` / WASI sysroot integration, no browser runtime).

## What works today

```bat
absolutec program.abs --target wasm32-unknown-unknown --emit-llvm -o program.ll
absolutec program.abs --target wasm32-unknown-unknown --emit-object -o program.o
```

- Module `target triple` is set to the requested triple (e.g. `wasm32-unknown-unknown`).
- LLVM WebAssembly backend is linked and initialized alongside the host backend.
- Default object extension for wasm is `.o` on every host OS.
- `export "C"` symbols remain unmangled in IR/objects (useful as wasm exports after linking).

## What is rejected

| Mode | Result |
|------|--------|
| `--target wasm32-... --build-exe` | Error: use emit-llvm/object + external linker |
| `--target wasm32-... --sanitize=address` | Error |
| `--build-exe --target <any non-empty>` | Error (host linker only) |

## Runtime and linking (not automated)

Emitted IR/objects still reference Absolute runtime symbols (`absolute_*`, libc
helpers for `println`/`assert`, managed heap, tasks, etc.). Those symbols come
from the **host** `Absolute-Runtime` today and are **not** wasm/WASI ports.

To produce a runnable `.wasm` you must currently:

1. Emit object or bitcode with `--target wasm32-unknown-unknown`.
2. Provide wasm-compatible definitions for every referenced runtime symbol
   (or stick to a pure `export "C"` subset and supply a tiny WASI/stub runtime).
3. Link with `wasm-ld` (wasi-sdk / emscripten / standalone LLVM).

See the host matrix in [`platforms.md`](platforms.md).

## Type / feature guidance

Prefer for wasm-facing surfaces:

- scalars, enums, `raw T*`, `string` as C pointer, `cfunc` / `export "C"`;
- no reliance on Win32/X11, `load(.dll)`, or host ASan.

Managed pointers, Absolute exceptions, and the task runtime need a dedicated
wasm runtime design before they can be considered portable across engines.

Language C ABI rules still apply: [`native-c-abi.md`](native-c-abi.md).

## CLI

| Flag | Meaning |
|------|---------|
| `--target <triple>` | LLVM target triple; omit for host default |
| `--target=wasm32-unknown-unknown` | Same, equals form |

Common triples:

- `wasm32-unknown-unknown`
- `wasm32-unknown-wasi` (triple only; WASI sysroot not bundled)

## Tests

- `tests/wasm-smoke.abs` + `absolute.emit-wasm-smoke-ir` / `check-wasm-smoke-ir`
- `absolute.emit-wasm-smoke-object`
- `absolute.wasm-build-exe-rejected` (`WILL_FAIL`)

## Next steps (not done)

1. Optional `wasm-ld` driver behind `--build-exe` when wasi-sdk is detected.
2. Minimal Absolute-Runtime subset compiled for wasm32.
3. CI job: emit object + `wasm-ld` + `wasmtime` smoke.
4. Browser demo loader.

## Acceptance criteria progress

- [x] Explicit CLI/target selection (`--target`)
- [x] Documented wasm emit/link limits (this file)
- [ ] CI job: wasm artifact + engine run
- [x] Clear error for unsupported modes (`--build-exe`, ASan)
- [x] Host backends remain default when `--target` is omitted
