#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

usage() {
    cat <<'USAGE'
Usage: bash run-termux.sh [options] <file.abs|project.absproj> [-- program args...]

Options:
  --wasm              Build wasm32-unknown-unknown and run it with Node.js.
  --compiler PATH     Use a specific absolutec executable.
  --output PATH       Override the generated executable/module path.
  --build-only        Compile without launching the result.
  -h, --help          Show this help.
USAGE
}

wasm=0
build_only=0
compiler="${ABSOLUTEC:-}"
output=""
input=""
program_args=()

while (($#)); do
    case "$1" in
        --wasm) wasm=1; shift ;;
        --build-only) build_only=1; shift ;;
        --compiler)
            shift
            [[ $# -gt 0 ]] || { echo "--compiler requires a path" >&2; exit 2; }
            compiler="$1"
            shift
            ;;
        --output)
            shift
            [[ $# -gt 0 ]] || { echo "--output requires a path" >&2; exit 2; }
            output="$1"
            shift
            ;;
        --)
            shift
            program_args=("$@")
            break
            ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)
            if [[ -n "$input" ]]; then
                echo "Only one Absolute input may be supplied before --." >&2
                exit 2
            fi
            input="$1"
            shift
            ;;
    esac
done

[[ -n "$input" ]] || { usage >&2; exit 2; }
input="$(cd "$(dirname "$input")" && pwd)/$(basename "$input")"
[[ -f "$input" ]] || { echo "Absolute input does not exist: $input" >&2; exit 1; }

if [[ -z "$compiler" ]]; then
    candidates=(
        "$repo_root/.absolute/build/termux-release/Release/absolutec"
        "$repo_root/.absolute/build/termux-release/absolutec"
        "$repo_root/.absolute/build/termux-release/Absolute-Compiler/absolutec"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then compiler="$candidate"; break; fi
    done
fi
if [[ -z "$compiler" ]] && command -v absolutec >/dev/null 2>&1; then
    compiler="$(command -v absolutec)"
fi
if [[ -z "$compiler" || ! -x "$compiler" ]]; then
    echo "absolutec was not found. Build it first: bash build-termux.sh --bootstrap" >&2
    exit 1
fi

name="$(basename "$input")"
name="${name%.*}"
output_dir="$(dirname "$input")/.absolute/out"
mkdir -p "$output_dir"
if [[ -z "$output" ]]; then
    output="$output_dir/$name"
    if ((wasm)); then output+=".wasm"; fi
else
    mkdir -p "$(dirname "$output")"
    output="$(cd "$(dirname "$output")" && pwd)/$(basename "$output")"
fi

compiler_args=()
if [[ "$input" == *.absproj ]]; then
    compiler_args+=(build "$input")
else
    compiler_args+=("$input")
fi
if ((wasm)); then
    compiler_args+=(--target wasm32-unknown-unknown)
fi
compiler_args+=(--build-exe -o "$output")

(
    cd "$repo_root"
    "$compiler" "${compiler_args[@]}"
)

if ((build_only)); then
    echo "Built: $output"
    exit 0
fi

if ((wasm)); then
    command -v node >/dev/null 2>&1 || {
        echo "Node.js was not found. Install it with: pkg install nodejs" >&2
        exit 1
    }
    runner="$repo_root/absolute-extension/tools/absolute-wasm-run.js"
    host="$repo_root/tools/absolute-wasm-host.js"
    [[ -f "$runner" && -f "$host" ]] || {
        echo "Absolute WASM runner/host files were not found in the repository." >&2
        exit 1
    }
    node "$runner" "$host" "$output" "${program_args[@]}"
    exit $?
fi

chmod +x "$output"
"$output" "${program_args[@]}"
