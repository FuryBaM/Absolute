# `absolute.math`

`absolute.math` is a portable math layer for ordinary Absolute programs,
desktop graphics, `absolute.shader`, native targets and WebAssembly.

The implementation lives in `absolute-math.prelude.abs`, so generated programs
do not depend on a math DLL. The native plugin is used by `absolutec` only while
compiling: it registers the short type aliases and operators. This is why the
same source works for Windows/Linux executables and `wasm32-unknown-unknown`.

## Loading

Use the generated manifest, not the DLL directly:

```powershell
absolutec app.abs --plugin path/to/absolute-math.absplugin --run
```

The manifest loads:

- the compiler plugin (`absolute-math.dll`/`.so`);
- the ordinary Absolute prelude (`absolute-math.prelude.abs`);
- editor metadata (`absolute-math.editor.json`).

`absolute.desktop` and `absolute.shader` declare `absolute.math` as an optional
dependency. When their generated manifests are next to
`absolute-math.absplugin`, math is loaded automatically.

## Scalar API

- constants: `Math.pi()`, `Math.tau()`, `Math.halfPi()`, `Math.epsilon()`;
- numeric helpers: `abs`, `min`, `max`, `clamp`, `saturate`;
- interpolation: `lerp`, `inverseLerp`, `smoothstep`;
- angles: `radians`, `degrees`, `wrapRadians`;
- functions: `sqrt`, `sin`, `cos`, `tan`;
- comparison: `approximately`.

Scalar and vector calculations use `double` on the CPU. Conversion to GPU
`float` data is explicit through the packing API, so precision is not silently
lost in game logic or wasm.

## Vectors

Short aliases are available without a namespace:

```absolute
auto position = new vec3(1.0, 2.0, 3.0);
auto direction = new vec3(0.0, 1.0, 0.0);
auto moved = position + direction * 4.0;
auto unit = moved.normalized();
println(unit.length());
```

Available types:

- `vec2`: `x`, `y`;
- `vec3`: `x`, `y`, `z`, plus `crossed`;
- `vec4`: `x`, `y`, `z`, `w`, also suitable for RGBA colors.

All vectors support addition, subtraction, multiplication/division by a scalar,
`dot`, `length`, `lengthSquared`, `normalized`, `lerped`, `writeFloat`, and
`toFloatArray`.

Managed values are cleaned up by Absolute at scope exit. Normal code does not
need `delete` or a manual `destroy()` call.

## Matrices and graphics APIs

Matrices are row-major in CPU code:

```absolute
auto model =
    Math.translation(0.0, 1.0, 0.0)
    * Math.rotationY(Math.radians(45.0))
    * Math.scaling(2.0, 2.0, 2.0);

auto worldPosition = model * new vec4(1.0, 0.0, 0.0, 1.0);
```

Camera helpers:

- `Math.lookAtRH`;
- `Math.perspectiveRH` for OpenGL depth `-1..1`;
- `Math.perspectiveRHZeroToOne` for D3D/Vulkan depth `0..1`;
- the older output-parameter forms `Math.lookAt` and `Math.projection` remain
  available.

For desktop/shader interop:

```absolute
float[] vertex = position.toFloatArray();
float[] matrixRowMajor = model.toFloatArray();
float[] matrixColumnMajor = model.toColumnMajorFloatArray();

float[] interleaved = new float[6];
position.writeFloat(interleaved, 0);
direction.writeFloat(interleaved, 3);

auto storage = gpu.createStorageBuffer(interleaved);
```

The shader DSL accepts the same public spellings `vec2`, `vec3`, `vec4`,
`mat3`, and `mat4`. Matrices are allowed as uniforms. Inside generated GLSL
they become `vecN`/`matN`; inside HLSL/MSL they become `floatN`/`floatNxN`.
The CPU objects are not copied implicitly into a GPU program—pack them into
`float[]` and upload them through `Desktop.Gpu`.

## Build and checks

```powershell
cmake --build --preset windows-msvc-release --target Absolute-Math-Plugin
ctest --test-dir .absolute/build/windows-release -C Release -R "math-plugin"
```

The test group covers the native plugin, the combined
`math + shader + desktop` environment, and an executed wasm module.
