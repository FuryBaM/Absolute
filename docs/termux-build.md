# Termux build

Absolute can be built natively inside Termux with the Termux Clang/LLVM packages.
This is an experimental Android host build: the compiler, native runtime, CLI
plugins, and WebAssembly tooling are the target. The desktop plugin is built in
headless mode because Termux is not an Android window-system backend.

## First build

From a Termux shell:

```bash
pkg install git
git clone https://github.com/FuryBaM/Absolute.git
cd Absolute
git switch agent/stabilize-foundation

bash build-termux.sh --bootstrap
```

`--bootstrap` installs Clang, CMake, Ninja, LLVM, the matching
`libllvm-static` component archives, `llvm-tools`, `libpolly`, LLD, Node.js,
and the native libraries needed by LLVM. It does not run a full `pkg upgrade`
unless `--upgrade` is also supplied.

The compiler is written under:

```text
.absolute/build/termux-release/
```

The exact executable path is printed after the build. Create a convenient
Termux command with:

```bash
bash build-termux.sh --install
absolutec --help
```

## Build options

```bash
# Rebuild without reinstalling packages or running tests.
bash build-termux.sh

# Parser + analyzer only, without LLVM code generation.
bash build-termux.sh --frontend

# Clean build directory first.
bash build-termux.sh --clean

# Small native smoke subset.
bash build-termux.sh --smoke-test

# Complete CTest suite. This is considerably heavier on a phone.
bash build-termux.sh --test

# Enable ccache.
bash build-termux.sh --bootstrap --ccache
bash build-termux.sh --ccache
```

The scripts disable the X11 backend and let `absolute.desktop` use its headless
runtime. A real Android surface/input/audio backend is separate work; installing
Termux:X11 packages does not turn the existing X11 desktop backend into a
supported Android game window automatically.

The full test suite keeps LLVM IR emission and validation enabled, but executes
array and multi-file runtime coverage as native Android ELF binaries. Termux's
`lli`/MCJIT path is not treated as a supported execution backend because current
Termux LLVM builds can crash or block inside the JIT while the same IR links and
runs correctly through `clang++`.

Android's AddressSanitizer does not provide LeakSanitizer. UAF and double-free
tests run with `detect_leaks=0`; the dedicated runtime leak test is reported as
disabled on Termux, while Absolute's compile-time leak diagnostics remain active.

## Compile and run one file

Native execution is the default:

```bash
bash run-termux.sh hello.abs
bash run-termux.sh hello.abs -- first-argument "second argument"
```

The output stays next to the source under `.absolute/out`, matching the normal
standalone-file workflow instead of scattering binaries through the repository.

Build and run the same standalone source as WebAssembly:

```bash
bash run-termux.sh --wasm hello.abs
bash run-termux.sh --wasm hello.abs -- --mode=demo payload
```

Projects are accepted too:

```bash
bash run-termux.sh Demo.absproj
bash run-termux.sh --wasm Demo.absproj
```

For nontrivial programs, keep dependencies, plugins, source directories,
target, runtime, and `runArgs` in the `.absproj` file. A standalone `.abs` stays
useful for small programs, tests, and experiments, just like a single C++ source
file.

## LLVM package troubleshooting

Termux splits LLVM into several packages. A full Absolute backend build needs:

- `llvm` for `llvm-config` and the CMake package;
- `libllvm-static` for component archives such as `libLLVMDemangle.a`;
- `llvm-tools` for development executables exported by `LLVMConfig.cmake`,
  including `FileCheck`;
- `libpolly` for `LLVMPolly.so`, which is also exported by the same LLVM CMake
  package even when Absolute does not directly use Polly.

If CMake reports that `libLLVMDemangle.a` does not exist:

```bash
pkg update
pkg install libllvm-static
bash build-termux.sh --clean
```

If CMake reports that `$PREFIX/bin/FileCheck` does not exist:

```bash
pkg update
pkg install llvm-tools
bash build-termux.sh --clean
```

If CMake reports that `$PREFIX/lib/LLVMPolly.so` does not exist:

```bash
pkg update
pkg install libpolly
bash build-termux.sh --clean
```

If the previous configure stopped in LLVM's `FindFFI.cmake` with
`C: needs to be enabled before use`, update the Absolute branch. The CodeGen
subproject enables both C and C++ because LLVM performs a small C compile/link
check while locating libffi.

The bootstrap and build scripts check the static component archive, `FileCheck`,
and `LLVMPolly.so` before starting CMake, so an incomplete Termux LLVM split
package is reported with the exact package command instead of a failure deep
inside `LLVMExports.cmake`.

## Notes

- Absolute requires LLVM 18 or newer. Termux may ship a newer LLVM version than
  the Windows-validated LLVM 18 toolchain, so LLVM API changes can expose build
  errors that are not present on Windows.
- Node.js is required only for the Node WebAssembly host and WASM tests.
- A frontend-only build needs much less storage and can be selected with
  `--frontend`.
- The scripts must run inside Termux, not through Android shared storage mounted
  with restrictive execution permissions. Keep the repository under `$HOME`.
