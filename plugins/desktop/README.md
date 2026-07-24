# `absolute.desktop`

Windows/Win32 and Linux/X11 desktop runtime for Absolute. It provides a native resizable window,
event polling, keyboard and mouse state (held + edge), a 32-bit software framebuffer, 2D
primitives (rect, line, circle, blit), soft sprites/batch/text, a minimal OpenGL RHI
(`Desktop.Gpu` on Windows WGL), high-resolution time, frame delta, and sleeping for game loops.

Build the compiler and plugin in Release:

```powershell
cmake -S . -B .absolute/build/windows-release --preset windows-msvc-release
cmake --build .absolute/build/windows-release --config Release --target Absolute-Desktop-Plugin
```

The build creates `absolute-desktop.absplugin` beside the plugin DLL. The
manifest automatically adds the static runtime and Win32 system libraries when
`--build-exe` is used.

```powershell
.absolute\build\windows-release\Release\absolutec.exe examples\desktop\window.abs `
  --plugin .absolute\build\windows-release\plugins\desktop\Release\absolute-desktop.absplugin `
  --build-exe -o build\desktop-demo.exe
build\desktop-demo.exe
```

Shortest path on a full Windows+WSL toolchain:

```powershell
examples\desktop\run.bat
examples\desktop\run.bat pong.abs
```

## API

### Window lifecycle
- `new Desktop.Window(title, width, height, resizable)`
- `poll()`, `isOpen()`, `close()`, `setTitle()`, `width()`, `height()`

### Drawing (software framebuffer, colors `0x00RRGGBB`)
- `clear(color)`, `pixel(x, y, color)`, `fillRect(...)`, `drawLine(...)`, `fillCircle(...)`
- `blit(x, y, w, h, raw uint32* pixels)` — `0` pixels are transparent
- `present()`

### Input
- Held: `keyDown(key)`, `mouseDown(button)`, `mouseX()`, `mouseY()`
- Edges (updated on each `poll`): `keyPressed` / `keyReleased`, `mousePressed` / `mouseReleased`
- Text: `textCount()`, `textPop()` (code point / ASCII; `-1` empty), `textClear()`
- Gamepad (Windows XInput 0..3; Linux stub): `Desktop.gamepadConnected(i)`,
  `gamepadButton(i, btn)`, `gamepadAxis(i, axis)` in `[-1,1]` (triggers `[0,1]`)
- Codes (functions or `Desktop.Codes.*` statics): `Desktop.KeyEscape()`, arrows,
  `KeyW/A/S/D()`, `KeySpace()`, mouse `MouseLeft()`, pad `PadA()`…, axes `AxisLX()`…

### Timing
- `Desktop.time()` — monotonic seconds
- `window.deltaTime()` — seconds since previous `poll` (0 on first frame)
- `Desktop.sleep(ms)`
- `Desktop.FixedStep(updatesPerSecond)` — accumulator (`add` / `shouldUpdate` / `consume` / `alpha`)

### Soft sprites & images
- `new Desktop.Sprite(w, h)` — procedural offscreen buffer (`0` = transparent)
- `sprite.loadBmp(path)` — 24/32-bit uncompressed BMP (`false` on failure)
- `sprite.loadPng(path)` — PNG via Windows WIC (RGBA; low alpha → transparent `0`);
  currently returns false on non-Windows
- `sprite.loadImage(path)` — try PNG then BMP
- `sprite.colorKey(rgb)` — exact RGB → transparent `0` (e.g. magenta `Desktop.rgb(255, 0, 255)`)
- `clear`, `pixel`, `fillRect`, `fillCircle`, `destroy`, `isValid`
- `window.drawSprite(sprite, x, y)`
- `window.drawSpriteRect(sprite, dx, dy, sx, sy, sw, sh)` — atlas / sub-rect blit (zero-copy pitch)
- `sprite.drawText(x, y, text, color, scale)` — bake soft font into the sprite

### Soft sprite batch / atlas
- `new Desktop.SpriteBatch()` — queue many draws, flush in one pass
- `batch.begin(window, atlas)` — bind window + default atlas
- `batch.draw(x, y)` / `batch.drawRect(dx, dy, sx, sy, sw, sh)` — default atlas
- `batch.drawSprite(sprite, x, y)` / `batch.drawSpriteRect(...)` — other sprites without rebinding
- `batch.setAtlas(sprite)` — switch default atlas (flushes first)
- `batch.flush()` / `batch.end()` — submit queue (`end` also clears binding)
- `batch.count()` — pending entries (0 after flush/end); auto-flush at 8192
- Example: `examples/desktop/batch.abs`

### Soft text (built-in 8×8 font)
- Monospace ASCII 32–126; newline starts a new line; non-ASCII draws `?`
- `window.drawText(x, y, text, color, scale)` — scale clamped to 1..16; transparent background
- `Desktop.measureText(text, scale)` / `Desktop.measureTextHeight(text, scale)`
- `Desktop.fontGlyphWidth()` / `Desktop.fontGlyphHeight()` — unscaled glyph size (8×8)

### Mesh (Wavefront OBJ)
Portable CPU loader → GPU buffers:

- `new Desktop.Mesh()` + `mesh.loadObj(path)` — triangulates faces, `pos3+normal3+uv2`
- Face normals generated when `vn` missing
- `gpu.createVertexBufferFromMesh(mesh)` / `createIndexBufferFromMesh(mesh)`
- `gpu.createLayoutPos3Normal3Uv2()` — stride 32
- Asset: `examples/desktop/assets/cube.obj`
- Example: `examples/desktop/mesh.abs`

### Soft UI toolkit (`Desktop.Ui`)
Immediate-mode widgets over the software framebuffer + mouse:

