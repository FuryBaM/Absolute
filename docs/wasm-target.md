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
| Shared-memory pthread-style wasm threads | not yet (worker pool uses isolated instances) |

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
4. **Tasks** — sync `spawn`/`await` by default (`tests/wasm-task.abs`); optional
   Node worker pool (`taskWorkers`, `tests/wasm-task-mt.abs`).
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
- `tests/wasm-task-mt.abs` → `absolute.run-wasm-task-mt` (Node task worker pool)
- `tests/wasm-fs.abs` → `absolute.run-wasm-fs` (virtual FS round-trip)
- `tests/wasm-http.abs` → `absolute.run-wasm-http` (host HTTP mock)
- `tests/wasm-net.abs` → `absolute.run-wasm-net` (host TCP echo mock)
- `tests/wasm-net-real.abs` → `absolute.run-wasm-net-real` (real localhost TCP via worker bridge)
- `tests/wasm-smoke.abs` (WASI link) → `absolute.build-wasm-wasi-smoke` (Node WASI / wasmtime)
- `tests/wasm-wasi-services.abs` → `absolute.run-wasm-wasi-services` (clock/random/args/env)
- `tests/wasm-http.abs` via browser host → `absolute.run-wasm-browser-host`
- browser session stack → `absolute.run-wasm-browser-session` (COOP headers + Atomics protocol)
- shared-memory smoke → `absolute.run-wasm-shared-memory`
- shared-instance tasks → `absolute.run-wasm-shared-tasks` (`wasm-task-mt` + shared pool)
- IR/object checks

## Host import

| Import | Module | Purpose |
|--------|--------|---------|
| `absolute_log(ptr, len)` | `env` | UTF-8 console from `puts`/`printf` |
| `absolute_http_get(url, out, cap)` | `env` | Host HTTP GET (or mocks) into linear memory |
| `absolute_tcp_*` | `env` | Host TCP: mocks and/or real OS sockets (Node) |
| `absolute_task_pool_size` | `env` | `0` = sync tasks; `>0` = host worker pool |
| `absolute_task_enqueue` / `absolute_task_await_job` | `env` | Offload spawn/await to workers |

Node helpers:

- `tools/absolute-wasm-host.js` — `instantiateAbsoluteWasm`
- `tools/absolute-wasm-tcp-worker.js` — background TCP for blocking imports
- `tools/absolute-wasm-task-worker.js` — task worker pool (isolated instances)
- `tools/absolute-wasm-run.js` — CLI runner for host-import modules
- `tools/absolute-wasm-wasi-run.js` — CLI runner for WASI preview1 modules (Node)
- `tools/absolute-wasm-browser-host.js` / `.mjs` — browser-safe mocks (no Atomics.wait)

### Task worker pool (Node)

By default `spawn` runs the entry immediately on the calling thread (same as a
single-threaded scheduler). With `instantiateAbsoluteWasm(bytes, { taskWorkers: N })`
and `N > 0`:

1. Runtime copies the task context (i64 slots) into a `SharedArrayBuffer` job queue.
2. One of `N` OS workers claims the job, runs the entry on its **own** module
   instance (`__indirect_function_table` + `malloc`), and writes the result slot.
3. `await` blocks via `Atomics.wait` until the slot is done, then copies **only**
   the 8-byte result slot back into the main context.

| Option | Behavior |
|--------|----------|
| `taskWorkers: 0` / omitted | Sync tasks (no extra workers) |
| `taskWorkers: N` | Up to `N` parallel OS workers (capped at 16) |
| `host.shutdown()` | Terminates task workers (and TCP worker if any) |

Limits:

- **Isolated heaps** — workers do not share linear memory with main. Contexts must
  be primitive/pointer **slots** as produced by Absolute codegen. Pointers into
  the main heap are not valid on the worker.
- **Not shared-memory wasm threads** — no `memory.atomic` pthread model yet.
- Job queue holds a fixed number of in-flight tasks (32); enqueue fails (abort)
  if saturated.

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

### WASI runtime (preview1)

Link against `absolute_wasm_runtime_wasi.o` (no custom `env.*` Absolute host):

| WASI import | Absolute use |
|-------------|--------------|
| `fd_write` | `println` / assert console |
| `clock_time_get` | `absolute_time_unix_*`, monotonic |
| `random_get` | `absolute_random_entropy` (+ PRNG seed) |
| `args_sizes_get` / `args_get` | `absolute_process_args_*` |
| `environ_*` | seed for `absolute_env_*` (table capped) |
| `proc_exit` | `absolute_process_exit` / abort |

