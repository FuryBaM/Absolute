[CmdletBinding()]
param(
    [string]$Version = '18.1.8',
    [string]$InstallRoot,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$toolchainsRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\toolchains'))
$downloadsRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\downloads'))
if (-not $InstallRoot) { $InstallRoot = Join-Path $toolchainsRoot "llvm-$Version" }
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)

function Assert-Under([string]$Path, [string]$Parent) {
    $prefix = $Parent.TrimEnd('\') + '\'
    if (-not $Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside '$Parent': $Path"
    }
}

Assert-Under $InstallRoot $toolchainsRoot
$config = Join-Path $InstallRoot 'lib\cmake\llvm\LLVMConfig.cmake'
$clang = Join-Path $InstallRoot 'bin\clang-cl.exe'
if ((Test-Path -LiteralPath $config) -and (Test-Path -LiteralPath $clang) -and -not $Force) {
    Write-Host "LLVM SDK $Version is already installed: $InstallRoot"
    Write-Output $InstallRoot
    exit 0
}

foreach ($command in 'curl.exe', 'tar.exe') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command is required to bootstrap the portable LLVM SDK."
    }
}

New-Item -ItemType Directory -Force -Path $toolchainsRoot, $downloadsRoot | Out-Null
$archiveName = "clang+llvm-$Version-x86_64-pc-windows-msvc.tar.xz"
$archive = Join-Path $downloadsRoot $archiveName
$urlName = $archiveName.Replace('+', '%2B')
$url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$Version/$urlName"

if (-not (Test-Path -LiteralPath $archive)) {
    Write-Host "Downloading LLVM SDK $Version (about 1 GB)..."
    & curl.exe --fail --location --retry 3 --continue-at - --output $archive $url
    if ($LASTEXITCODE -ne 0) { throw "LLVM download failed with exit code $LASTEXITCODE" }
}

$extractRoot = Join-Path $toolchainsRoot ".llvm-$Version.extracting"
Assert-Under $extractRoot $toolchainsRoot
if (Test-Path -LiteralPath $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
New-Item -ItemType Directory -Path $extractRoot | Out-Null

Write-Host 'Extracting LLVM SDK...'
& tar.exe -xf $archive -C $extractRoot
if ($LASTEXITCODE -ne 0) { throw "LLVM extraction failed with exit code $LASTEXITCODE" }

$discoveredConfig = Get-ChildItem -LiteralPath $extractRoot -Recurse -Filter LLVMConfig.cmake |
    Where-Object FullName -like '*\lib\cmake\llvm\LLVMConfig.cmake' |
    Select-Object -First 1
if (-not $discoveredConfig) { throw "Extracted archive does not contain LLVMConfig.cmake: $archive" }
$sdkRoot = $discoveredConfig.Directory.Parent.Parent.Parent.FullName

if (Test-Path -LiteralPath $InstallRoot) {
    Assert-Under $InstallRoot $toolchainsRoot
    Remove-Item -LiteralPath $InstallRoot -Recurse -Force
}
Move-Item -LiteralPath $sdkRoot -Destination $InstallRoot
if (Test-Path -LiteralPath $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }

if (-not (Test-Path -LiteralPath $config) -or -not (Test-Path -LiteralPath $clang)) {
    throw "LLVM SDK verification failed after extraction: $InstallRoot"
}
Write-Host "Installed portable LLVM SDK ${Version}: $InstallRoot"
Write-Output $InstallRoot
