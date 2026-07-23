[CmdletBinding()]
param(
    [string]$Source,
    [string]$Output,
    [switch]$NoRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Join-Path $PSScriptRoot 'window.abs'
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $PSScriptRoot '.desktop-build\absolute-desktop-demo.exe'
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $PSScriptRoot '.desktop-build'
$windowsBuild = Join-Path $buildRoot 'windows'
$nativeOutput = Join-Path $buildRoot 'native'
New-Item -ItemType Directory -Force -Path $windowsBuild, $nativeOutput | Out-Null

function Import-VisualStudioEnvironment {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vsWhere)) {
        throw "Visual Studio Installer was not found: $vsWhere"
    }

    $vsInstall = (& $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -Last 1).Trim()
    if (-not $vsInstall) {
        throw 'Visual Studio C++ x64 build tools were not found.'
    }

    $vcvars64 = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars64)) {
        throw "vcvars64.bat was not found: $vcvars64"
    }

    $batchOutput = & cmd.exe /d /c "call `"$vcvars64`" && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize Microsoft Visual C++ environment using $vcvars64"
    }

    foreach ($line in $batchOutput) {
        if ($line -match '^([^=]+)=(.*)$') {
            $name = $Matches[1]
            $value = $Matches[2]
            [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

function Convert-ToWslPath([string]$Path) {
    $normalized = [IO.Path]::GetFullPath($Path).Replace('\', '/')
    $converted = & wsl.exe wslpath -a $normalized
    if ($LASTEXITCODE -ne 0 -or -not $converted) { throw "Could not convert path to WSL: $Path" }
    return ($converted | Select-Object -Last 1).Trim()
}

function Find-WslTool([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        $found = & wsl.exe sh -lc "command -v $candidate" 2>$null
        if ($LASTEXITCODE -eq 0 -and $found) { return ($found | Select-Object -Last 1).Trim() }
    }
    throw "None of these WSL tools were found: $($Candidates -join ', ')"
}

function Invoke-Step([string]$Label, [string]$Command, [string[]]$Arguments) {
    Write-Host "[$Label]"
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label failed with exit code $LASTEXITCODE" }
}

Import-VisualStudioEnvironment
foreach ($command in 'wsl.exe', 'cmake.exe', 'link.exe') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Required command was not found: $command" }
}
if (-not (Test-Path -LiteralPath $Source)) { throw "Absolute source does not exist: $Source" }

$wslCmake = Find-WslTool @('cmake')
$wslClang = Find-WslTool @('clang++-18', 'clang++')
$wslLlvmConfig = Find-WslTool @('llvm-config-18', 'llvm-config')
$llvmDirectory = (& wsl.exe $wslLlvmConfig --cmakedir | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0 -or -not $llvmDirectory) { throw 'Could not determine LLVM CMake directory in WSL' }
$zstdIncludeResult = & wsl.exe sh -lc 'if [ -f /usr/include/zstd.h ]; then printf "/usr/include\n"; elif [ -f /usr/local/include/zstd.h ]; then printf "/usr/local/include\n"; else ls -d /root/absolute-codex-deps-*/extracted/usr/include 2>/dev/null | head -n 1; fi'
$zstdLibraryResult = & wsl.exe sh -lc 'if [ -f /usr/lib/x86_64-linux-gnu/libzstd.so ]; then printf "/usr/lib/x86_64-linux-gnu/libzstd.so\n"; elif [ -f /usr/local/lib/libzstd.so ]; then printf "/usr/local/lib/libzstd.so\n"; else ls /root/absolute-codex-deps-*/extracted/usr/lib/*/libzstd.so 2>/dev/null | head -n 1; fi'
$zstdInclude = if ($zstdIncludeResult) { ($zstdIncludeResult | Select-Object -Last 1).Trim() } else { '' }
$zstdLibrary = if ($zstdLibraryResult) { ($zstdLibraryResult | Select-Object -Last 1).Trim() } else { '' }

$repoWsl = Convert-ToWslPath $repoRoot
$compilerBuildWsl = '/root/absolute-desktop-compiler'
$configureArguments = @(
    $wslCmake, '-S', $repoWsl, '-B', $compilerBuildWsl,
    '-DCMAKE_BUILD_TYPE=Release', '-DABSOLUTE_ENABLE_LLVM=ON',
    '-DABSOLUTE_DESKTOP_ENABLE_X11=OFF', "-DLLVM_DIR=$llvmDirectory"
)
if ($zstdInclude -and $zstdLibrary) {
    $configureArguments += "-Dzstd_INCLUDE_DIR=$zstdInclude"
    $configureArguments += "-Dzstd_LIBRARY=$zstdLibrary"
}
$compilerCacheExists = & wsl.exe sh -lc "test -f '$compilerBuildWsl/CMakeCache.txt'"
if ($LASTEXITCODE -ne 0) {
    Invoke-Step 'Configure Absolute LLVM compiler' 'wsl.exe' $configureArguments
}
Invoke-Step 'Build Absolute compiler and host plugin' 'wsl.exe' @(
    $wslCmake, '--build', $compilerBuildWsl, '--target',
    'Absolute-Compiler', 'Absolute-Desktop-Plugin', 'Absolute-Math-Plugin', '--parallel'
)

if (-not (Test-Path -LiteralPath (Join-Path $windowsBuild 'CMakeCache.txt'))) {
    Invoke-Step 'Configure Windows desktop runtime' 'cmake.exe' @(
        '-S', $repoRoot, '-B', $windowsBuild,
        '-DABSOLUTE_ENABLE_LLVM=OFF', '-DBUILD_TESTING=OFF',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded'
    )
}
Invoke-Step 'Build Windows desktop runtime' 'cmake.exe' @(
    '--build', $windowsBuild, '--config', 'Release', '--target',
    'Absolute-Runtime', 'Absolute-Desktop-Runtime', '--parallel'
)

$compilerWsl = "$compilerBuildWsl/Release/absolutec"
$hostManifestWsl = "$compilerBuildWsl/plugins/desktop/absolute-desktop.absplugin"
$mathPluginWsl = "$compilerBuildWsl/plugins/math/absolute-math.so"
$llvmIr = Join-Path $nativeOutput 'desktop.ll'
$object = Join-Path $nativeOutput 'desktop.obj'
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
Invoke-Step 'Emit LLVM IR' 'wsl.exe' @(
    $compilerWsl, (Convert-ToWslPath $sourcePath),
    '--plugin', $hostManifestWsl,
    '--plugin', $mathPluginWsl,
    '--emit-llvm', '-o', (Convert-ToWslPath $llvmIr)
)
Invoke-Step 'Compile Windows object' 'wsl.exe' @(
    $wslClang, '--target=x86_64-pc-windows-msvc', '-O2', '-DNDEBUG',
    '-c', (Convert-ToWslPath $llvmIr), '-o', (Convert-ToWslPath $object)
)

$runtimeLibrary = Join-Path $windowsBuild 'Release\Absolute-Runtime.lib'
$desktopLibrary = Join-Path $windowsBuild 'plugins\desktop\Release\absolute-desktop-runtime.lib'
$outputPath = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
Invoke-Step 'Link Windows executable' 'link.exe' @(
    '/nologo', "/out:$outputPath", '/subsystem:console', '/stack:67108864',
    $object, $runtimeLibrary, $desktopLibrary,
    'libcmt.lib', 'libvcruntime.lib', 'libucrt.lib',
    'kernel32.lib', 'user32.lib', 'gdi32.lib'
)

Write-Host "Built: $outputPath"
if (-not $NoRun) {
    Write-Host '[Run desktop application]'
    & $outputPath
    exit $LASTEXITCODE
}
