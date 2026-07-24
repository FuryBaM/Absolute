# `absolute.desktop`

Windows/Win32 and Linux/X11 desktop runtime for Absolute. It provides a native resizable window,
event polling, keyboard and mouse state (held + edge), a 32-bit software framebuffer, 2D
primitives (rect, line, circle, blit), high-resolution time, frame delta, and sleeping for game loops.

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
- Codes: `Desktop.KeyEscape` (27), arrows `KeyLeft`…`KeyDown`, `KeyW/A/S/D`, `KeySpace`, mouse `0/1/2`

### Timing
- `Desktop.time()` — monotonic seconds
- `window.deltaTime()` — seconds since previous `poll` (0 on first frame)
- `Desktop.sleep(ms)`
- `Desktop.FixedStep(updatesPerSecond)` — accumulator (`add` / `shouldUpdate` / `consume` / `alpha`)

### Soft sprites
- `new Desktop.Sprite(w, h)` — offscreen buffer (`0` = transparent)
- `clear`, `pixel`, `fillRect`, `fillCircle`, `destroy`
- `window.drawSprite(sprite, x, y)`

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

Examples: `examples/desktop/window.abs`, `examples/desktop/pong.abs`.
