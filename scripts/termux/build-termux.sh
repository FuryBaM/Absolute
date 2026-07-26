#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

usage() {
    cat <<'USAGE'
Usage: bash build-termux.sh [options]

Options:
  --bootstrap          Install/update the Termux build dependencies first.
  --upgrade            With --bootstrap, also run `pkg upgrade`.
  --frontend           Build parser/analyzer only (LLVM backend disabled).
  --clean              Remove the selected Termux build directory first.
  --test               Run the complete CTest suite after building.
  --smoke-test         Run a small native compiler/runtime smoke subset.
  --no-tests           Build only (default; aliases: --no-test, --build-only).
  --ccache             Enable ccache when it is installed.
  --no-plugins         Do not build the example plugins.
  --install            Symlink the built compiler to $PREFIX/bin/absolutec.
  --jobs N             Parallel build/test jobs (default: detected CPU count).
  -h, --help           Show this help.
USAGE
}

bootstrap=0
upgrade=0
frontend=0
clean=0
test_mode=none
use_cache=0
build_plugins=1
install_compiler=0
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 2)"

while (($#)); do
    case "$1" in
        --bootstrap) bootstrap=1 ;;
        --upgrade) upgrade=1 ;;
        --frontend) frontend=1 ;;
        --clean) clean=1 ;;
        --test|--full-tests) test_mode=full ;;
        --smoke-test|--smoke-tests) test_mode=smoke ;;
        --no-tests|--no-test|--build-only) test_mode=none ;;
        --ccache|--sccache) use_cache=1 ;;
        --no-plugins) build_plugins=0 ;;
        --install) install_compiler=1 ;;
        --jobs)
            shift
            [[ $# -gt 0 ]] || { echo "--jobs requires a number" >&2; exit 2; }
            jobs="$1"
            ;;
        --jobs=*) jobs="${1#*=}" ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid job count: $jobs" >&2
    exit 2
fi

if ! command -v pkg >/dev/null 2>&1 || [[ -z "${PREFIX:-}" ]]; then
    echo "build-termux.sh must run inside Termux." >&2
    exit 1
fi

if ((bootstrap)); then
    bootstrap_args=()
    if ((upgrade)); then bootstrap_args+=(--upgrade); fi
    if ((use_cache)); then bootstrap_args+=(--ccache); fi
    bash "$repo_root/scripts/termux/bootstrap-termux.sh" "${bootstrap_args[@]}"
fi

for tool in cmake ninja clang clang++; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Required tool '$tool' was not found. Run: bash build-termux.sh --bootstrap" >&2
        exit 1
    }
done

mode=release
llvm_enabled=ON
if ((frontend)); then
    mode=frontend
    llvm_enabled=OFF
fi
build_dir="$repo_root/.absolute/build/termux-$mode"

if ((clean)); then
    case "$build_dir" in
        "$repo_root/.absolute/build/termux-"*) rm -rf "$build_dir" ;;
        *) echo "Refusing to clean unexpected path: $build_dir" >&2; exit 1 ;;
    esac
fi

llvm_dir=""
if [[ "$llvm_enabled" == ON ]]; then
    command -v llvm-config >/dev/null 2>&1 || {
        echo "llvm-config was not found. Run: bash build-termux.sh --bootstrap" >&2
        exit 1
    }
    llvm_dir="$(llvm-config --cmakedir 2>/dev/null || true)"
    if [[ -z "$llvm_dir" || ! -f "$llvm_dir/LLVMConfig.cmake" ]]; then
        for candidate in "$PREFIX/lib/cmake/llvm" "$PREFIX/lib/llvm/cmake"; do
            if [[ -f "$candidate/LLVMConfig.cmake" ]]; then
                llvm_dir="$candidate"
                break
            fi
        done
    fi
    if [[ -z "$llvm_dir" || ! -f "$llvm_dir/LLVMConfig.cmake" ]]; then
        echo "LLVMConfig.cmake was not found. llvm-config reported: ${llvm_dir:-<empty>}" >&2
        exit 1
    fi
    llvm_major="$(llvm-config --version | cut -d. -f1)"
    if [[ "$llvm_major" =~ ^[0-9]+$ ]] && ((llvm_major < 18)); then
        echo "Absolute requires LLVM 18 or newer; Termux provides $(llvm-config --version)." >&2
        exit 1
    fi
fi

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    -DCMAKE_PREFIX_PATH="$PREFIX"
    -DABSOLUTE_ENABLE_LLVM="$llvm_enabled"
    -DABSOLUTE_BUILD_EXAMPLE_PLUGINS="$([[ $build_plugins -eq 1 ]] && echo ON || echo OFF)"
    -DABSOLUTE_DESKTOP_ENABLE_X11=OFF
    -DABSOLUTE_LLVM_USE_LIBEDIT=OFF
    -DABSOLUTE_LLVM_USE_CURL=OFF
    -DABSOLUTE_USE_COMPILER_CACHE="$([[ $use_cache -eq 1 ]] && echo ON || echo OFF)"
    -DBUILD_TESTING=ON
)
if [[ "$llvm_enabled" == ON ]]; then
    cmake_args+=( -DLLVM_DIR="$llvm_dir" )
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --parallel "$jobs"

compiler=""
for candidate in \
    "$build_dir/Release/absolutec" \
    "$build_dir/absolutec" \
    "$build_dir/Absolute-Compiler/absolutec"; do
    if [[ -f "$candidate" ]]; then
        compiler="$candidate"
        break
    fi
done
if [[ -z "$compiler" ]]; then
    compiler="$(find "$build_dir" -type f -name absolutec -perm -u+x 2>/dev/null | head -n 1 || true)"
fi
if [[ -z "$compiler" ]]; then
    echo "Build completed, but the absolutec executable was not found under $build_dir." >&2
    exit 1
fi

case "$test_mode" in
    full)
        ctest --test-dir "$build_dir" --output-on-failure --parallel "$jobs"
        ;;
    smoke)
        ctest --test-dir "$build_dir" --output-on-failure --parallel "$jobs" \
            -R '^(absolute\.sample|absolute\.build-base-language|absolute\.run-base-language|absolute\.build-native|absolute\.run-native)$'
        ;;
esac

if ((install_compiler)); then
    mkdir -p "$PREFIX/bin"
    ln -sfn "$compiler" "$PREFIX/bin/absolutec"
    echo "Installed compiler symlink: $PREFIX/bin/absolutec"
fi

echo
echo "Absolute Termux build completed."
echo "  Build directory: $build_dir"
echo "  Compiler:        $compiler"
if [[ "$test_mode" == none ]]; then
    echo "  Tests:           skipped (use --smoke-test or --test)"
fi
echo
echo "Run a file:"
echo "  bash run-termux.sh hello.abs"
