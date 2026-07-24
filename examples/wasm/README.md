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

Programs that only use `println` / `assert` / scalars can also build when the
wasm console shim is present (`Absolute-Runtime/wasm/absolute_wasm_shim.c`):

```bat
absolutec tests\wasm-smoke.abs --target wasm32-unknown-unknown --build-exe -o examples\wasm\module.wasm
```

Managed heap, tasks, `load`, FS, and sockets still need a real wasm runtime port.

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
