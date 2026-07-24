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

Any static file server works. Example:

```bat
cd examples\wasm
npx --yes serve -p 5173
```

Open `http://localhost:5173/` and either:

- auto-load `module.wasm` if present, or
- choose a `.wasm` file and click **Run exports / main**.

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

**Not on the main thread:** real OS TCP and multi-worker tasks (they need
`Atomics.wait`, which browsers forbid on the UI thread). Host the whole module
in a Worker + COOP/COEP if you need that model.

## Node host / CLI

```bat
node tools/absolute-wasm-run.js examples\wasm\module.wasm
node tools/absolute-wasm-run.js out.wasm --export wasm_add --args 20,22
node tools/absolute-wasm-wasi-run.js out-wasi.wasm --env FOO=1
```

See [`docs/wasm-target.md`](../../docs/wasm-target.md).
