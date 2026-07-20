# Native Windows build

Absolute can be built, tested, and used on Windows without WSL. The supported
toolchain is x64 MSVC plus the portable LLVM 18.1.8 development SDK.

## Requirements

- Windows 10 or newer;
- Visual Studio 2019 or newer with **Desktop development with C++**;
- CMake 3.20 or newer;
- about 4 GB for the portable LLVM SDK and build artifacts.

## First build

Run from Command Prompt or PowerShell in the repository root:

```bat
build-windows.bat --bootstrap
```

`--bootstrap` downloads the official
`clang+llvm-18.1.8-x86_64-pc-windows-msvc.tar.xz` archive into
`.absolute\downloads` and extracts it into `.absolute\toolchains`. Both paths
are ignored by Git and no administrator access is required. The script finds
Visual Studio with `vswhere`, initializes `vcvars64.bat`, builds Release, and
runs the native test set.

Subsequent builds do not download anything:

```bat
build-windows.bat
```

Useful options:

```bat
build-windows.bat -NoTest
build-windows.bat -Clean
build-windows.bat --frontend
build-windows.bat --sccache
```

The frontend mode builds the parser, analyzer, CLI, and DLL plugins without
LLVM. It is useful on machines where only syntax and semantic diagnostics are
needed.

`--sccache` is optional. It requires `sccache.exe` in `PATH` and switches the
MSVC build to the locally bootstrapped Ninja generator so CMake can use the
compiler launcher. Cached builds are kept separately under
`.absolute\build\windows-release-sccache`.

The default and recommended Windows toolchain is MSVC with the Visual Studio
generator and `/MP`. An experimental `--clang-cl` switch exists for comparing
toolchains, but the compiler version must be accepted by the installed Visual
Studio STL. In particular, Visual Studio 2026 requires Clang 19 or newer while
the validated Absolute backend SDK is LLVM 18.1.8; the build script detects
this combination before configuration and prints a direct diagnostic.

## Using another LLVM SDK

CMake searches these locations in order:

1. `LLVM_DIR` or `ABSOLUTE_LLVM_DIR` pointing at the directory that contains
   `LLVMConfig.cmake`;
2. `ABSOLUTE_LLVM_ROOT` or `LLVM_ROOT` pointing at the SDK root;
3. `.absolute\toolchains\*\lib\cmake\llvm`;
4. common system install directories.

The compiler requires LLVM 18 or newer and is validated with 18.1.8. The
official LLVM 18 portable archive contains a non-relocatable DIA SDK reference;
the Absolute CMake configuration replaces it with `diaguids.lib` from the
active Visual Studio installation.

## Output

The default build directory is `.absolute\build\windows-release`:

- compiler: `Release\absolutec.exe`;
- runtime: `Release\Absolute-Runtime.lib`;
- plugins: `plugins\<name>\Release\*.dll`;
- generated Absolute programs: native COFF `.obj` and Windows `.exe` files.

The compiler invokes the same MSVC toolchain used to build it. Run it from a
Visual Studio developer shell, or launch it through project tasks that
initialize `vcvars64.bat`.

## Benchmarks

All benchmark launchers use the native Windows backend by default:

```bat
benchmarks\build-suite\run.bat
benchmarks\array-suite\run.bat 15 2 1
benchmarks\pointer-object-suite\run.bat 10 2 1
```

Pass `wsl` as the final argument to retain the older comparison path:

```bat
benchmarks\build-suite\run.bat 4 linux wsl
benchmarks\array-suite\run.bat 15 2 1 wsl
benchmarks\pointer-object-suite\run.bat 10 2 1 wsl
```

On Windows, runtime behavior is tested through generated `.exe` files. The
Linux-only `lli` cases are omitted because Windows UCRT implements
`printf`/`snprintf` as inline wrappers that are not JIT-resolvable exports.
