[CmdletBinding()]
param(
    [string]$Version = '25.0',
    [string]$InstallRoot,
    [switch]$Force
)

# Downloads the portable wasi-sysroot (headers + libc.a) — not the full multi-hundred-MB wasi-sdk.
# Set WASI_SYSROOT / WASI_SDK_PATH after install for optional extra C objects.

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$toolchainsRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\toolchains'))
$downloadsRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\downloads'))
if (-not $InstallRoot) { $InstallRoot = Join-Path $toolchainsRoot "wasi-sysroot-$Version" }
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)

function Assert-Under([string]$Path, [string]$Parent) {
    $prefix = $Parent.TrimEnd('\') + '\'
    if (-not $Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside '$Parent': $Path"
    }
}

Assert-Under $InstallRoot $toolchainsRoot
$marker = Join-Path $InstallRoot 'share\wasi-sysroot\include\wasi\api.h'
if (-not (Test-Path -LiteralPath $marker)) {
    $marker = Join-Path $InstallRoot 'include\wasi\api.h'
}
if ((Test-Path -LiteralPath $marker) -and -not $Force) {
    Write-Host "wasi-sysroot $Version already installed: $InstallRoot"
    Write-Output $InstallRoot
    exit 0
}

foreach ($command in 'curl.exe', 'tar.exe') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command is required to bootstrap wasi-sysroot."
    }
}

New-Item -ItemType Directory -Force -Path $toolchainsRoot, $downloadsRoot | Out-Null
$tag = "wasi-sdk-$($Version.Split('.')[0])"
$archiveName = "wasi-sysroot-$Version.tar.gz"
$archive = Join-Path $downloadsRoot $archiveName
$url = "https://github.com/WebAssembly/wasi-sdk/releases/download/$tag/$archiveName"

if (-not (Test-Path -LiteralPath $archive) -or $Force) {
    Write-Host "Downloading $url"
    & curl.exe -fsSL -o $archive $url
    if ($LASTEXITCODE -ne 0) { throw "curl failed for $url" }
}

$extractRoot = Join-Path $downloadsRoot "wasi-sysroot-$Version-extract"
if (Test-Path -LiteralPath $extractRoot) {
    Remove-Item -LiteralPath $extractRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
Write-Host "Extracting $archive"
& tar.exe -xzf $archive -C $extractRoot
if ($LASTEXITCODE -ne 0) { throw "tar extract failed" }

# Tarball usually contains a single top-level dir (wasi-sysroot or similar).
$inner = Get-ChildItem -LiteralPath $extractRoot | Select-Object -First 1
if (-not $inner) { throw "empty archive extract" }

if (Test-Path -LiteralPath $InstallRoot) {
    Remove-Item -LiteralPath $InstallRoot -Recurse -Force
}
Move-Item -LiteralPath $inner.FullName -Destination $InstallRoot
Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue

# Normalize: expose WASI_SYSROOT as the directory that contains include/ + lib/
$candidates = @(
    (Join-Path $InstallRoot 'share\wasi-sysroot'),
    $InstallRoot
)
$sysroot = $null
foreach ($c in $candidates) {
    if ((Test-Path (Join-Path $c 'include')) -or (Test-Path (Join-Path $c 'lib'))) {
        $sysroot = $c
        break
    }
}
if (-not $sysroot) { $sysroot = $InstallRoot }

$props = Join-Path $InstallRoot 'absolute-wasi-sysroot.json'
@{
    version = $Version
    installRoot = $InstallRoot
    sysroot = $sysroot
    tag = $tag
} | ConvertTo-Json | Set-Content -Path $props -Encoding UTF8

Write-Host "wasi-sysroot installed:"
Write-Host "  install: $InstallRoot"
Write-Host "  sysroot: $sysroot"
Write-Host "Set: `$env:WASI_SYSROOT = '$sysroot'"
Write-Output $sysroot
