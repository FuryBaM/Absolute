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
- Console helpers and managed pointers link against
  `Absolute-Runtime/wasm/absolute_wasm_runtime.c` (built with
  `clang --target=wasm32-unknown-unknown` when available).
- Browser demo: `examples/wasm/` (static HTML + `loader.js`).

## What is rejected or unsupported

| Mode | Result |
|------|--------|
| `--target wasm32-... --sanitize=address` | Error |
| `--build-exe --target` for non-wasm triples | Error |
| `load(.dll)` / desktop plugins on wasm | not available |
| Full WASI sysroot / wasi-sdk libc | Not shipped |
| Multi-threaded Absolute tasks on wasm | sync only (no worker pool yet) |

## Runtime and linking

| Layer | Status |
|-------|--------|
| Host `Absolute-Runtime` | Native only |
| `absolute_wasm_runtime.o` | Heap, managed, errors, sync tasks, virtual FS, env/process, host TCP/HTTP |
| `ABSOLUTE_WASM_LIBS` | Extra space/`;`-separated objects to link |

Supported profiles:

1. **Export-only** — `export "C"` scalars.
2. **Console/assert** — `println` / `assert` via `env.absolute_log`.
3. **Managed objects** — `new` / `delete` (see `tests/wasm-managed.abs`).
4. **Sync tasks** — `spawn`/`await` run immediately (see `tests/wasm-task.abs`).
5. **Virtual FS** — in-memory `absolute_fs_*` (see `tests/wasm-fs.abs`).
6. **HTTP** — host `absolute_http_get` with mocks or prefetch (see `tests/wasm-http.abs`).
7. **TCP** — host `absolute_tcp_*` mocks or real OS sockets under Node (below).

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
- `tests/wasm-smoke.abs` → `absolute.run-wasm-smoke`
- `tests/wasm-managed.abs` → `absolute.run-wasm-managed` (`new`/`delete` Box)
- `tests/wasm-task.abs` → `absolute.run-wasm-task` (sync spawn/await)
- `tests/wasm-fs.abs` → `absolute.run-wasm-fs` (virtual FS round-trip)
- `tests/wasm-http.abs` → `absolute.run-wasm-http` (host HTTP mock)
- `tests/wasm-net.abs` → `absolute.run-wasm-net` (host TCP echo mock)
- `tests/wasm-net-real.abs` → `absolute.run-wasm-net-real` (real localhost TCP via worker bridge)
- IR/object checks + optional WASI/wasmtime smoke

## Host import

| Import | Module | Purpose |
|--------|--------|---------|
| `absolute_log(ptr, len)` | `env` | UTF-8 console from `puts`/`printf` |
| `absolute_http_get(url, out, cap)` | `env` | Host HTTP GET (or mocks) into linear memory |
| `absolute_tcp_*` | `env` | Host TCP: mocks and/or real OS sockets (Node) |

Node helpers:

- `tools/absolute-wasm-host.js` — `instantiateAbsoluteWasm`
- `tools/absolute-wasm-tcp-worker.js` — background TCP for blocking imports
- `tools/absolute-wasm-run.js` — CLI runner for `main` / exports

### Real TCP (Node)

Wasm imports are synchronous, so real sockets run on a `worker_threads` worker
and the main thread blocks with `SharedArrayBuffer` + `Atomics.wait` until the
worker finishes each op (`connect` / `send` / `receive` / …).

| Option | Behavior |
|--------|----------|
| `tcpMocks: { "host:port": { mode: "echo" } }` | Deterministic unit-test table (no OS sockets) |
| `forceTcpMocks: true` | Always use mocks |
| `allowNetwork: true` (default when no mocks) | Real OS TCP via the worker bridge |
| `host.shutdown()` | Close sockets and terminate the TCP worker |

Limits:

- **Same-thread peer servers deadlock.** Because `Atomics.wait` freezes the
  main Node event loop, an echo/listen server on that same thread will never
  process accept/data while wasm is blocked in a TCP import. Put the peer in
  another process/worker (as `run-wasm-net-real.cmake` does), or talk to an
  external host.
- Browser embeds still need their own `absolute_tcp_*` implementation (or mocks);
  the worker bridge is Node-oriented.
- UDP remains unavailable on wasm.

WASI console build (`absolute_wasm_runtime_wasi.o`) uses
`wasi_snapshot_preview1.fd_write` instead of `env.absolute_log` so
`wasmtime run --invoke main module.wasm` works when the WASI object is linked.

## Next steps (not done)

1. Multi-threaded Absolute tasks / SharedArrayBuffer worker pool.
2. Optional wasi-sdk sysroot and richer WASI networking.
3. Browser-side TCP/WebSocket bridge equivalent to the Node worker.

## Acceptance criteria progress

- [x] Explicit CLI/target selection (`--target`)
- [x] Documented wasm emit/link limits (this file)
- [x] Engine smoke: Node for export/smoke/managed/task modules
- [x] Host console via `env.absolute_log`
- [x] Browser loader example (`examples/wasm`)
- [x] Clear errors for ASan / non-wasm cross `--build-exe`
- [x] Host backends remain default when `--target` is omitted

