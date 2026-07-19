# `absolute.math` plugin

This example demonstrates the optional syntax-plugin prelude hook. Loading the
plugin injects the `Math` namespace before user sources are analyzed; no marker
keyword or manual import is required.

Available scalar functions:

- `Math.abs`, `Math.sqrt`, `Math.sin`, `Math.cos`
- `Math.wrapRadians`

Available managed types and operations:

- `vec2`: `Math.vec2Set`, add, dot, length
- `vec3`: `Math.vec3Set`, add, subtract, dot, length, normalize, cross
- `mat3`: identity and determinant
- `mat4`: identity and multiply
- `Math.projection`: right-handed perspective projection
- `Math.lookAt`: right-handed view matrix

The plugin registers `vec2`, `vec3`, `mat3`, and `mat4` as syntax aliases, so
they are written without the `Math.` namespace. Binary operators are provided
through the generic plugin operator registry:

- `vec2 + vec2`, `vec2 - vec2`, and scalar multiplication
- `vec3 + vec3`, `vec3 - vec3`, and scalar multiplication
- `mat3 * mat3` and `mat4 * mat4`

Matrices use row-major field names (`m00` through `m33`). `projection` expects a
vertical field of view in radians. The scalar implementations are written in
ordinary Absolute and become visible through the prelude, so this plugin needs
no companion runtime library. Trigonometric functions use range reduction and a
finite Taylor approximation; they are suitable as a plugin-system example, not
as a replacement for a correctly rounded platform libm.

Example:

```absolute
auto eye = new vec3(0.0, 0.0, 1.0);
auto center = new vec3(0.0, 0.0, 0.0);
auto up = new vec3(0.0, 1.0, 0.0);
auto view = new mat4();
Math.lookAt(view, eye, center, up);

auto projection = new mat4();
Math.projection(projection, 1.0471975512, 16.0 / 9.0, 0.1, 1000.0);
auto viewProjection = projection * view;
```

Build and load:

```powershell
cmake --build build --config Release --target Absolute-Math-Plugin
absolutec program.abs --plugin build/plugins/math/Release/absolute-math.dll
```
