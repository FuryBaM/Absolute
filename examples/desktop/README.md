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
| `text.abs` | Built-in 8×8 soft font, scaled HUD, sprite-baked label, typing |

## API highlights

- **Frame timing:** `window.deltaTime()`; `Desktop.FixedStep(hz)` for fixed updates
- **Input edges:** `keyPressed` / `keyReleased`, `mousePressed` / `mouseReleased`
- **Held input:** `keyDown`, `mouseDown`, `mouseX` / `mouseY`
- **2D soft buffer:** `clear`, `pixel`, `fillRect`, `drawLine`, `fillCircle`, `blit`, `present`
- **Sprites:** `Desktop.Sprite(w,h)`, `fillRect`/`fillCircle`/`clear`, `window.drawSprite(sprite,x,y)`
- **Images:** `sprite.loadBmp(path)`, `colorKey(rgb)`, `drawSpriteRect(..., sx,sy,sw,sh)`
- **Text:** `window.drawText(x,y,text,color,scale)`, `Desktop.measureText`, `sprite.drawText`
- **Keys:** `Desktop.KeyEscape()`, `KeySpace()`, `KeyW/A/S/D()`, arrows, …

See `plugins/desktop/README.md` for backend details.
