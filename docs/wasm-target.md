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
- Pure `export "C"` modules produce a loadable `.wasm`
  (`tests/wasm-export-only.abs` + Node runner).
- Programs that lower to `puts` / `printf` / `abort` / `snprintf` (for example
  `println` and `assert`) link against the optional **wasm console shim**
  `Absolute-Runtime/wasm/absolute_wasm_shim.c` (built with
  `clang --target=wasm32-unknown-unknown` when available).
- Browser demo: `examples/wasm/` (static HTML + `loader.js`).

## What is rejected or unsupported

| Mode | Result |
|------|--------|
| `--target wasm32-... --sanitize=address` | Error |
| `--build-exe --target` for non-wasm triples | Error |
| managed heap, tasks, `load`, FS, sockets on wasm | undefined symbols / no shim |
| Full WASI sysroot / Absolute-Runtime port | Not shipped |

## Runtime and linking

| Layer | Status |
|-------|--------|
| Host `Absolute-Runtime` | Native only |
| `absolute_wasm_shim.o` | Minimal no-op I/O + `abort` trap + libc mem helpers |
| `ABSOLUTE_WASM_LIBS` | Extra space/`;`-separated objects to link |

Supported profiles:

1. **Export-only** — `export "C"` scalars, no host runtime calls.
2. **Console/assert** — `println` / `assert` / pure scalars with the wasm shim.
3. Load in Node / browser with **empty imports** (`WebAssembly.instantiate(bytes, {})`).

See the host matrix in [`platforms.md`](platforms.md) and
[`examples/wasm/README.md`](../examples/wasm/README.md).

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

- `tests/wasm-export-only.abs` → `absolute.run-wasm-export`
- `tests/wasm-smoke.abs` → `absolute.run-wasm-smoke` (shim + `main` + `wasm_add`)
- IR/object checks: `emit-wasm-smoke-ir`, `check-wasm-smoke-ir`, `emit-wasm-smoke-object`

## Next steps (not done)

1. Real managed-heap / task / FS Absolute-Runtime for wasm32 or WASI.
2. Optional wasi-sdk sysroot for full libc.
3. Dedicated wasmtime CI job (Node already covers smoke when tools exist).

## Acceptance criteria progress

- [x] Explicit CLI/target selection (`--target`)
- [x] Documented wasm emit/link limits (this file)
- [x] Engine smoke: Node for export-only and console/assert modules
- [x] Browser loader example (`examples/wasm`)
- [x] Clear errors for ASan / non-wasm cross `--build-exe`
- [x] Host backends remain default when `--target` is omitted
