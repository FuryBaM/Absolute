# `absolute.shader` — multi-target GPU IR

Opaque plugin AST for shader stages. Declares typed inputs/outputs/uniforms and
emits **first-class GPU IR** into the Absolute module:

| IR | Kind | Accessor (per stage, e.g. `Vertex`) |
|----|------|--------------------------------------|
| GLSL 330 / compute 430 | `SOURCE_TEXT` / `glsl` | `absolute_shader_glsl_Vertex()` |
| HLSL | `SOURCE_TEXT` / `hlsl` | `absolute_shader_hlsl_Vertex()` |
| MSL (Metal source) | `SOURCE_TEXT` / `msl` | `absolute_shader_msl_Vertex()` |
| SPIR-V | `ABSOLUTE_ARTIFACT_SPIRV` | `absolute_shader_spirv_Vertex()` + `_size` / `_has` |
| DXIL | `ABSOLUTE_ARTIFACT_DXIL` | `absolute_shader_dxil_Vertex()` + `_size` / `_has` |
| Metal IR (AIR) | `ABSOLUTE_ARTIFACT_METAL_IR` | reserved (`_has` is 0 until metallib is wired) |

SPIR-V and DXIL are produced at emit-time with portable **DXC** when available
(`.absolute/toolchains/dxc-spirv` or `ABSOLUTE_DXC`). Without DXC, source IR
still emits and binary sizes are 0.

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
extern "C" string absolute_shader_hlsl_Vertex();
extern "C" string absolute_shader_msl_Vertex();
extern "C" int32 absolute_shader_has_spirv_Vertex();
extern "C" int32 absolute_shader_spirv_size_Vertex();

// OpenGL RHI (GLSL):
auto program = gpu.createShader(
    absolute_shader_glsl_Vertex(),
    absolute_shader_glsl_Fragment());

// D3D11 / D3D12 / Vulkan RHI (HLSL):
auto programD3D = gpu.createShader(
    absolute_shader_hlsl_Vertex(),
    absolute_shader_hlsl_Fragment());
```

## Compute + `absolute.desktop`

The canonical `Compute` stage exposes `Shader.generatedComputeHlsl()` through
the plugin prelude, so user code does not need to declare an `extern` accessor:

```absolute
@shader.stage(Compute)
shader Compute {
    // Declaration order maps to D3D11 UAV slots u0, u1, ...
    storage float values;
    workgroup 1, 1, 1;

    // Body of the generated HLSL main(uint3 id : SV_DispatchThreadID).
    hlsl {
        values[id.x] = values[id.x] * 2.0;
    }
}

auto compute = gpu.createComputeShader(Shader.generatedComputeHlsl());
auto storage = gpu.createStorageBuffer(values);
gpu.bind(compute);
gpu.bindStorage(storage, 0);
gpu.dispatch(values.length, 1, 1);
storage.download(values);
```

The shader plugin owns stage parsing and HLSL/DXIL/SPIR-V generation. The
desktop plugin owns D3D11 execution, UAV storage and CPU readback. Compute
storage is currently `float` only and the desktop execution backend is D3D11.

## DSL

| Declaration | Meaning |
|-------------|---------|
| `input [type] name;` | Vertex attribute (or fragment varying). Types: `float`, `float2`/`vec2`, `float3`/`vec3`, `float4`/`vec4`, `int`. Untyped names keep legacy inference (`position`→float3, `uv`→float2, …). |
| `output [type] name;` | Stage output (`clipPosition` → `gl_Position` / `SV_POSITION` / `[[position]]`). |
| `uniform [type] name;` | Uniform / cbuffer field (bound via `gpu.setUniformF` / `setUniformI`). |
| `storage float name;` | Compute read/write storage. Declaration order becomes HLSL UAV bindings `u0..u7`. |
| `workgroup x, y, z;` | Compute thread-group size emitted as HLSL `numthreads(x,y,z)`. |
| `hlsl { ... }` | Compute statements inserted into generated `main(uint3 id : SV_DispatchThreadID)`. Storage and entry-point declarations are generated automatically. |
| `code { ... }` | Optional **full GLSL** body for the stage. Nested `{`/`}` supported. If omitted, a default mesh-style body is generated for GLSL/HLSL/MSL. `#version 330 core` is always prepended when missing. **Do not write `#...` in the body** — Absolute’s lexer rejects `#`. Custom `code` skips HLSL→SPIR-V/DXIL auto-compile. |

`@shader.stage(Name)` must match the block stage name.

**Note:** when this plugin is loaded, `shader` is a global keyword — do not use `shader` as an Absolute variable name (use `program` instead).

## Emitted symbols (per stage, e.g. `Vertex`)

| Symbol | Role |
|--------|------|
| `@absolute.shader.Vertex.input_count` | constant i32 |
| `@absolute.shader.Compute.storage_count` / `.workgroup` | compute reflection constants |
| `@absolute.shader.Vertex.glsl` / `.hlsl` / `.msl` | private source text |
| `@absolute.shader.Vertex.spirv` / `.dxil` / `.metal_ir` | private binary blobs |
| `absolute_shader_glsl_Vertex()` | GLSL source string |
| `absolute_shader_hlsl_Vertex()` | HLSL source string |
| `absolute_shader_msl_Vertex()` | MSL source string |
| `absolute_shader_*_size_Vertex()` | byte length (source without NUL; binary raw size) |
| `absolute_shader_has_spirv_Vertex()` | 1 if SPIR-V produced |
| `absolute_shader_has_dxil_Vertex()` | 1 if DXIL produced |
| `absolute_shader_has_metal_ir_Vertex()` | 1 if Metal AIR produced (currently 0) |
| `absolute_shader_input_count_Vertex()` | reflection |
| `absolute_shader_input_components_Vertex(i32)` | reflection |
| `absolute_shader_vertex_stride_floats_Vertex()` | packed float stride |
| `absolute_shader_storage_count_Compute()` | compute storage count |
| `absolute_shader_workgroup_{x,y,z}_Compute()` | compute local group dimensions |
| `Shader.generatedComputeHlsl()` | namespaced, predeclared bridge for `Desktop.Gpu.createComputeShader` |
| `absolute_shader_artifact_kind_{spirv,dxil,metal_ir,source_text}()` | `AbsoluteArtifactKindV1` constants |

Without `code { }`, generated bodies implement a default lit mesh pipeline (Y-rotation when `uTime` is present).

## RHI bind examples

```powershell
absolutec examples/desktop/shader-rhi.abs `
  --plugin path/to/absolute-desktop.absplugin `
  --plugin path/to/absolute-shader.dll `
  --build-exe -o shader-rhi.exe

# Multi-IR smoke (no window):
absolutec examples/desktop/shader-multi-ir-smoke.abs `
  --plugin path/to/absolute-shader.dll `
  --build-exe -o shader-multi-ir-smoke.exe

# End-to-end shader DSL → generated HLSL → D3D11 dispatch:
absolutec examples/desktop/shader-compute.abs `
  --plugin path/to/absolute-desktop.absplugin `
  --plugin path/to/absolute-shader.dll `
  --build-exe -o shader-compute.exe
```

See `examples/desktop/shader-rhi.abs`, `shader-code.abs`,
`shader-multi-ir-smoke.abs`, and `shader-compute.abs`.

## Build plugin

```powershell
cmake --build .absolute/build/windows-release --target Absolute-Shader-Plugin
```