How to build:

```bat
absolutec program.abs --target wasm32-unknown-unknown --emit-object -o program.o
wasm-ld --no-entry --export-all program.o absolute_wasm_runtime_wasi.o -o program.wasm
```

Or, after rebuilding `absolutec` with `ABSOLUTE_WASM_WASI_OBJECT`:

```bat
set ABSOLUTE_WASM_RUNTIME=wasi
absolutec program.abs --target wasm32-unknown-wasi --build-exe -o program.wasm
```

Run:

```bat
node tools/absolute-wasm-wasi-run.js program.wasm --env KEY=val --arg extra
wasmtime run --invoke main program.wasm
```

Node's experimental WASI requires the `_initialize` export (provided by the WASI
runtime object) and `wasi.initialize(instance)` before calling `main`.

Tests: `absolute.build-wasm-wasi-smoke`, `absolute.run-wasm-wasi-services`.

**Not included yet:** linking Absolute modules against wasi-libc (symbol clash with
Absolute's heap/printf). Absolute still ships its own heap/VFS/printf subset.

### Optional wasi-sysroot (headers + libc.a)

For experiments / extra C objects (not required for Absolute wasm programs):

```powershell
pwsh scripts/windows/bootstrap-wasi-sysroot.ps1
# sets up .absolute/toolchains/wasi-sysroot-25.0
$env:WASI_SYSROOT = (Resolve-Path .absolute\toolchains\wasi-sysroot-25.0\...)
```

CMake loads `cmake/AbsoluteWasi.cmake` and reports `ABSOLUTE_WASI_SYSROOT` /
`ABSOLUTE_WASI_LIBC` when present. Absolute's default link path still uses
`absolute_wasm_runtime*.o` only.

### Browser embedder

| Piece | Role |
|-------|------|
| `examples/wasm/` | Demo UI (main vs worker mode) |
| `tools/absolute-wasm-browser-host.js` | Main-thread mocks library |
| `tools/absolute-wasm-browser-session-worker.js` | Runs the module off UI thread |
| `tools/absolute-wasm-browser-session-client.js` | Main-thread RPC client |
| `tools/absolute-wasm-ws-tcp-worker.js` | Nested WebSocket TCP + SAB/Atomics |
| `tools/absolute-wasm-browser-task-worker.js` | Nested task pool workers (isolated instances) |
| `scripts/serve-wasm-demo.mjs` | Static server with COOP/COEP + `/tools/*` |

```bat
node scripts/serve-wasm-demo.mjs
```

- **main mode:** HTTP/TCP mocks; no `Atomics.wait` (forbidden on UI thread).
- **worker mode:** session Worker can block; with `crossOriginIsolated` and
  `wsMap`, TCP uses nested WebSocket worker (same Atomics protocol as Node).
  `taskWorkers: N` starts N nested task workers (same job SAB layout as Node).
- Tests: `absolute.run-wasm-browser-host`, `absolute.run-wasm-browser-session`,
  `absolute.run-wasm-browser-task-pool`.

### Shared-memory modules (experimental)

Optional second runtime object with a locked heap for multi-threaded hosts:

| Piece | Role |
|-------|------|
| `absolute_wasm_runtime_shared.o` | `-matomics -mbulk-memory -DABSOLUTE_WASM_SHARED` |
| `ABSOLUTE_WASM_RUNTIME=shared` or `ABSOLUTE_WASM_SHARED=1` | absolutec links shared object |
| wasm-ld flags | `--shared-memory --import-memory --max-memory=16777216` |
| Host | creates `WebAssembly.Memory({ shared: true })` as `env.memory` |

```bat
set ABSOLUTE_WASM_RUNTIME=shared
absolutec tests\wasm-smoke.abs --target wasm32-unknown-unknown --build-exe -o shared.wasm
```

Manual link (without rebuilt absolutec):

```bat
absolutec tests\wasm-smoke.abs --target wasm32-unknown-unknown --emit-object -o smoke.o
wasm-ld --shared-memory --import-memory --max-memory=16777216 --no-entry --export-all ^
  smoke.o absolute_wasm_runtime_shared.o -o shared.wasm
```

`instantiateAbsoluteWasm` detects the `env.memory` import and supplies a shared
Memory. Export `absolute_wasm_shared_memory_enabled()` returns 1 on shared builds.
Heap `malloc`/`free` use a global spinlock when `ABSOLUTE_WASM_SHARED` is defined.

### Shared-instance task pool

When a **shared-memory** module is instantiated with `taskWorkers: N > 0`, the host
uses `absolute-wasm-shared-task-worker.js`:

1. Host and each worker instantiate the **same** module with the **same**
   `WebAssembly.Memory` (shared).
2. Heap control (`lock`, `break`, free-list) lives in linear memory so every
   instance sees one arena (wasm globals alone are per-instance).
3. `spawn` enqueues `(entryIndex, contextPtr)`; the worker calls
   `table.get(entry)(contextPtr)` **in place** (no context byte copy).

```js
const { exports, taskPoolMode, sharedMemory } = await instantiateAbsoluteWasm(bytes, {
  taskWorkers: 2, // => taskPoolMode === 'shared' for shared modules
});
```

Non-shared modules still use the isolated pool (`absolute-wasm-task-worker.js`,
context copy).

Limits:

- Not a full pthread / wasi-threads model (no TLS, no automatic C thread spawn).
- Browser pages need COOP/COEP for `SharedArrayBuffer`.
- Default (non-shared) modules keep exported non-shared memory for simple embeds.
- Managed/global Absolute state beyond the locked heap is still largely
  single-thread oriented.

Tests: `absolute.run-wasm-shared-memory`, `absolute.run-wasm-shared-tasks`.

### wasi-libc coexistence (selective kits)

Full `libc.a` **cannot** be linked with Absolute: `wasm-ld` errors on duplicate
`malloc` / `exit` / `printf` / … Absolute keeps its heap and console; wasi-libc
is pulled in as **selected `.o` members** for symbols Absolute does not provide.

| Piece | Role |
|-------|------|
| `scripts/windows/bootstrap-wasi-sysroot.ps1` | wasi-sysroot + compiler-rt builtins |
| `cmake/AbsoluteWasi.cmake` | discovers `ABSOLUTE_WASI_SYSROOT` / `ABSOLUTE_WASI_LIBC` |
| `cmake/AbsoluteWasiLibcExtras.cmake` | kit lists + `llvm-ar` extract |
| `tests/wasi-libc-probe.c` | `wasi_libc_strtol` / optional `wasi_libc_strtod` |
| `tests/run-wasm-wasi-libc.cmake` | link Absolute WASI runtime + kit + builtins |

| Kit (`WASI_LIBC_KIT=`) | Provides |
|------------------------|----------|
| `STRTOL` | `strtol` (+ minimal scan glue) |
| `STRTOD` (default) | `strtol` + `strtod`/`atof` (+ float/stdio/fd glue) |

```bat
powershell -File scripts\windows\bootstrap-wasi-sysroot.ps1
# WASI_LIBC_KIT=STRTOD|STRTOL
```

Link order: Absolute object → probe → `absolute_wasm_runtime_wasi.o` → kit objects
→ `libclang_rt.builtins-wasm32.a`. **Never** full `-lc` while Absolute defines
malloc/printf (guest-on-libc would need Absolute to drop those and use wasm32
`size_t` malloc ABI — not done).

Adding a kit: start from a root `.o`, resolve remaining `env` imports with
`llvm-nm -A libc.a`, append members until only `wasi_snapshot_preview1` remains.

Test: `absolute.run-wasm-wasi-libc` (when sysroot is present at configure).

### Browser shared-instance tasks

Session Worker (`absolute-wasm-browser-session-worker.js`) detects imported
shared `env.memory` and, with `taskWorkers: N`, starts nested
`absolute-wasm-browser-shared-task-worker.js` workers that share the same
`WebAssembly.Memory` and run `entry(contextPtr)` in place (same model as Node
`taskPoolMode: 'shared'`). Non-shared modules keep isolated browser task workers.

Requires COOP/COEP (`scripts/serve-wasm-demo.mjs`).

## Next steps (not done)

1. Guest-on-libc mode (Absolute without custom malloc; size_t ABI).
2. Full wasi-threads / TLS-aware Absolute runtime.
3. More kits (qsort, locale, …) as needed.

## Acceptance criteria progress

- [x] Explicit CLI/target selection (`--target`)
- [x] Documented wasm emit/link limits (this file)
- [x] Engine smoke: Node for export/smoke/managed/task modules
- [x] Host console via `env.absolute_log`
- [x] Browser loader example (`examples/wasm`)
- [x] Clear errors for ASan / non-wasm cross `--build-exe`
- [x] Host backends remain default when `--target` is omitted