- `new Desktop.Ui(window, font)` — font may be invalid; falls back to 8×8
- `ui.begin()` / `ui.end()` each frame after `poll`
- `ui.panel`, `label`, `labelDim`, `button` (returns click), `checkbox` (returns state),
  `slider` (returns value), `progress`
- `ui.theme` — colors (`UiTheme`)
- Example: `examples/desktop/ui.abs`

```absolute
auto ui = new Desktop.Ui(window, font);
ui.begin();
if (ui.button(40, 40, 120, 32, "OK")) { /* clicked */ }
checked = ui.checkbox(40, 90, "Option", checked);
value = ui.slider(40, 140, 200, 18, value, 0.0, 1.0);
ui.end();
```

### Audio (WAV mixer)
Windows waveOut software mixer @ 44.1 kHz stereo, up to 32 voices. Non-Windows: stub.

- `new Desktop.Audio()` / `isValid` / `destroy` / `setMasterVolume(0..1)` / `stopAll` / `activeVoices`
- `audio.loadWav(path)` → `Sound*` (8/16-bit PCM mono/stereo; resampled)
- `sound.play(volume)` / `playLoop(volume)` / `stop` / `isPlaying` / `duration`
- Assets: `examples/desktop/assets/beep.wav`, `blip.wav`, `thud.wav`
- Example: `examples/desktop/audio.abs`

### Soft TrueType / system fonts (`Desktop.Font`)
Windows GDI (ClearType raster → soft buffer). Non-Windows: create returns invalid.

- `new Desktop.Font(faceName, pixelHeight, style)`
- Styles: `Font.StyleNormal`, `StyleBold`, `StyleItalic`, `StyleBoldItalic`
- `font.loadFile(path, pixelHeight, style)` — private `.ttf`/`.otf` via `AddFontResourceEx`
- `font.measure` / `measureHeight` / `lineHeight` / `destroy`
- `window.drawFontText(font, x, y, text, color)` — UTF-8, supports `\n`
- `sprite.drawFontText(font, x, y, text, color)`
- Example: `examples/desktop/font.abs`

### GPU / OpenGL RHI (`Desktop.Gpu`)
Windows: WGL + OpenGL 3.3 core when available (legacy fallback). Linux/X11: stub
(`isValid() == false`) until GLX lands. Soft `window.present()` and `gpu.present()` are
separate paths — use GPU clear/present for GL frames.

Resources:
- `createShader(vs, fs)` → `GpuShader*`
- `createVertexBuffer(float[] vertices)` → `GpuBuffer*` (VBO; layout is separate)
- `createIndexBuffer(int32[] indices)` → `GpuIndexBuffer*` (EBO, `drawIndexed`)
- `createSampler(filter, wrap)` → `GpuSampler*` (`FilterNearest`/`FilterLinear`,
  `WrapClamp`/`WrapRepeat`/`WrapMirror` on `Desktop.Gpu`)
- `new VertexLayout(strideBytes)` + `layout.add(location, components, offsetBytes)`
- `createLayoutPos3Color3()` / `createLayoutPos3Uv2()`
- `createPipeline(shader, layout)` → `GpuPipeline*`
- `createTextureFromSprite(sprite)` → `GpuTexture*` (RGBA8 + color-key alpha)

Frame:
```absolute
auto shader = gpu.createShader(vertexSource, fragmentSource);
auto vb = gpu.createVertexBuffer(vertices);
auto ib = gpu.createIndexBuffer(indices);
auto layout = gpu.createLayoutPos3Uv2();
auto pipeline = gpu.createPipeline(shader, layout);
auto sampler = gpu.createSampler(Desktop.Gpu.FilterNearest, Desktop.Gpu.WrapClamp);

gpu.beginFrame();
gpu.clear(0.06, 0.07, 0.12, 1.0);
gpu.bind(pipeline);
gpu.bind(vb);
gpu.bind(ib);
gpu.bind(texture);
gpu.bind(sampler);
gpu.setUniformI("uTex", 0);
gpu.drawIndexed(6);
gpu.endFrame();
gpu.present();
```

- `bind` overloads: pipeline, vertex buffer, index buffer, texture, sampler
- Alpha blending on in `beginFrame`
- Examples: `triangle.abs`, `gpu-sprites.abs` (indexed quads + sampler)

### Game loop patterns

Variable step:

```absolute
while (window.poll()) {
    double dt = window.deltaTime();
    if (dt <= 0.0) { dt = 0.016; }
    if (dt > 0.05) { dt = 0.05; }
    // update with dt, draw, present
}
window.close();
```

Fixed step (e.g. 60 Hz sim):

```absolute
auto fixed = new Desktop.FixedStep(60.0);
while (window.poll()) {
    fixed.add(window.deltaTime());
    while (fixed.shouldUpdate()) {
        // simulate(fixed.step)
        fixed.consume();
    }
    // render(fixed.alpha())
    window.present();
}
```

The Linux backend is enabled when X11 development files are available
(`libx11-dev`). Without them the plugin builds a headless backend so CI can
still compile/link; `Desktop.Window` then reports that it could not open.

Call `window.close()` before leaving `main`. Native window handles are explicit
resources; the current Absolute class model does not yet run RAII destructors.

Examples: `examples/desktop/window.abs`, `pong.abs`, `sprites.abs`, `input.abs`,
`image.abs` (BMP + atlas), `text.abs` (soft font HUD / typing),
`batch.abs` (`SpriteBatch` + atlas tiles), `triangle.abs` (OpenGL pipeline),
`gpu-sprites.abs` (textured ship + atlas), `image-png.abs` (PNG `loadImage`),
`font.abs`, `audio.abs`, `ui.abs`, `mesh.abs` (OBJ + lit GPU cube).
