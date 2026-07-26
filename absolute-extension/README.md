# Absolute Language for VS Code

Language support for `.abs` files with:

- **LSP language intelligence** (`server/lsp-server.js`)
  - completion / hover (plugin editor sidecars + core language)
  - go-to-definition, find references, rename
  - document / workspace symbols
  - semantic highlighting
  - document formatting
  - code actions (format / refresh diagnostics)
  - compiler diagnostics via `absolutec`
- native **build / run / debug**
- project and plugin discovery (safe JSON sidecars only; native DLL/SO never loads in VS Code)

## Commands

- `Absolute: Build and Run` (`Ctrl+F5`)
- `Absolute: Build and Debug` (`F5`)
- `Absolute: Open Project`
- `Absolute: Run Project` / `Absolute: Debug Project`
- `Absolute: Refresh Plugins`
- `Absolute: Show Loaded Plugins`
- `Absolute: Open Opaque Block` — virtual document + breakpoint map for `shader` blocks
- `Absolute: Evaluate Expression` / `Absolute: Open REPL`

See also [docs/debugging.md](https://github.com/FuryBaM/Absolute/blob/main/docs/debugging.md) for natvis, GDB printers, and layouts.

## Settings

| Setting | Purpose |
|--------|---------|
| `absolute.compilerPath` | `absolutec` for builds and LSP diagnostics |
| `absolute.compilerArguments` | Extra compiler args |
| `absolute.buildCommand` | Optional full build command (`{input}`, `{output}`, `{workspace}`, `{plugins}`) |
| `absolute.outputDirectory` | Optional override; projects default to `build/`, files to `.absolute/out/` |
| `absolute.plugins` / `absolute.pluginSearchPaths` | Plugin roots |
| `absolute.editorMetadata` | Editor-only `*.editor.json` sidecars |
| `absolute.debugger` | `auto` / `cppvsdbg` / `cppdbg` |

Example for using the compiler built in this repository:

```json
{
  "absolute.compilerPath": "${workspaceFolder}\\.absolute\\build\\windows-release\\Release\\absolutec.exe"
}
```

First-time setup: `absolute build-compiler --bootstrap` from the repo root.
Application projects build and run normally. Library projects build a
`.dll`, `.so`, or `.dylib`; Run/Debug reports the built library instead of
trying to launch it as an executable.

## Architecture

```
VS Code extension (extension.js)
  ├── build / run / native debug (cppvsdbg / cppdbg)
  └── LSP client (client/lsp-client.js)
        └── absolute-lsp (server/lsp-server.js + language.js)
```

Other editors can speak LSP directly:

```bash
node absolute-extension/server/lsp-server.js
```

## Developer CLI

From the repo root:

```bat
absolute fmt tests\std-log.abs
absolute test
absolute doc std tests -o absolute-api.md
absolute package list examples\chess\Chess.absproj
```

Commands:

- `fmt` — format `.abs` sources
- `test` — run CTest against a known build directory
- `doc` — generate Markdown API outline from symbols
- `package list|resolve` — inspect / dry-resolve project packages
- `eval <expr>` / `repl` — expression evaluator and interactive REPL

## Package

```bash
npx @vscode/vsce package
```

Install the produced `.vsix` with **Extensions: Install from VSIX…**.
