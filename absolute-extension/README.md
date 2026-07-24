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

## Settings

| Setting | Purpose |
|--------|---------|
| `absolute.compilerPath` | `absolutec` for builds and LSP diagnostics |
| `absolute.compilerArguments` | Extra compiler args |
| `absolute.buildCommand` | Optional full build command (`{input}`, `{output}`, `{workspace}`, `{plugins}`) |
| `absolute.outputDirectory` | Default `${workspaceFolder}/.absolute/bin` |
| `absolute.plugins` / `absolute.pluginSearchPaths` | Plugin roots |
| `absolute.editorMetadata` | Editor-only `*.editor.json` sidecars |
| `absolute.debugger` | `auto` / `cppvsdbg` / `cppdbg` |

Example for this repository's Windows + WSL desktop pipeline:

```json
{
  "absolute.buildCommand": "${workspaceFolder}\\examples\\desktop\\run.bat -Source {input} -Output {output} -NoRun",
  "absolute.compilerPath": "${workspaceFolder}\\.absolute\\build\\windows-release\\Release\\absolutec.exe"
}
```

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
tools\absolute-dev.bat fmt tests\std-log.abs
tools\absolute-dev.bat test
tools\absolute-dev.bat doc std tests -o absolute-api.md
tools\absolute-dev.bat package list examples\chess\Chess.absproj
```

Commands:

- `fmt` — format `.abs` sources
- `test` — run CTest against a known build directory
- `doc` — generate Markdown API outline from symbols
- `package list|resolve` — inspect / dry-resolve project packages

## Package

```bash
npm run check
npm run package
```

Install the produced `.vsix` with **Extensions: Install from VSIX…**.
