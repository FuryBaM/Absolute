# Absolute desktop examples

Requires the `absolute.desktop` plugin (Win32 on Windows, X11 on Linux).

## Build plugin

```bat
build-windows.bat -NoTest
cmake --build .absolute\build\windows-release --config Release --target Absolute-Desktop-Plugin
```

Plugin output is typically:

`.absolute\build\windows-release\plugins\desktop\Release\absolute-desktop.absplugin`

## Run demos

```bat
examples\desktop\run.bat
examples\desktop\run.bat pong.abs
```

Or with a native Release `absolutec`:

```bat
.set ABSOLUTEC=.absolute\build\windows-release\Release\absolutec.exe
%ABSOLUTEC% examples\desktop\window.abs --plugin ...\absolute-desktop.absplugin --build-exe -o window.exe
window.exe
%ABSOLUTEC% examples\desktop\pong.abs --plugin ...\absolute-desktop.absplugin --build-exe -o pong.exe
pong.exe
```

## Examples

| File | Description |
|------|-------------|
| `window.abs` | Bouncing rect, delta time, line, mouse circle |
| `pong.abs` | Two-player soft Pong (W/S, arrows, Space serve, Esc quit) |
| `sprites.abs` | Soft sprites + `FixedStep` 60 Hz sim (WASD / arrows) |
| `input.abs` | Text queue + mouse/gamepad cursor probe |
| `image.abs` | BMP load, magenta color key, atlas `drawSpriteRect` |
| `image-png.abs` | PNG `loadImage` (WIC) with BMP fallback, same soft demo |
| `font.abs` | `Desktop.Font` system/TTF soft text vs built-in 8×8 |
| `audio.abs` | WAV mixer: play / loop / stop / multi-voice |
| `ui.abs` | Immediate-mode UI: button, checkbox, slider, progress |
| `mesh.abs` | OBJ mesh load + GPU lit rotating cube |
| `shader-rhi.abs` | `absolute.shader` auto-GLSL + reflection → `Desktop.Gpu` |
| `shader-code.abs` | `absolute.shader` custom `code { GLSL }` → Gpu |
| `text.abs` | Built-in 8×8 soft font, scaled HUD, sprite-baked label, typing |
| `batch.abs` | Soft `SpriteBatch`: 120 atlas tiles + ship in one flush |
| `triangle.abs` | GPU RHI: shader + buffer + pipeline + bind/draw (`BackendAuto`) |
| `gpu-sprites.abs` | Indexed textured quads + `GpuSampler` (ship + stars) |
| `d3d-clear.abs` | D3D11 clear/present only (`BackendD3D11`, Windows) |
| `d3d-triangle.abs` | D3D11 HLSL triangle + VB/pipeline/draw |
| `d3d-triangle-smoke.abs` | Non-interactive D3D11 triangle resource check |

## API highlights

- **Frame timing:** `window.deltaTime()`; `Desktop.FixedStep(hz)` for fixed updates
- **Input edges:** `keyPressed` / `keyReleased`, `mousePressed` / `mouseReleased`
- **Held input:** `keyDown`, `mouseDown`, `mouseX` / `mouseY`
- **2D soft buffer:** `clear`, `pixel`, `fillRect`, `drawLine`, `fillCircle`, `blit`, `present`
- **Sprites:** `Desktop.Sprite(w,h)`, `fillRect`/`fillCircle`/`clear`, `window.drawSprite(sprite,x,y)`
- **Images:** `loadBmp` / `loadPng` / `loadImage`, `colorKey`, `drawSpriteRect`
- **Batch:** `Desktop.SpriteBatch`, `begin`/`drawRect`/`drawSprite`/`end`
- **GPU:** `Desktop.Gpu(window, backend)`: OpenGL full RHI or D3D11 clear/present;
  `BackendAuto` / `BackendOpenGL` / `BackendD3D11`
- **Text:** built-in 8×8 `drawText`; `Desktop.Font` + `drawFontText` / `measure`
- **Audio:** `Desktop.Audio` + `loadWav` / `play` / `playLoop` / `stopAll`
- **UI:** `Desktop.Ui` button/checkbox/slider/progress
- **Mesh:** `Desktop.Mesh.loadObj` + GPU buffers / layout
- **Keys:** `Desktop.KeyEscape()`, `KeySpace()`, `KeyW/A/S/D()`, arrows, …

See `plugins/desktop/README.md` for backend details.
