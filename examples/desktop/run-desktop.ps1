[CmdletBinding()]
param(
    [string]$Source,
    [string]$Output,
    [switch]$NoRun,
    [ValidateSet('windows', 'wsl')]
    [string]$Backend = 'windows'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Join-Path $PSScriptRoot 'window.abs'
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$windowsBuild = Join-Path $repoRoot '.absolute\build\windows-release'
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repoRoot '.absolute\out\desktop\absolute-desktop-demo.exe'
}

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

function Invoke-Step([string]$Label, [string]$Command, [string[]]$Arguments) {
    Write-Host "[$Label]"
    # Keep tool stdout/stderr on the console; do not let it become function pipeline output.
    & $Command @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "$Label failed with exit code $LASTEXITCODE" }
}

function Resolve-SourcePath([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    $fromExamples = Join-Path $PSScriptRoot $Path
    if (Test-Path -LiteralPath $fromExamples) {
        return (Resolve-Path -LiteralPath $fromExamples).Path
    }
    throw "Absolute source does not exist: $Path"
}

function Find-WindowsCompiler {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:ABSOLUTEC)) {
        $candidates += $env:ABSOLUTEC
    }
    $candidates += @(
        (Join-Path $windowsBuild 'Release\absolutec.exe'),
        (Join-Path $windowsBuild 'absolutec.exe')
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Find-DesktopPlugin {
    $candidates = @(
        (Join-Path $windowsBuild 'plugins\desktop\Release\absolute-desktop.absplugin'),
        (Join-Path $windowsBuild 'plugins\desktop\absolute-desktop.absplugin')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Ensure-WindowsToolchain {
    $compiler = Find-WindowsCompiler
    $plugin = Find-DesktopPlugin
    if ($compiler -and $plugin) {
        return @{ Compiler = $compiler; Plugin = $plugin }
    }

    if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
        throw 'cmake.exe was not found. Install CMake 3.20+ or run build-windows.bat --bootstrap first.'
    }

    $llvmConfig = Get-ChildItem -LiteralPath (Join-Path $repoRoot '.absolute\toolchains') -Recurse -Filter 'LLVMConfig.cmake' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\lib\cmake\llvm\LLVMConfig.cmake' } |
        Select-Object -First 1
    if (-not $llvmConfig) {
        throw @"
Native Absolute compiler/plugin is missing, and no portable LLVM SDK was found under .absolute\toolchains.

Bootstrap and build once from the repository root:

  build-windows.bat --bootstrap -NoTest

Then re-run this desktop example.
"@
    }

    Import-VisualStudioEnvironment
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'MSVC cl.exe was not found after loading Visual Studio environment.'
    }

    $ninja = Join-Path $repoRoot '.absolute\toolchains\ninja\ninja.exe'
    if (-not (Test-Path -LiteralPath $ninja)) {
        $bootstrapNinja = Join-Path $repoRoot 'scripts\windows\bootstrap-ninja.ps1'
        if (Test-Path -LiteralPath $bootstrapNinja) {
            Write-Host '[Bootstrap Ninja]'
            & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $bootstrapNinja
            if ($LASTEXITCODE -ne 0) { throw "Ninja bootstrap failed with exit code $LASTEXITCODE" }
        }
    }
    if (Test-Path -LiteralPath $ninja) {
        $env:Path = ([IO.Path]::GetDirectoryName($ninja)) + ';' + $env:Path
    }

    if (-not (Test-Path -LiteralPath (Join-Path $windowsBuild 'CMakeCache.txt'))) {
        Invoke-Step 'Configure Windows Absolute build' 'cmake.exe' @(
            '-S', $repoRoot, '-B', $windowsBuild, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=cl.exe',
            '-DABSOLUTE_ENABLE_LLVM=ON', '-DABSOLUTE_BUILD_EXAMPLE_PLUGINS=ON'
        )
    }

    Invoke-Step 'Build Absolute compiler and desktop plugin' 'cmake.exe' @(
        '--build', $windowsBuild, '--config', 'Release', '--target',
        'Absolute-Compiler', 'Absolute-Desktop-Plugin', '--parallel'
    )

    $compiler = Find-WindowsCompiler
    $plugin = Find-DesktopPlugin
    if (-not $compiler -or -not $plugin) {
        throw "Build finished, but absolutec or absolute-desktop.absplugin was not found under $windowsBuild"
    }
    return @{ Compiler = $compiler; Plugin = $plugin }
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

function Invoke-WslDesktopBuild {
    Import-VisualStudioEnvironment
    foreach ($command in 'wsl.exe', 'cmake.exe', 'link.exe') {
        if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Required command was not found: $command" }
    }

    $wslProbe = & wsl.exe sh -lc "echo ok" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $wslProbe) {
        throw @"
WSL backend was requested, but no usable Linux distro is available.
Install a distro (for example: wsl --install -d Ubuntu), then install cmake, clang-18/llvm-18, and re-run with -Backend wsl.

Preferred on this machine: use the native Windows path (default), after:

  build-windows.bat --bootstrap -NoTest
"@
    }

    $buildRoot = Join-Path $repoRoot '.absolute\build\desktop-wsl'
    $localWindowsBuild = Join-Path $buildRoot 'windows'
    $nativeOutput = Join-Path $buildRoot 'native'
    New-Item -ItemType Directory -Force -Path $localWindowsBuild, $nativeOutput | Out-Null

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
        '-DABSOLUTE_DESKTOP_ENABLE_X11=OFF', "-DLLVM_DIR=$llvmDirectory",
        '-UABSOLUTE_WASM_LD', '-UABSOLUTE_WASM_LD_FOR_TEST', '-UABSOLUTE_WASM_CLANG'
    )
    if ($zstdInclude -and $zstdLibrary) {
        $configureArguments += "-Dzstd_INCLUDE_DIR=$zstdInclude"
        $configureArguments += "-Dzstd_LIBRARY=$zstdLibrary"
    }

    $compilerConfigureState = & wsl.exe sh -lc @"
if [ ! -f '$compilerBuildWsl/CMakeCache.txt' ]; then
  printf 'missing\n'
elif grep -Eq 'ABSOLUTE_WASM_(LD|CLANG)(_FOR_TEST)?:FILEPATH=.*\.exe' '$compilerBuildWsl/CMakeCache.txt' 2>/dev/null; then
  printf 'stale-pe\n'
else
  printf 'ok\n'
fi
"@
    $compilerConfigureState = if ($compilerConfigureState) { ($compilerConfigureState | Select-Object -Last 1).Trim() } else { 'missing' }
    if ($compilerConfigureState -ne 'ok') {
        if ($compilerConfigureState -eq 'stale-pe') {
            Write-Host 'Reconfiguring Absolute LLVM compiler (clearing cached Windows PE wasm tools)'
        }
        Invoke-Step 'Configure Absolute LLVM compiler' 'wsl.exe' $configureArguments
    }
    Invoke-Step 'Build Absolute compiler and host plugin' 'wsl.exe' @(
        $wslCmake, '--build', $compilerBuildWsl, '--target',
        'Absolute-Compiler', 'Absolute-Desktop-Plugin', 'Absolute-Math-Plugin', '--parallel'
    )

    if (-not (Test-Path -LiteralPath (Join-Path $localWindowsBuild 'CMakeCache.txt'))) {
        Invoke-Step 'Configure Windows desktop runtime' 'cmake.exe' @(
            '-S', $repoRoot, '-B', $localWindowsBuild,
            '-DABSOLUTE_ENABLE_LLVM=OFF', '-DBUILD_TESTING=OFF',
            '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded'
        )
    }
    Invoke-Step 'Build Windows desktop runtime' 'cmake.exe' @(
        '--build', $localWindowsBuild, '--config', 'Release', '--target',
        'Absolute-Runtime', 'Absolute-Desktop-Runtime', '--parallel'
    )

    $sourcePath = Resolve-SourcePath $Source
    $compilerWsl = "$compilerBuildWsl/Release/absolutec"
    $hostManifestWsl = "$compilerBuildWsl/plugins/desktop/absolute-desktop.absplugin"
    $mathPluginWsl = "$compilerBuildWsl/plugins/math/absolute-math.so"
    $llvmIr = Join-Path $nativeOutput 'desktop.ll'
    $object = Join-Path $nativeOutput 'desktop.obj'

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

    $runtimeLibrary = Join-Path $localWindowsBuild 'Release\Absolute-Runtime.lib'
    $desktopLibrary = Join-Path $localWindowsBuild 'plugins\desktop\Release\absolute-desktop-runtime.lib'
    $outputPath = [IO.Path]::GetFullPath($Output)
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
    Invoke-Step 'Link Windows executable' 'link.exe' @(
        '/nologo', "/out:$outputPath", '/subsystem:console', '/stack:67108864',
        $object, $runtimeLibrary, $desktopLibrary,
        'libcmt.lib', 'libvcruntime.lib', 'libucrt.lib',
        'kernel32.lib', 'user32.lib', 'gdi32.lib',
        'ws2_32.lib', 'shell32.lib'
    )
    return $outputPath
}

function Invoke-WindowsDesktopBuild {
    $tools = Ensure-WindowsToolchain
    Import-VisualStudioEnvironment
    if (-not (Get-Command link.exe -ErrorAction SilentlyContinue)) {
        throw 'link.exe was not found after loading Visual Studio environment.'
    }

    $sourcePath = Resolve-SourcePath $Source
    $outputPath = [IO.Path]::GetFullPath($Output)
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($outputPath)) | Out-Null

    Invoke-Step 'Build desktop executable' $tools.Compiler @(
        $sourcePath,
        '--plugin', $tools.Plugin,
        '--build-exe', '-o', $outputPath
    )
    return $outputPath
}

$outputPath = if ($Backend -eq 'wsl') {
    Invoke-WslDesktopBuild
} else {
    Invoke-WindowsDesktopBuild
}

Write-Host "Built: $outputPath"
if (-not $NoRun) {
    Write-Host '[Run desktop application]'
    & $outputPath
    exit $LASTEXITCODE
}
