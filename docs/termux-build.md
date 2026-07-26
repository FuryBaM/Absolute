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
`libllvm-static` component archives, LLD, Node.js, and the native libraries
needed by LLVM. It does not run a full `pkg upgrade` unless `--upgrade` is also
supplied.

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

Termux splits the LLVM shared runtime and component archives into separate
packages. `LLVMConfig.cmake` exports component targets such as
`LLVMDemangle`, so a full Absolute backend build needs `libllvm-static` even
though `clang`, `llvm`, and `libllvm` are already installed.

If CMake reports that `libLLVMDemangle.a` does not exist, repair the toolchain
and restart configuration:

```bash
pkg update
pkg install libllvm-static
bash build-termux.sh --clean
```

If the previous configure stopped in LLVM's `FindFFI.cmake` with
`C: needs to be enabled before use`, update the Absolute branch. The CodeGen
subproject enables both C and C++ because LLVM performs a small C compile/link
check while locating libffi.

The bootstrap and build scripts now check for
`$PREFIX/lib/libLLVMDemangle.a` before starting CMake and print the package
command directly instead of letting LLVM fail several screens later.

## Notes

- Absolute requires LLVM 18 or newer. Termux may ship a newer LLVM version than
  the Windows-validated LLVM 18 toolchain, so LLVM API changes can expose build
  errors that are not present on Windows.
- Node.js is required only for the Node WebAssembly host and WASM tests.
- A frontend-only build needs much less storage and can be selected with
  `--frontend`.
- The scripts must run inside Termux, not through Android shared storage mounted
  with restrictive execution permissions. Keep the repository under `$HOME`.
