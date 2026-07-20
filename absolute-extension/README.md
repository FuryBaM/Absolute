# Absolute Language for VS Code

Language support for `.abs` files with syntax highlighting, plugin-aware
completion/hover/semantic tokens, native build/run, and native debugging.

The extension reads `plugins` and `pluginSearchPaths` from `.absproj`, follows
`.absplugin` dependencies recursively, and consumes the manifest's safe `editor`
JSON sidecar. Native DLL/SO files are never loaded into the VS Code process.

Commands:

- `Absolute: Build and Run` (`Ctrl+F5`)
- `Absolute: Build and Debug` (`F5`)
- `Absolute: Open Project`
- `Absolute: Run Project` (`Ctrl+F5` while an `.absproj` is open)
- `Absolute: Debug Project` (`F5` while an `.absproj` is open)
- `Absolute: Refresh Plugins`
- `Absolute: Show Loaded Plugins`

For native compiler installations, set `absolute.compilerPath`. For custom
toolchains, set `absolute.buildCommand`; it supports `{input}`, `{output}`,
`{workspace}`, and `{plugins}` placeholders. The native debugger uses
`cppvsdbg` on Windows and `cppdbg` on Linux/macOS, supplied by Microsoft's C/C++
VS Code extension.

The build and debug commands also work when a single `.abs` file is opened
without opening its containing folder. In that mode the source directory is
used as `${workspaceFolder}` and as the default build root.

During plugin development, `absolute.editorMetadata` can point directly to one
or more `*.editor.json` sidecars. They affect IntelliSense only and are not
passed to the compiler as native plugins.

For this repository's Windows + WSL setup:

```json
{
  "absolute.buildCommand": "${workspaceFolder}\\examples\\desktop\\run.bat -Source {input} -Output {output} -NoRun"
}
```
