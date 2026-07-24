# WebAssembly target

Status: **experimental**. Absolute can emit WebAssembly IR/objects and, when
`wasm-ld` is available, link **export-only** modules into `.wasm` for engines
such as Node's `WebAssembly` API.

## What works today

```bat
absolutec program.abs --target wasm32-unknown-unknown --emit-llvm -o program.ll
absolutec program.abs --target wasm32-unknown-unknown --emit-object -o program.o
absolutec tests\wasm-export-only.abs --target wasm32-unknown-unknown --build-exe -o out.wasm
```

- Module `target triple` is set to the requested triple (e.g. `wasm32-unknown-unknown`).
- LLVM WebAssembly backend is linked and initialized alongside the host backend.
- `--build-exe` for wasm runs `wasm-ld --no-entry --export-all` (found via
  `ABSOLUTE_WASM_LD`, configure-time LLVM tools, PATH, or
  `.absolute/toolchains/llvm-*/bin/wasm-ld`).
- Pure `export "C"` modules (no `println` / managed heap / tasks) produce a
  loadable `.wasm` (see `tests/wasm-export-only.abs` + Node runner).

## What is rejected or unsupported

| Mode | Result |
|------|--------|
| `--target wasm32-... --sanitize=address` | Error |
| `--build-exe --target` for non-wasm triples | Error |
| wasm module that calls host Absolute runtime | `wasm-ld` undefined-symbol failure |
| Full WASI / browser app runtime | Not shipped |

## Runtime and linking

Host `Absolute-Runtime` is **not** wasm-compatible. Programs that use
`println`, `assert` (runtime path), managed pointers, `load`, tasks, etc. will
not link until a wasm runtime port exists.

Supported link profile today:

1. Absolute source with only `export "C"` functions and Absolute scalars.
2. `absolutec --target wasm32-unknown-unknown --build-exe -o mod.wasm`
3. Load in Node / browser / wasmtime with no imports (memory is exported).

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

- `tests/wasm-smoke.abs` — IR/object + host-runtime link rejection
- `tests/wasm-export-only.abs` + `absolute.run-wasm-export` (build + Node)
- `absolute.emit-wasm-smoke-ir` / `check-wasm-smoke-ir` / `emit-wasm-smoke-object`

## Next steps (not done)

1. Minimal Absolute-Runtime subset compiled for wasm32/WASI.
2. Optional wasi-sdk sysroot integration for `main` + libc.
3. Dedicated CI job installing wasmtime (Node runner already covers export smoke when tools exist).
4. Browser demo loader.

## Acceptance criteria progress

- [x] Explicit CLI/target selection (`--target`)
- [x] Documented wasm emit/link limits (this file)
- [x] Engine smoke: Node WebAssembly for export-only modules (`run-wasm-export`)
- [x] Clear errors for ASan / non-wasm cross `--build-exe` / host-runtime link failures
- [x] Host backends remain default when `--target` is omitted
