#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bash scripts/termux/bootstrap-termux.sh [options]

Options:
  --upgrade       Upgrade installed Termux packages before installing dependencies.
  --no-update     Skip `pkg update`.
  --no-node       Do not install Node.js (WASM run/tests will be unavailable).
  --ccache        Install ccache for cached compiler builds.
  -h, --help      Show this help.
USAGE
}

update_packages=1
upgrade_packages=0
with_node=1
with_ccache=0

while (($#)); do
    case "$1" in
        --upgrade) upgrade_packages=1 ;;
        --no-update) update_packages=0 ;;
        --no-node) with_node=0 ;;
        --ccache) with_ccache=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if ! command -v pkg >/dev/null 2>&1; then
    echo "This bootstrap script must be run inside Termux (the pkg command was not found)." >&2
    exit 1
fi

if [[ -z "${PREFIX:-}" || ! -d "${PREFIX:-}" ]]; then
    echo "Termux PREFIX is not available. Start a normal Termux shell and try again." >&2
    exit 1
fi

if ((update_packages)); then
    pkg update -y
fi
if ((upgrade_packages)); then
    pkg upgrade -y
fi

packages=(
    git
    clang
    cmake
    ninja
    llvm
    llvm-tools
    libllvm-static
    libpolly
    lld
    make
    pkg-config
    python
    libffi
    libucontext
    libxml2
    ncurses
    zlib
    zstd
)
if ((with_node)); then
    packages+=(nodejs)
fi
if ((with_ccache)); then
    packages+=(ccache)
fi

pkg install -y "${packages[@]}"

llvm_dir=""
if command -v llvm-config >/dev/null 2>&1; then
    llvm_dir="$(llvm-config --cmakedir 2>/dev/null || true)"
fi
if [[ -z "$llvm_dir" || ! -f "$llvm_dir/LLVMConfig.cmake" ]]; then
    for candidate in \
        "$PREFIX/lib/cmake/llvm" \
        "$PREFIX/lib/llvm/cmake"; do
        if [[ -f "$candidate/LLVMConfig.cmake" ]]; then
            llvm_dir="$candidate"
            break
        fi
    done
fi

if [[ -z "$llvm_dir" || ! -f "$llvm_dir/LLVMConfig.cmake" ]]; then
    echo "Dependencies were installed, but LLVMConfig.cmake was not found." >&2
    echo "Check: llvm-config --cmakedir" >&2
    exit 1
fi

if [[ ! -f "$PREFIX/lib/libLLVMDemangle.a" ]]; then
    echo "Termux LLVM CMake metadata is installed, but its component archives are missing." >&2
    echo "Expected: $PREFIX/lib/libLLVMDemangle.a" >&2
    echo "Install the matching package with: pkg install libllvm-static" >&2
    exit 1
fi

if [[ ! -x "$PREFIX/bin/FileCheck" ]]; then
    echo "Termux LLVM CMake metadata references FileCheck, but the executable is missing." >&2
    echo "Expected: $PREFIX/bin/FileCheck" >&2
    echo "Install the matching package with: pkg install llvm-tools" >&2
    exit 1
fi

if [[ ! -f "$PREFIX/lib/LLVMPolly.so" ]]; then
    echo "Termux LLVM CMake metadata references LLVMPolly, but the shared library is missing." >&2
    echo "Expected: $PREFIX/lib/LLVMPolly.so" >&2
    echo "Install the matching package with: pkg install libpolly" >&2
    exit 1
fi

if ! pkg-config --exists libucontext; then
    echo "libucontext was installed, but pkg-config cannot find it." >&2
    echo "Repair it with: pkg reinstall libucontext" >&2
    exit 1
fi
ucontext_include_dir="$(pkg-config --variable=includedir libucontext)"
ucontext_libdir="$(pkg-config --variable=libdir libucontext)"
if [[ ! -f "$ucontext_include_dir/libucontext/libucontext.h" ]]; then
    echo "libucontext header is missing: $ucontext_include_dir/libucontext/libucontext.h" >&2
    echo "Repair it with: pkg reinstall libucontext" >&2
    exit 1
fi

llvm_version="$(llvm-config --version 2>/dev/null || echo unknown)"
echo
echo "Termux toolchain is ready."
echo "  PREFIX:       $PREFIX"
echo "  Clang:        $(clang --version | head -n 1)"
echo "  LLVM:         $llvm_version"
echo "  LLVM_DIR:     $llvm_dir"
echo "  LLVM static:  $PREFIX/lib/libLLVMDemangle.a"
echo "  LLVM tools:   $PREFIX/bin/FileCheck"
echo "  LLVM Polly:   $PREFIX/lib/LLVMPolly.so"
echo "  libucontext:  $ucontext_libdir"
echo
echo "Next:"
echo "  bash build-termux.sh"
