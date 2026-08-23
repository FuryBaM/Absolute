# Continuous integration policy

This document is the source of truth for the required Absolute CI matrix,
toolchain compatibility, failure classification, retries, timeouts, and
diagnostic artifacts.

## Required matrix

| Check | Pinned runner | Contract |
|-------|---------------|----------|
| `build-and-test` | `ubuntu-24.04` | LLVM Debug and Release, complete CTest suite; tests labelled `differential` run on Release only, in a separate step |
| `build-and-test` | `windows-2022` | MSVC frontend Debug and Release contracts |
| `llvm-compatibility` | Ubuntu 24.04/26.04 containers | LLVM 18, 19, 20 and 21 Release codegen contract |
| `windows-llvm-release` | `windows-2022` | Portable LLVM native and WebAssembly Release suite |
| `macos-smoke` | `macos-15` | ARM64 Release configure, build, and CTest |
| `linux-wasm-smoke` | `ubuntu-24.04` | Node and WASI WebAssembly tests |
| `termux-host-contract` | `ubuntu-24.04` | Termux-mode build plus frontend CTest contract |
| `language-stress-and-fuzz` | `ubuntu-24.04` | Scheduler, collection, property, and mutation stress |
| `runtime-thread-sanitizer` | `ubuntu-24.04` | Native scheduler harness under TSan |

`termux-host-contract` validates portable source and build assumptions but does
not pretend to be an Android/Bionic device. A real on-device Termux smoke
requires an ARM64 self-hosted runner and remains an infrastructure task.

## Supported toolchains

| Component | Supported / pinned contract |
|-----------|-----------------------------|
| C++ language | C++20 |
| LLVM and Clang | 18 through 21; full Linux/Windows suite and portable Windows SDK remain pinned to 18.x/18.1.8 |
| MSVC | Visual Studio 2022 17.x in required CI; VS 2019+ frontend/native builds remain accepted locally, while the LLVM 18 Windows ASan suite is validated against the VS 2022 CRT |
| CMake | 3.20 minimum; 3.20 through current 4.x is the compatibility range |
| Ninja | 1.11 minimum; Windows bootstrap uses 1.13.2 |
| Node.js | 24.x |
| WASI SDK/sysroot | 25.0 |
| Wasmtime | 45.0.0 |
| Termux | Current official packages with Clang/LLVM 18+, CMake 3.20+, Ninja 1.11+ |

Every job records the concrete runner and tool versions in its diagnostic log.
Changing a pinned major version is a deliberate compatibility change and must
be reviewed together with this table.

The LLVM compatibility entries intentionally do not duplicate the complete
CTest suite. Each entry builds the Release compiler against exactly one LLVM
major, runs the O0-through-O3 differential executable corpus, emits
representative arrays/OOP/managed-pointer IR, and verifies that IR with the
matching `llvm-as`. The full semantic, runtime, native, and WebAssembly suites
remain pinned to LLVM 18.

The required Linux and Windows LLVM jobs run the same versioned ABI linker
corpus. Native outputs must be ELF on Linux and PE from the MSVC linker on
Windows; the WebAssembly entry must be a wasm module from `wasm-ld`. All three
paths execute the same sources and compare complete stdout with the same
manifest oracle.

## Failure classes

CI writes `failure-classification.txt` before uploading diagnostics:

- `platform-bootstrap`: package manager, download, cache, or hosted image issue;
- `configure`: incompatible CMake/toolchain discovery;
- `build-regression`: compiler or linker failure;
- `test-regression`: deterministic semantic, codegen, native, or WASM failure;
- `runtime-regression`: stress/property runtime failure;
- `fuzz-regression`: generated compiler input found a crash or hang;
- `sanitizer-regression`: ASan/TSan/LSan report or sanitizer harness failure.

Bootstrap commands may be retried up to three times because network mirrors are
external. Configure, build, runtime, fuzz, and sanitizer failures are never
retried automatically. A deterministic failure must remain visible.

## Timeouts

Bootstrap, configure, build, regular tests, stress, fuzz, and sanitizers have
separate step timeouts. Individual CTest programs are limited to 180 seconds
unless they set a longer CMake `TIMEOUT`; stress/fuzz tools additionally
enforce their own shorter per-process limits. The job timeout is a final
containment boundary, not the primary timeout.

The tests labelled `differential` (`absolute.suite-differential`,
`absolute.alias-differential`, `absolute.codegen-fuzz`) rebuild the runnable
corpus at several optimization levels, and the suite differential also
rebuilds it for wasm. They set CMake `TIMEOUT` values of 900–3600 seconds
because of that, and they cannot share the default Ubuntu CTest step with
the rest of the suite: on Debug, `suite-differential` alone occupied the
whole 50-minute budget as test 12; on Release the remaining tests ran out
of time around test 750 of 823. CI therefore excludes the label from the
default Ubuntu, macOS, and Windows LLVM `ctest` invocations (`-LE
differential`, `--parallel 2`) and runs it on Linux Release in a dedicated
step. Debug is omitted there because the comparison is about the shipped
compiler, and the unoptimized one does not finish under the job cap.

## Artifacts

Failed matrix jobs retain logs, CTest temporary output, LLVM IR, WebAssembly
modules, generated reproducers, sanitizer logs, crash cores, and relevant test
binaries for 14 days. Hardening jobs upload diagnostics even when successful so
scheduled-run history remains inspectable.

## Required merge gates

The workflows expose two unique aggregate checks:

- `CI required gate`
- `Hardening required gate`

The `master` branch ruleset must require both checks, require the branch to be
up to date, and reject bypasses for ordinary contributors. Workflow YAML cannot
enable repository branch protection by itself; an administrator must configure
these two names in the GitHub ruleset after they have appeared in a workflow
run.
