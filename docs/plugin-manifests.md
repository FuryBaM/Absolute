# Plugin manifests and dependencies

Native libraries can still be loaded directly with `--plugin plugin.dll`.
Use an `.absplugin` manifest when a plugin has dependencies, a version, or
capabilities that other plugins consume.

```json
{
  "name": "absolute.shader.opengl",
  "version": "1.2.0",
  "abi": 1,
  "library": "absolute-shader-opengl.dll",
  "nativeLibraries": ["absolute-shader-runtime.lib", "opengl32.lib"],
  "dependencies": {
    "absolute.shader": ">=1.0.0 <2.0.0",
    "absolute.math": "^1.1.0"
  },
  "provides": ["shader.backend.opengl"],
  "requires": ["shader.ast"]
}
```

`library` is relative to the manifest unless it is absolute. The library's
`AbsoluteSyntaxPluginV1::name` must match the manifest `name`, and `abi` must
match `ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION`.

`nativeLibraries` are automatically passed to the native linker when Absolute
builds an executable. Relative files are resolved beside the manifest; bare
system library names such as `user32.lib` or `pthread` are passed through. This
lets one plugin package contain a compiler adapter and a separate runtime.

Dependencies may use the compact name-to-range object shown above. The resolver
looks for `<name>.absplugin` and then `<name-with-dots-replaced-by-dashes>.absplugin`
beside the requesting manifest and in configured search paths. An explicit path
can be used when a package has a different layout:

```json
{
  "dependencies": [
    {
      "name": "absolute.math",
      "version": "~1.4.0",
      "path": "../math/absolute-math.absplugin"
    }
  ]
}
```

Supported version constraints are exact versions, `=`, `>`, `>=`, `<`, `<=`,
caret ranges such as `^1.2.0`, tilde ranges such as `~1.2.0`, and whitespace or
comma-separated intersections such as `>=1.0.0 <2.0.0`. Manifest versions use
`MAJOR.MINOR.PATCH`.

Load a root manifest from the command line:

```powershell
absolutec program.abs `
  --plugin packages/absolute-shader-opengl.absplugin `
  --plugin-path packages
```

Or configure roots and search directories in a project:

```json
{
  "name": "ShaderDemo",
  "entry": "src/main.abs",
  "sources": ["src"],
  "plugins": ["plugins/absolute-shader-opengl.absplugin"],
  "pluginSearchPaths": ["plugins", "packages"]
}
```

The resolver recursively validates manifests and versions, detects cycles,
loads each native library once, and loads dependencies before dependants.
Libraries are unloaded in the reverse order. A `requires` capability must have
been declared by a dependency's `provides`; duplicate capability providers are
rejected.

Direct library loading is the compatibility mode and has no trustworthy version
metadata. If that plugin later participates in a versioned dependency graph,
load its manifest instead of its DLL/SO directly.
