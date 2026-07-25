# Absolute desktop examples

Requires the `absolute.desktop` plugin (Win32 on Windows, X11 on Linux).

## Build plugin

First-time Windows setup (downloads portable LLVM 18 into `.absolute\toolchains`):

```bat
build-windows.bat --bootstrap -NoTest
```

Later rebuilds:

```bat
build-windows.bat -NoTest
cmake --build .absolute\build\windows-release --config Release --target Absolute-Desktop-Plugin
```

Plugin output is typically:

`.absolute\build\windows-release\plugins\desktop\Release\absolute-desktop.absplugin`

## Run demos

Native Windows is the default path (no WSL required). `run.bat` loads the MSVC
environment, finds `absolutec` + the desktop plugin under
`.absolute\build\windows-release`, builds the example with `--build-exe`, and runs it.

```bat
examples\desktop\run.bat
examples\desktop\run.bat pong.abs
examples\desktop\run.bat -Source d3d-triangle.abs -Output .absolute\bin\d3d-triangle.exe -NoRun
```

Optional legacy hybrid pipeline (Linux LLVM in WSL + MSVC link):

```bat
examples\desktop\run.bat -Backend wsl
```

Or invoke a native Release `absolutec` directly:

```bat
set ABSOLUTEC=.absolute\build\windows-release\Release\absolutec.exe
%ABSOLUTEC% examples\desktop\window.abs --plugin .absolute\build\windows-release\plugins\desktop\Release\absolute-desktop.absplugin --build-exe -o window.exe
window.exe
%ABSOLUTEC% examples\desktop\pong.abs --plugin .absolute\build\windows-release\plugins\desktop\Release\absolute-desktop.absplugin --build-exe -o pong.exe
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
| `d3d-sprites.abs` | D3D11 textured ship + sampler + WASD |
| `d3d-sprites-smoke.abs` | Non-interactive D3D11 texture/sampler draw |
| `d3d12-clear.abs` | D3D12 clear/present (`BackendD3D12`, Windows) |
| `d3d12-clear-smoke.abs` | Non-interactive D3D12 clear check |
| `d3d12-triangle.abs` | D3D12 HLSL triangle + VB/PSO/draw |
| `d3d12-triangle-smoke.abs` | Non-interactive D3D12 triangle resource check |

## API highlights

- **Frame timing:** `window.deltaTime()`; `Desktop.FixedStep(hz)` for fixed updates
- **Input edges:** `keyPressed` / `keyReleased`, `mousePressed` / `mouseReleased`
- **Held input:** `keyDown`, `mouseDown`, `mouseX` / `mouseY`
- **2D soft buffer:** `clear`, `pixel`, `fillRect`, `drawLine`, `fillCircle`, `blit`, `present`
- **Sprites:** `Desktop.Sprite(w,h)`, `fillRect`/`fillCircle`/`clear`, `window.drawSprite(sprite,x,y)`
- **Images:** `loadBmp` / `loadPng` / `loadImage`, `colorKey`, `drawSpriteRect`
- **Batch:** `Desktop.SpriteBatch`, `begin`/`drawRect`/`drawSprite`/`end`
- **GPU:** `Desktop.Gpu(window, backend)`: OpenGL full RHI, D3D11/D3D12 mesh RHI
  (D3D12 textures later); `BackendAuto` / `OpenGL` / `D3D11` / `D3D12`
- **Text:** built-in 8×8 `drawText`; `Desktop.Font` + `drawFontText` / `measure`
- **Audio:** `Desktop.Audio` + `loadWav` / `play` / `playLoop` / `stopAll`
- **UI:** `Desktop.Ui` button/checkbox/slider/progress
- **Mesh:** `Desktop.Mesh.loadObj` + GPU buffers / layout
- **Keys:** `Desktop.KeyEscape()`, `KeySpace()`, `KeyW/A/S/D()`, arrows, …

See `plugins/desktop/README.md` for backend details.
