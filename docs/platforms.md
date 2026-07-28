# Supported platforms

Absolute is primarily a **host-native** compiler: native object code and linking
use the machine and toolchain that built `absolutec`. WebAssembly is the explicit
cross-target exception through `--target wasm32-...`.

## Host matrix

| Host OS | Arch | Compiler build | `absolutec` codegen / link | CI |
|---------|------|----------------|----------------------------|----|
| Windows 10+ | x64 | MSVC + portable LLVM 18.1.8 | COFF `.obj` + `link.exe` | Yes: `windows-2022` frontend matrix + full LLVM Release job |
| Linux | x64 | GCC/Clang + system or distro LLVM | ELF `.o` + host `c++` driver | Yes: `ubuntu-24.04` Debug/Release with LLVM |
| macOS | Apple Silicon (arm64) | Apple Clang / Homebrew LLVM | Mach-O `.o` + host C++ driver | Yes: `macos-15` smoke job |
| Android / Termux | Usually arm64 | Termux Clang + Termux LLVM | Android ELF + Termux `clang++` | Host contract in CI; on-device smoke needs a self-hosted runner |
| Windows | ARM64 | Not productized | Would follow host if bootstrapped | No portable ARM64 LLVM bootstrap yet |
| Linux | ARM64 | Untested host-native | Host triple if built on ARM | Optional future runner |

Details for Windows are in [`windows-build.md`](windows-build.md). WSL builds
should keep the CMake build tree on the Linux filesystem (see presets
`wsl-release` / `wsl-release-ninja`). Termux setup and run commands are in
[`termux-build.md`](termux-build.md).

## What “supporting a platform” means

1. **Build Absolute** on that host with CMake (and LLVM when the backend is on).
2. **Run `ctest`** for semantic, emit, and native runtime tests available on the host.
3. **Emit objects/executables** for the same host triple
   (`llvm::sys::getDefaultTargetTriple()`).
4. Optionally emit WebAssembly with an explicit `wasm32` target.

It does **not** mean general native cross-compilation from Windows to Linux, or
from x64 to ARM64, until a complete target-specific linker, sysroot, and runtime
story exists.

## Codegen and link assumptions

| Concern | Behavior |
|---------|----------|
| Triple | Host default for native output; explicit `wasm32-...` for WebAssembly |
| Object format | Host LLVM object (COFF / ELF / Mach-O), or WebAssembly object |
| Windows link | `ABSOLUTE_HOST_LINKER` (`link.exe`) + UCRT / system libs |
| POSIX / Termux link | Host C++ driver (`c++` / `clang++`) + runtime + optional `-ldl` |
| ASan (Windows) | Assumes x86_64 LLVM 18 runtime names and the VS 2022 CRT used by the required runner; newer preview CRTs need a matching sanitizer runtime |
| Desktop plugin | Win32 or X11 / headless; Termux currently uses headless mode |
| Dynamic `load` | `.dll` on Windows, `.so` on Linux/Termux; macOS uses `dlopen` for `.dylib` |

## Continuous integration

Defined in `.github/workflows/ci.yml`:

| Job | Runner | Scope |
|-----|--------|-------|
| `build-and-test` | `ubuntu-24.04`, `windows-2022` × Debug/Release | Linux full LLVM; Windows frontend (`ABSOLUTE_ENABLE_LLVM=OFF`) |
| `llvm-compatibility` | Ubuntu 24.04/26.04 containers | Release backend contract with LLVM 18, 19, 20 and 21 |
| `windows-llvm-release` | `windows-2022` | `build-windows.bat --bootstrap` (portable LLVM 18.1.8) |
| `macos-smoke` | `macos-15` | Homebrew LLVM, Release configure/build/`ctest` |
| `linux-wasm-smoke` | `ubuntu-24.04` | Node/WASI WebAssembly suite |
| `termux-host-contract` | `ubuntu-24.04` | Termux-mode frontend build and CTest contract |
| `language-stress-and-fuzz` | `ubuntu-24.04` | Concurrency, collections, properties, and compiler fuzz |
| `runtime-thread-sanitizer` | `ubuntu-24.04` | Scheduler harness under TSan |

The pinned `macos-15` runner exercises an **ARM64 macOS host** without a separate
ARM Linux/Windows job. GitHub does not provide a normal Termux/Android host
runner, so CI validates the Termux build contract on Linux while true Bionic
execution remains an on-device check.

The complete version, timeout, artifact, retry, and required-gate contract is in
[`ci-policy.md`](ci-policy.md).

## ARM64 status

- **macOS arm64:** covered by the macOS CI smoke job when the runner is ARM.
- **Android / Termux arm64:** experimental native build using the Termux Clang,
  LLVM, CMake, and Ninja packages. The desktop plugin is headless.
- **Linux arm64:** not in CI; host-native build should work with distro LLVM if
  you configure CMake on that machine.
- **Windows arm64:** blocked on a validated portable LLVM SDK and ASan library
  names; Windows presets currently pin x64 MSVC.

## WebAssembly

First-class experimental target: `absolutec --target wasm32-unknown-unknown`
(`--emit-llvm` / `--emit-object` / `--build-exe` via `wasm-ld`).

| Layer | Status |
|-------|--------|
| Runtime | heap, managed, errors, VFS, env/process, tasks |
| Host (Node) | console, HTTP, TCP mocks/real, task pools, shared memory |
| WASI | preview1 console/time/random/args/env; selective wasi-libc kits |
| Browser | mocks on UI thread; Worker session + optional shared tasks (COOP/COEP) |
| CLI helper | `absolute wasm build\|run\|test` |
| CI | `ctest -R wasm` on Windows + Linux |

Not yet: dynamic `load`, full wasi-threads/TLS, guest-on-wasi-libc malloc.
See [`wasm-target.md`](wasm-target.md) and [`examples/wasm/`](../examples/wasm/).

## Related docs

- [`windows-build.md`](windows-build.md) — MSVC + portable LLVM workflow
- [`termux-build.md`](termux-build.md) — Android/Termux native build and runner
- [`native-c-abi.md`](native-c-abi.md) — C interop at language boundaries
- [`debugging.md`](debugging.md) — native debugger helpers (VS / GDB)
