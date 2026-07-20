# `absolute.desktop`

Windows/Win32 and Linux/X11 desktop runtime for Absolute. It provides a native resizable window,
event polling, keyboard and mouse state, a 32-bit software framebuffer, simple
pixel/rectangle drawing, high-resolution time, and sleeping for game loops.

Build the compiler and plugin in Release:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Absolute-Compiler Absolute-Desktop-Plugin
```

The build creates `absolute-desktop.absplugin` beside the plugin DLL. The
manifest automatically adds the static runtime and Win32 system libraries when
`--build-exe` is used:

```powershell
build\Release\absolutec.exe examples\desktop\window.abs `
  --plugin build\plugins\desktop\Release\absolute-desktop.absplugin `
  --build-exe -o build\desktop-demo.exe
build\desktop-demo.exe
```

On this repository's Windows + WSL toolchain the shortest command is:

```powershell
examples\desktop\run.bat
```

It builds the LLVM compiler in WSL, cross-compiles the generated IR for Windows,
links the Win32 runtime with Visual Studio, and starts the example.

Main API:

- `new Desktop.Window(title, width, height, resizable)`
- `poll()`, `isOpen()`, `close()`, `setTitle()`
- `clear()`, `pixel()`, `fillRect()`, `present()`
- `keyDown()`, `mouseX()`, `mouseY()`, `mouseDown()`
- `Desktop.rgb()`, `Desktop.time()`, `Desktop.sleep()`

Key codes use Win32 virtual-key values: Escape is `27`, arrows are `37`-`40`,
space is `32`, and letters use their uppercase ASCII values. Mouse buttons are
`0` (left), `1` (right), and `2` (middle). Colors are `0x00RRGGBB`; use
`Desktop.rgb(red, green, blue)` to construct them.

The Linux backend is enabled when X11 development files are available (for
example, the `libx11-dev` package). Without them the plugin builds a headless
backend so CI and servers can still compile/link programs and use timing/color
helpers; `Desktop.Window` then reports that it could not open.

Call `window.close()` before leaving `main`. Native window handles are explicit
resources; the current Absolute class model does not yet run RAII destructors.
