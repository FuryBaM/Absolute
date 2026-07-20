[CmdletBinding()]
param(
    [ValidateSet('release', 'frontend')]
    [string]$Mode = 'release',
    [ValidateSet('msvc', 'clang-cl')]
    [string]$Toolchain = 'msvc',
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$UseCompilerCache,
    [switch]$NoTest,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC environment is not initialized. Run this script through build-windows.bat.'
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'cmake.exe was not found.' }

$llvmEnabled = if ($Mode -eq 'frontend') { 'OFF' } else { 'ON' }
$buildName = if ($Toolchain -eq 'msvc') { "windows-$Mode" } else { "windows-$Toolchain-$Mode" }
if ($UseCompilerCache) { $buildName += '-sccache' }
$buildRoot = Join-Path $repoRoot ".absolute\build\$buildName"
$visualStudioMajor = 0
if ($env:VisualStudioVersion -match '^(\d+)\.') { $visualStudioMajor = [int]$Matches[1] }
$visualStudioGenerator = switch ($visualStudioMajor) {
    18 { 'Visual Studio 18 2026' }
    17 { 'Visual Studio 17 2022' }
    16 { 'Visual Studio 16 2019' }
    default { throw "Unsupported Visual Studio version '$env:VisualStudioVersion'. Use VS 2019 or newer." }
}
if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    $allowed = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\build')).TrimEnd('\') + '\'
    if (-not $buildRoot.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean unexpected directory: $buildRoot"
    }
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}

Push-Location $repoRoot
try {
    $cacheArgument = if ($UseCompilerCache) { '-DABSOLUTE_USE_COMPILER_CACHE=ON' } else { '-DABSOLUTE_USE_COMPILER_CACHE=OFF' }
    if ($UseCompilerCache -and -not (Get-Command sccache.exe -ErrorAction SilentlyContinue)) {
        throw 'The --sccache option was requested, but sccache.exe was not found in PATH.'
    }

    if ($Toolchain -eq 'clang-cl') {
        $llvmRoot = Join-Path $repoRoot '.absolute\toolchains\llvm-18.1.8'
        $clang = Join-Path $llvmRoot 'bin\clang-cl.exe'
        $lld = Join-Path $llvmRoot 'bin\lld-link.exe'
        $ninja = Join-Path $repoRoot '.absolute\toolchains\ninja\ninja.exe'
        foreach ($tool in $clang, $lld, $ninja) {
            if (-not (Test-Path -LiteralPath $tool)) { throw "Required clang-cl tool was not found: $tool" }
        }
        $clangVersionLine = (& $clang --version | Select-Object -First 1)
        if ($LASTEXITCODE -ne 0 -or $clangVersionLine -notmatch 'clang version\s+(\d+)') {
            throw "Could not determine clang-cl version from: $clangVersionLine"
        }
        $clangMajor = [int]$Matches[1]
        if ($visualStudioMajor -ge 18 -and $clangMajor -lt 19) {
            throw "Visual Studio 2026 STL requires Clang 19 or newer, but Absolute's portable LLVM SDK provides clang-cl $clangMajor. Use the default MSVC build on this Visual Studio installation."
        }
        $env:Path = ([IO.Path]::GetDirectoryName($ninja)) + ';' + $env:Path
        & cmake.exe -S $repoRoot -B $buildRoot -G Ninja `
            '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_CXX_COMPILER=$clang" "-DCMAKE_LINKER=$lld" `
            "-DABSOLUTE_ENABLE_LLVM=$llvmEnabled" '-DABSOLUTE_BUILD_EXAMPLE_PLUGINS=ON' $cacheArgument
    }
    elseif ($UseCompilerCache) {
        $ninja = Join-Path $repoRoot '.absolute\toolchains\ninja\ninja.exe'
        if (-not (Test-Path -LiteralPath $ninja)) { throw "Required Ninja executable was not found: $ninja" }
        $env:Path = ([IO.Path]::GetDirectoryName($ninja)) + ';' + $env:Path
        & cmake.exe -S $repoRoot -B $buildRoot -G Ninja `
            '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=cl.exe' `
            "-DABSOLUTE_ENABLE_LLVM=$llvmEnabled" '-DABSOLUTE_BUILD_EXAMPLE_PLUGINS=ON' $cacheArgument
    }
    else {
        & cmake.exe -S $repoRoot -B $buildRoot -G $visualStudioGenerator -A x64 `
            "-DABSOLUTE_ENABLE_LLVM=$llvmEnabled" '-DABSOLUTE_BUILD_EXAMPLE_PLUGINS=ON' $cacheArgument
    }
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
    & cmake.exe --build $buildRoot --config Release --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
    if (-not $NoTest) {
        & ctest.exe --test-dir $buildRoot -C Release --output-on-failure --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE" }
    }
}
finally { Pop-Location }
