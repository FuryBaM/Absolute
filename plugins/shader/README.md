# `absolute.shader` opaque AST plugin

This example uses the opaque plugin path rather than source expansion. The
plugin receives an `AbsoluteParserCursorV1`, consumes its complete block, owns
the resulting payload, validates it during semantic analysis, and emits a
standalone LLVM IR module during code generation.

```absolute
@shader.stage(Vertex)
shader Vertex {
    input position;
    input normal;
    output clipPosition;
}
```

The example validates the qualified attribute received from the host and emits
`absolute.shader.Vertex`, plus input/output and metadata count globals.
This is deliberately a small shader DSL: a production plugin can replace the
payload and callbacks with a real shader AST, type checker, SPIR-V/DXIL
compiler, reflection data, or LLVM lowering without changing the Absolute AST.

Opaque nodes use only the versioned C ABI. C++ AST and LLVM C++ objects never
cross the dynamic-library boundary. The emitter returns textual LLVM IR as a
complete module; Absolute parses, verifies, and links it into the destination
module. Plugins are trusted native code, and duplicate or invalid symbols fail
at link/verification time.

Build and load on Windows:

```powershell
cmake --build build --config Release --target Absolute-Shader-Plugin
absolutec program.abs --plugin build/plugins/shader/Release/absolute-shader.dll
```
