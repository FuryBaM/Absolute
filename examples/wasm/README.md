# Absolute WebAssembly browser demo

## Build a module

From the repository root (requires portable LLVM with `clang` + `wasm-ld`):

```bat
build-windows.bat -NoTest
.absolute\build\windows-release\Release\absolutec.exe ^
  tests\wasm-export-only.abs ^
  --target wasm32-unknown-unknown ^
  --build-exe -o examples\wasm\module.wasm
```

Programs with `println` / `assert` / managed `new`/`delete` link against the
wasm runtime subset (`Absolute-Runtime/wasm/absolute_wasm_runtime.c`):

```bat
absolutec tests\wasm-smoke.abs --target wasm32-unknown-unknown --build-exe -o examples\wasm\module.wasm
absolutec tests\wasm-managed.abs --target wasm32-unknown-unknown --build-exe -o examples\wasm\module.wasm
```

Tasks, dynamic `load`, FS, and sockets are still not ported.

## Serve locally

Any static file server works. Example:

```bat
cd examples\wasm
npx --yes serve -p 5173
```

Open `http://localhost:5173/` and either:

- auto-load `module.wasm` if present, or
- choose a `.wasm` file and click **Run wasm_add(20, 22)**.

## API

Modules import:

| Import | Purpose |
|--------|---------|
| `env.absolute_log(ptr, len)` | console UTF-8 |
| `env.absolute_http_get(url, out, cap)` | host HTTP / mocks |

```js
const { instantiateAbsoluteWasm } = require('../../tools/absolute-wasm-host.js');
const { exports, logs } = await instantiateAbsoluteWasm(fs.readFileSync('module.wasm'), {
  captureLogs: true,
  httpMocks: { 'https://example.test/hello': 'hello' },
});
```

CLI:

```bat
node tools/absolute-wasm-run.js examples\wasm\module.wasm
node tools/absolute-wasm-run.js out.wasm --export wasm_add --args 20,22
```

WASI console modules (linked with `absolute_wasm_runtime_wasi.o`) can run under
`wasmtime run --invoke main module.wasm` when wasmtime is installed.
