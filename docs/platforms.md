# Supported platforms

Absolute is a **host-native** compiler today: object code and linking use the
machine and toolchain that built `absolutec`. There is no `--target` /
cross-compile switch.

## Host matrix

| Host OS | Arch | Compiler build | `absolutec` codegen / link | CI |
|---------|------|----------------|----------------------------|----|
| Windows 10+ | x64 | MSVC + portable LLVM 18.1.8 | COFF `.obj` + `link.exe` | Yes: frontend matrix + full LLVM Release job |
| Linux | x64 | GCC/Clang + system or distro LLVM | ELF `.o` + host `c++` driver | Yes: Ubuntu Debug/Release with LLVM |
| macOS | x64 or Apple Silicon (arm64) | Apple Clang / Homebrew LLVM | Mach-O `.o` + host C++ driver | Yes: smoke job (configure, build, `ctest`) |
| Windows | ARM64 | Not productized | Would follow host if bootstrapped | No portable ARM64 LLVM bootstrap yet |
| Linux | ARM64 | Untested host-native | Host triple if built on ARM | Optional future runner |

Details for Windows are in [`windows-build.md`](windows-build.md). WSL builds
should keep the CMake build tree on the Linux filesystem (see presets
`wsl-release` / `wsl-release-ninja`).

## What “supporting a platform” means

1. **Build Absolute** on that host with CMake (and LLVM when the backend is on).
2. **Run `ctest`** for semantic, emit, and native runtime tests available on the host.
3. **Emit objects/executables** for the **same** host triple
   (`llvm::sys::getDefaultTargetTriple()`).

It does **not** mean cross-compiling from Windows to Linux, or from x64 to
ARM64, until an explicit target triple CLI and sysroot story exist.

## Codegen and link assumptions

| Concern | Behavior |
|---------|----------|
| Triple | Host default only |
| Object format | Host LLVM object (COFF / ELF / Mach-O) |
| Windows link | `ABSOLUTE_HOST_LINKER` (`link.exe`) + UCRT / system libs |
| POSIX link | Host C++ driver (`c++` / `clang++`) + runtime + optional `-ldl` |
| ASan (Windows) | Currently assumes x86_64 runtime library names |
| Desktop plugin | Win32 or X11 / headless; no Cocoa backend yet |
| Dynamic `load` | `.dll` on Windows, `.so` on Linux; macOS uses `dlopen` for `.dylib` |

## Continuous integration

Defined in `.github/workflows/ci.yml`:

| Job | Runner | Scope |
|-----|--------|--------|
| `build-and-test` | `ubuntu-latest`, `windows-latest` × Debug/Release | Linux full LLVM; Windows frontend (`ABSOLUTE_ENABLE_LLVM=OFF`) |
| `windows-llvm-release` | `windows-latest` | `build-windows.bat --bootstrap` (portable LLVM 18.1.8) |
| `macos-smoke` | `macos-latest` | Homebrew LLVM, Release configure/build/`ctest` |

`macos-latest` is typically **Apple Silicon** on current GitHub runners, so the
smoke job exercises an **ARM64 macOS host** without a separate ARM Linux/Windows
job.

## ARM64 status

- **macOS arm64:** covered by the macOS CI smoke job when the runner is ARM.
- **Linux arm64:** not in CI; host-native build should work with distro LLVM if
  you configure CMake on that machine.
- **Windows arm64:** blocked on a validated portable LLVM SDK and ASan library
  names; Windows presets currently pin x64 MSVC.

## WebAssembly

Experimental target via `absolutec --target wasm32-unknown-unknown`:

- `--emit-llvm` / `--emit-object` / `--build-exe` (`wasm-ld`)
- wasm runtime: heap, managed pointers, errors, sync tasks, virtual FS, env/process
- Node host imports: console, HTTP mocks/prefetch, TCP mocks **or** real OS sockets
  (worker + `Atomics.wait` bridge; see `wasm-target.md`)
- Node engine smoke tests; browser demo under `examples/wasm/`

Dynamic `load` and shared-memory wasm threads are not ported yet. WASI preview1
services work via `absolute_wasm_runtime_wasi.o` + Node WASI/wasmtime. Optional
wasi-sysroot can be bootstrapped for headers/libc experiments. Browser: main-thread
mocks or Worker session (`serve-wasm-demo.mjs` COOP/COEP; WebSocket TCP via nested
worker). Node can opt into a task worker pool (`taskWorkers`). See
[`wasm-target.md`](wasm-target.md).

## Related docs

- [`windows-build.md`](windows-build.md) — MSVC + portable LLVM workflow
- [`native-c-abi.md`](native-c-abi.md) — C interop at language boundaries
- [`debugging.md`](debugging.md) — native debugger helpers (VS / GDB)
