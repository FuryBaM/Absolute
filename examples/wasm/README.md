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

`loader.js` instantiates modules with **empty imports**:

```js
WebAssembly.instantiate(bytes, {})
```

Exported Absolute `export "C"` functions appear on `instance.exports`.
