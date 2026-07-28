#!/usr/bin/env bash
set -euo pipefail

version="${1:-25.0}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid WASI SDK version: $version" >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
toolchains_root="$repo_root/.absolute/toolchains"
downloads_root="$repo_root/.absolute/downloads"
install_root="$toolchains_root/wasi-sysroot-$version"
builtins_root="$toolchains_root/wasi-builtins-$version"
tag="wasi-sdk-${version%%.*}"

case "$install_root" in
    "$toolchains_root"/*) ;;
    *) echo "Refusing to install outside $toolchains_root" >&2; exit 2 ;;
esac

mkdir -p "$toolchains_root" "$downloads_root"

download() {
    local url="$1"
    local output="$2"
    if [[ -f "$output" ]]; then
        return
    fi
    curl --fail --location --show-error --silent \
        --retry 5 --retry-all-errors --connect-timeout 20 \
        "$url" -o "$output"
}

sysroot_archive="$downloads_root/wasi-sysroot-$version.tar.gz"
sysroot_url="https://github.com/WebAssembly/wasi-sdk/releases/download/$tag/wasi-sysroot-$version.tar.gz"
if [[ ! -f "$install_root/include/wasi/api.h" &&
      ! -f "$install_root/include/wasm32-wasi/wasi/api.h" &&
      ! -f "$install_root/include/wasm32-wasip1/wasi/api.h" &&
      ! -f "$install_root/share/wasi-sysroot/include/wasi/api.h" ]]; then
    download "$sysroot_url" "$sysroot_archive"
    tar -tzf "$sysroot_archive" >/dev/null
    extract_root="$(mktemp -d "$toolchains_root/.wasi-sysroot-$version.XXXXXX")"
    trap 'rm -rf -- "${extract_root:-}"' EXIT
    tar -xzf "$sysroot_archive" -C "$extract_root"
    mapfile -t entries < <(find "$extract_root" -mindepth 1 -maxdepth 1 -type d)
    if [[ "${#entries[@]}" -ne 1 ]]; then
        echo "Expected one wasi-sysroot archive root, found ${#entries[@]}" >&2
        exit 1
    fi
    rm -rf -- "$install_root"
    mv -- "${entries[0]}" "$install_root"
    rm -rf -- "$extract_root"
    trap - EXIT
fi

builtins_name="libclang_rt.builtins-wasm32-wasi-$version.tar.gz"
builtins_archive="$downloads_root/$builtins_name"
builtins_url="https://github.com/WebAssembly/wasi-sdk/releases/download/$tag/$builtins_name"
if ! find "$builtins_root" -type f -name libclang_rt.builtins-wasm32.a \
        -print -quit 2>/dev/null | grep -q .; then
    download "$builtins_url" "$builtins_archive"
    tar -tzf "$builtins_archive" >/dev/null
    rm -rf -- "$builtins_root"
    mkdir -p "$builtins_root"
    tar -xzf "$builtins_archive" -C "$builtins_root"
fi

if [[ -d "$install_root/share/wasi-sysroot" ]]; then
    sysroot="$install_root/share/wasi-sysroot"
else
    sysroot="$install_root"
fi

test -f "$sysroot/include/wasi/api.h" ||
    test -f "$sysroot/include/wasm32-wasi/wasi/api.h" ||
    test -f "$sysroot/include/wasm32-wasip1/wasi/api.h"
test -f "$sysroot/lib/wasm32-wasi/libc.a" ||
    test -f "$sysroot/lib/wasm32-wasip1/libc.a"
find "$builtins_root" -type f -name libclang_rt.builtins-wasm32.a \
    -print -quit | grep -q .

printf 'WASI_SYSROOT=%s\n' "$sysroot"
