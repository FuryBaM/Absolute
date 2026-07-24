# Absolute WebAssembly browser demo

## Build a module

From the repository root (requires portable LLVM with `clang` + `wasm-ld`):

```bat
build-windows.bat -NoTest
.absolute\build\windows-release\Release\absolutec.exe ^
  tests\wasm-smoke.abs ^
  --target wasm32-unknown-unknown ^
  --build-exe -o examples\wasm\module.wasm
```

Also works with managed / http / net (mock) / task programs:

```bat
absolutec tests\wasm-http.abs --target wasm32-unknown-unknown --build-exe -o examples\wasm\module.wasm
absolutec tests\wasm-net.abs --target wasm32-unknown-unknown --build-exe -o examples\wasm\module.wasm
```

## Serve locally

For **worker mode** and SharedArrayBuffer (WebSocket TCP path), use the COOP/COEP server:

```bat
node scripts/serve-wasm-demo.mjs
```

Open `http://127.0.0.1:5173/` — DevTools should show `crossOriginIsolated === true`.
The server also maps `/tools/*` to the repo `tools/` tree for worker modules.

Plain static servers still work for **main-thread** mock mode only:

```bat
cd examples\wasm
npx --yes serve -p 5173
```

## Browser host imports

| Import | Behavior in the demo |
|--------|----------------------|
| `env.absolute_log` | Append UTF-8 to the page log |
| `env.absolute_http_get` | `window.__ABSOLUTE_HTTP_MOCKS[url]` |
| `env.absolute_tcp_*` | Mock table (`__ABSOLUTE_TCP_MOCKS`, e.g. echo on `127.0.0.1:9`) |
| `env.absolute_task_*` | Pool size `0` → sync tasks inside the module |

Shared library for Node unit tests / tooling:

- `tools/absolute-wasm-browser-host.js` (CJS)
- `tools/absolute-wasm-browser-host.mjs` (ESM)

| Mode | How | TCP / tasks |
|------|-----|-------------|
| main | UI thread | TCP mocks; sync tasks only |
| worker | session Worker | TCP mocks or WebSocket map; `taskWorkers` nested pool when SAB |

Worker mode defaults to `taskWorkers=2` when `SharedArrayBuffer` is available
(`?taskWorkers=0` to disable). If the `.wasm` imports shared `env.memory`
(Absolute shared runtime), the session uses **in-place** shared-instance tasks
(`absolute-wasm-browser-shared-task-worker.js`); otherwise isolated workers
(copy context).

```js
// Optional WebSocket map (worker mode, COOP/COEP page):
window.__ABSOLUTE_WS_MAP = { 'echo.example:443': 'wss://echo.websocket.events/' };
// Clear tcp mocks so the session prefers the WS bridge:
window.__ABSOLUTE_TCP_MOCKS = {};
```

`Atomics.wait` is allowed inside the session Worker (not on the UI thread).

## Node host / CLI

```bat
node tools/absolute-wasm-run.js examples\wasm\module.wasm
node tools/absolute-wasm-run.js out.wasm --export wasm_add --args 20,22
node tools/absolute-wasm-wasi-run.js out-wasi.wasm --env FOO=1
```

See [`docs/wasm-target.md`](../../docs/wasm-target.md).
