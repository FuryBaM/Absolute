#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <18|19|20|21> [build-directory]" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 2
fi

llvm_major="$1"
case "$llvm_major" in
    18|19|20|21) ;;
    *)
        echo "Unsupported LLVM compatibility entry: $llvm_major" >&2
        usage
        exit 2
        ;;
esac

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd -- "$script_dir/../.." && pwd)"
build_dir="${2:-${TMPDIR:-/tmp}/absolute-llvm-${llvm_major}-contract}"

llvm_config="${LLVM_CONFIG:-llvm-config-${llvm_major}}"
llvm_as="${LLVM_AS:-llvm-as-${llvm_major}}"
c_compiler="${ABSOLUTE_LLVM_C_COMPILER:-clang-${llvm_major}}"
cxx_compiler="${ABSOLUTE_LLVM_CXX_COMPILER:-clang++-${llvm_major}}"

for command_name in \
    cmake python3 \
    "$llvm_config" "$llvm_as" "$c_compiler" "$cxx_compiler"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required command is unavailable: $command_name" >&2
        exit 1
    fi
done

if command -v ninja >/dev/null 2>&1; then
    cmake_generator="Ninja"
elif command -v make >/dev/null 2>&1; then
    cmake_generator="Unix Makefiles"
else
    echo "Neither ninja nor make is available" >&2
    exit 1
fi

llvm_version="$("$llvm_config" --version)"
actual_major="${llvm_version%%.*}"
if [[ "$actual_major" != "$llvm_major" ]]; then
    echo "Expected LLVM $llvm_major, but $llvm_config reports $llvm_version" >&2
    exit 1
fi

echo "LLVM compatibility contract: requested=$llvm_major actual=$llvm_version"
echo "Build directory: $build_dir"
echo "CMake generator: $cmake_generator"

cmake -S "$source_dir" -B "$build_dir" -G "$cmake_generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(command -v "$c_compiler")" \
    -DCMAKE_CXX_COMPILER="$(command -v "$cxx_compiler")" \
    -DABSOLUTE_NATIVE_CXX_COMPILER="$(command -v "$cxx_compiler")" \
    -DABSOLUTE_EXPECT_LLVM_MAJOR="$llvm_major" \
    -DBUILD_TESTING=ON \
    -DLLVM_DIR="$("$llvm_config" --cmakedir)"

cmake --build "$build_dir" \
    --target Absolute-Compiler \
    --parallel "${ABSOLUTE_CI_JOBS:-2}"

ctest --test-dir "$build_dir" -C Release \
    --output-on-failure --timeout 180 \
    -R '^absolute\.(compiler-version|optimization-differential)$'

compiler=""
for candidate in \
    "$build_dir/Release/absolutec" \
    "$build_dir/absolutec"; do
    if [[ -x "$candidate" ]]; then
        compiler="$candidate"
        break
    fi
done
if [[ -z "$compiler" ]]; then
    echo "Absolute compiler was not produced under $build_dir" >&2
    exit 1
fi

ir_dir="$build_dir/llvm-compatibility-ir"
mkdir -p "$ir_dir"

declare -a ir_cases=(
    "arrays:O0"
    "classes-codegen:O2"
    "interfaces-codegen:O3"
    "structs-codegen:O3"
    "managed-fast-path:O3"
)

for entry in "${ir_cases[@]}"; do
    source_name="${entry%%:*}"
    optimization="${entry##*:}"
    ir_file="$ir_dir/${source_name}-${optimization}.ll"
    bitcode_file="$ir_dir/${source_name}-${optimization}.bc"

    "$compiler" "$source_dir/tests/${source_name}.abs" \
        "-${optimization}" --emit-llvm -o "$ir_file"
    "$llvm_as" "$ir_file" -o "$bitcode_file"
done

echo "llvm-compatibility=ok major=$llvm_major version=$llvm_version"
