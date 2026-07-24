# `absolute.shader` — reflection + GLSL for RHI bind

Opaque plugin AST for shader stages. Declares typed inputs/outputs/uniforms,
emits **reflection metadata** and **GLSL 330** source into the Absolute module
for binding with `Desktop.Gpu` (OpenGL backend).

```absolute
@shader.stage(Vertex)
shader Vertex {
    input float3 position;
    input float3 normal;
    input float2 uv;
    uniform float uTime;
    uniform float uScale;
    output float4 clipPosition;
    output float3 vNormal;
    output float2 vUv;
}

@shader.stage(Fragment)
shader Fragment {
    input float3 vNormal;
    input float2 vUv;
    output float4 FragColor;
}

extern "C" string absolute_shader_glsl_Vertex();
extern "C" string absolute_shader_glsl_Fragment();

auto program = gpu.createShader(
    absolute_shader_glsl_Vertex(),
    absolute_shader_glsl_Fragment());
```

## DSL

| Declaration | Meaning |
|-------------|---------|
| `input [type] name;` | Vertex attribute (or fragment varying). Types: `float`, `float2`/`vec2`, `float3`/`vec3`, `float4`/`vec4`, `int`. Untyped names keep legacy inference (`position`→float3, `uv`→float2, …). |
| `output [type] name;` | Stage output (`clipPosition` → `gl_Position` in generated VS). |
| `uniform [type] name;` | Uniform (bound via `gpu.setUniformF` / `setUniformI`). |
| `code { ... }` | Optional **full GLSL** body for the stage. Nested `{`/`}` supported. If omitted, a default mesh-style body is generated. `#version 330 core` is always prepended when missing. **Do not write `#...` in the body** — Absolute’s lexer rejects `#`. |

`@shader.stage(Name)` must match the block stage name.

**Note:** when this plugin is loaded, `shader` is a global keyword — do not use `shader` as an Absolute variable name (use `program` instead).

## Emitted symbols (per stage, e.g. `Vertex`)

| Symbol | Role |
|--------|------|
| `@absolute.shader.Vertex.input_count` | constant i32 |
| `@absolute.shader.Vertex.output_count` | constant i32 |
| `@absolute.shader.Vertex.uniform_count` | constant i32 |
| `@absolute.shader.Vertex.input.N.location/components` | reflection |
| `@absolute.shader.Vertex.glsl` | private GLSL string |
| `absolute_shader_glsl_Vertex()` | `i8*` / Absolute `string` GLSL source |
| `absolute_shader_input_count_Vertex()` | reflection |
| `absolute_shader_input_components_Vertex(i32)` | reflection |
| `absolute_shader_vertex_stride_floats_Vertex()` | packed float stride |
| `absolute.shader.Vertex` | empty void stub (compat) |

Without `code { }`, generated GLSL is a default lit mesh pipeline (Y-rotation when `uTime` is present).

## RHI bind examples

```powershell
absolutec examples/desktop/shader-rhi.abs `
  --plugin path/to/absolute-desktop.absplugin `
  --plugin path/to/absolute-shader.dll `
  --build-exe -o shader-rhi.exe

# Custom GLSL body:
absolutec examples/desktop/shader-code.abs `
  --plugin path/to/absolute-desktop.absplugin `
  --plugin path/to/absolute-shader.dll `
  --build-exe -o shader-code.exe
```

See `examples/desktop/shader-rhi.abs` and `shader-code.abs`.

## Build plugin

```powershell
cmake --build .absolute/build/windows-release --target Absolute-Shader-Plugin
```
