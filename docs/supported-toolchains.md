# Supported toolchains

Absolute deliberately supports a small, explicit toolchain range. Updating a
runner image must not silently redefine the compiler contract.

## Required baseline

| Component | Supported baseline | CI / release validation |
| --- | --- | --- |
| CMake | 3.20 or newer | Version shipped by the selected runner or local platform |
| C++ compiler | C++20-capable Clang, MSVC or Apple Clang | Linux Clang 18, Windows MSVC, macOS Apple Clang with LLVM 18 libraries |
| LLVM development SDK | 18 through 21 | LLVM 18 in Linux/macOS/Windows CI; LLVM 21 in Termux |
| Windows LLVM SDK | 18.1.8 | Portable SDK under `.absolute/toolchains/llvm-18.1.8` |
| Node.js | 24 | WebAssembly host tests |
| Wasmtime | 45.0.0 | Linux WASI smoke tests |
| Ninja | 1.11 or newer recommended | Native and Termux builds; other CMake generators remain supported |
| Android / Termux | current Termux packages with LLVM 21 or newer | Native Android ELF build and the supported CTest suite |

LLVM versions older than 18 are rejected by the build scripts. Newer LLVM
versions are not treated as supported merely because configuration succeeds:
they must compile the backend and pass the runtime/IR checks first.

## Platform contracts

### Linux

CI uses Ubuntu 24.04 with the versioned `llvm-18-dev`, `llvm-18-tools`,
`clang-18` and `lld-18` packages. Debug and Release configurations run as
separate matrix entries. A failure in one entry does not cancel the others.

### Windows

Frontend-only Debug and Release jobs use the current Visual Studio C++ runner.
The native backend job uses the portable LLVM 18.1.8 SDK and MSVC. Bootstrap,
build and CTest are separate steps so a network/toolchain failure cannot be
mistaken for a compiler regression.

### macOS

The smoke job uses Homebrew `llvm@18`. Falling back silently to an arbitrary
latest LLVM is intentionally forbidden because that turns CI into an API-change
lottery.

### WebAssembly

Node.js 24 is the browser/Node host baseline. Linux WASI execution uses
Wasmtime 45.0.0. The archive is downloaded only during the toolchain bootstrap
step with bounded retries; compiler builds and tests are never retried.

### Termux

The real target is Android/bionic, not an Ubuntu job with an optimistic name.
`termux-host-contract` only checks shell syntax, CMake options and the frontend
build contract on GitHub-hosted Linux. Full validation remains a native Termux
run:

```bash
bash build-termux.sh --clean --test
```

The current validated Termux configuration uses LLVM 21. LeakSanitizer is not
available in Android ASan, so the dedicated runtime leak test is disabled there;
compile-time leak diagnostics, UAF and double-free coverage remain enabled.

## CI failure policy

Toolchain bootstrap is isolated from configure, build and test steps. Only
network/package bootstrap commands have bounded retries. Deterministic compile,
link, runtime, sanitizer and fuzz failures are reported immediately and are not
hidden by automatic reruns.

Failed jobs upload their configure/build/CTest logs, CTest temporary files,
generated LLVM IR, WebAssembly modules and relevant failed executables for seven
days. The `CI required gate` job combines the supported platform jobs into one
status suitable for a branch-protection rule.

## Updating a supported version

A version bump must update all of the following in one change:

1. `.github/workflows/ci.yml`;
2. the corresponding bootstrap script or cache key;
3. this document;
4. any version-specific compatibility code and tests.

The change is complete only after the full matrix passes without a manual rerun.
