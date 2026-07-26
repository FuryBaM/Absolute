# WebAssembly projects and standalone files

Absolute keeps the same split as native C/C++ tooling:

- a standalone `.abs` file builds for the host by default;
- `absolutec file.abs --target wasm32-unknown-unknown --build-exe -o file.wasm` builds one file as WebAssembly;
- a `.absproj` stores repeatable target, runtime, dependency, plugin and launch settings.

The VS Code extension provides `Absolute: Build and Run as WebAssembly` for an explicit standalone-file run. Normal `Build and Run` remains native unless the selected project declares a WebAssembly target.

Example project:

```json
{
  "name": "wasm-demo",
  "type": "app",
  "entry": "src/main.abs",
  "sources": ["src"],
  "target": "wasm32-unknown-unknown",
  "runtime": "node",
  "runArgs": ["--mode=demo"],
  "plugins": [],
  "pluginSearchPaths": []
}
```

Supported extension runtimes:

- `node`: builds `.wasm` and runs it through `absolute-wasm-host.js`;
- `browser`: builds the module and reports its output path without launching a browser automatically.

`wasm32-wasi` still requires a separate WASI runner. The extension's Node host is intended for `wasm32-unknown-unknown` modules using the Absolute host ABI.
