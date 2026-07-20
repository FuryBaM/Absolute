[CmdletBinding()]
param(
    [ValidateRange(1, 256)]
    [int]$Jobs = 4,

    [ValidateSet('linux', 'mounted')]
    [string]$BuildLocation = 'linux'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

$suiteRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $suiteRoot '..\..'))
$mountedBuildRoot = Join-Path $suiteRoot '.benchmark-build\compiler'
$resultsRoot = Join-Path $suiteRoot 'results'

function Convert-ToWslPath([string]$Path) {
    $normalized = [IO.Path]::GetFullPath($Path).Replace('\', '/')
    $converted = & wsl.exe wslpath -a $normalized
    if ($LASTEXITCODE -ne 0 -or -not $converted) {
        throw "Could not convert path to WSL format: $Path"
    }
    return ($converted | Select-Object -Last 1).Trim()
}

function Find-WslTool([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        $found = & wsl.exe sh -lc "command -v $candidate" 2>$null
        if ($LASTEXITCODE -eq 0 -and $found) {
            return ($found | Select-Object -Last 1).Trim()
        }
    }
    throw "None of these WSL commands were found: $($Candidates -join ', ')"
}

function Invoke-Wsl([string]$Label, [string[]]$Arguments) {
    Write-Host "[$Label]"
    & wsl.exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE."
    }
}

function Measure-WslBuild([string]$Scenario, [string[]]$Arguments) {
    Write-Host "[Measure: $Scenario]"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & wsl.exe @Arguments
    $exitCode = $LASTEXITCODE
    $timer.Stop()
    if ($exitCode -ne 0) {
        throw "$Scenario failed with exit code $exitCode."
    }
    $script:results.Add([pscustomobject]@{
        Timestamp = [DateTime]::Now.ToString('o', $invariant)
        Scenario = $Scenario
        Seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 6)
        Compiler = $script:compilerDescription
        Generator = $script:generator
        Jobs = $Jobs
        BuildLocation = $BuildLocation
        BuildDirectory = $script:buildRootWsl
    })
}

if (-not (Get-Command 'wsl.exe' -ErrorAction SilentlyContinue)) {
    throw 'wsl.exe was not found.'
}

$wslCmake = Find-WslTool @('cmake')
$wslCompiler = Find-WslTool @('c++', 'g++', 'clang++')
$wslLlvmConfig = Find-WslTool @('llvm-config-18', 'llvm-config')
$llvmDirectory = (& wsl.exe $wslLlvmConfig --cmakedir | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0 -or -not $llvmDirectory) {
    throw 'Could not determine the LLVM CMake directory in WSL.'
}

$compilerLine = & wsl.exe $wslCompiler --version
$compilerDescription = ($compilerLine | Select-Object -First 1).Trim()
$repoRootWsl = Convert-ToWslPath $repoRoot
$buildRootWsl = if ($BuildLocation -eq 'linux') {
    '/root/.cache/absolute/build-benchmark'
} else {
    Convert-ToWslPath $mountedBuildRoot
}
$results = [Collections.Generic.List[object]]::new()

if ($BuildLocation -eq 'linux') {
    if (-not $buildRootWsl.StartsWith('/root/.cache/absolute/')) {
        throw "Refusing to clean unexpected WSL build directory: $buildRootWsl"
    }
    Invoke-Wsl 'Clean dedicated Linux build directory' @($wslCmake, '-E', 'remove_directory', $buildRootWsl)
} else {
    $resolvedBuild = [IO.Path]::GetFullPath($mountedBuildRoot)
    $resolvedSuite = [IO.Path]::GetFullPath($suiteRoot).TrimEnd('\') + '\'
    if (-not $resolvedBuild.StartsWith($resolvedSuite, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean build directory outside benchmark suite: $resolvedBuild"
    }
    if (Test-Path -LiteralPath $resolvedBuild) {
        Write-Host '[Clean dedicated mounted build directory]'
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path $resultsRoot | Out-Null

$zstdHeaderResult = & wsl.exe sh -lc "find /usr/include /usr/local/include /root -type f -name zstd.h -print 2>/dev/null | head -n 1"
$zstdLibraryResult = & wsl.exe sh -lc "find /usr/lib /usr/local/lib /root -name libzstd.so -print 2>/dev/null | head -n 1"
$zstdHeader = if ($zstdHeaderResult) { ($zstdHeaderResult | Select-Object -Last 1).Trim() } else { '' }
$zstdLibrary = if ($zstdLibraryResult) { ($zstdLibraryResult | Select-Object -Last 1).Trim() } else { '' }
$zstdIncludeDirectory = if ($zstdHeader) {
    (& wsl.exe dirname $zstdHeader | Select-Object -Last 1).Trim()
} else { '' }

$configureArguments = @(
    $wslCmake,
    '-S', $repoRootWsl,
    '-B', $buildRootWsl,
    '-DCMAKE_BUILD_TYPE=Release',
    '-DABSOLUTE_ENABLE_LLVM=ON',
    "-DLLVM_DIR=$llvmDirectory"
)
if ($zstdIncludeDirectory -and $zstdLibrary) {
    $configureArguments += "-Dzstd_INCLUDE_DIR=$zstdIncludeDirectory"
    $configureArguments += "-Dzstd_LIBRARY=$zstdLibrary"
}

$configureTimer = [Diagnostics.Stopwatch]::StartNew()
Invoke-Wsl 'Configure clean Release build' $configureArguments
$configureTimer.Stop()

$generator = 'unknown'
$generatorResult = & wsl.exe sh -lc "grep '^CMAKE_GENERATOR:INTERNAL=' '$buildRootWsl/CMakeCache.txt' | head -n 1"
if ($LASTEXITCODE -eq 0 -and $generatorResult) {
    $generator = (($generatorResult | Select-Object -Last 1).Trim() -split '=', 2)[1]
}
$results.Add([pscustomobject]@{
    Timestamp = [DateTime]::Now.ToString('o', $invariant)
    Scenario = 'configure-clean-release'
    Seconds = [Math]::Round($configureTimer.Elapsed.TotalSeconds, 6)
    Compiler = $compilerDescription
    Generator = $generator
    Jobs = $Jobs
    BuildLocation = $BuildLocation
    BuildDirectory = $buildRootWsl
})

$buildArguments = @($wslCmake, '--build', $buildRootWsl, '--target', 'Absolute-Compiler', '--parallel', $Jobs)
Measure-WslBuild 'clean-release' $buildArguments
Measure-WslBuild 'no-op' $buildArguments

$analyzerFile = "$repoRootWsl/Absolute-Analyzer/src/analyzer_values.cpp"
Invoke-Wsl 'Touch one Analyzer unit' @($wslCmake, '-E', 'touch', $analyzerFile)
Measure-WslBuild 'analyzer-unit' $buildArguments

$codegenFile = "$repoRootWsl/Absolute-CodeGen/src/codegen_values.cpp"
Invoke-Wsl 'Touch one CodeGen unit' @($wslCmake, '-E', 'touch', $codegenFile)
Measure-WslBuild 'codegen-unit' $buildArguments

$pchFile = "$repoRootWsl/Absolute-CodeGen/include/codegen_pch.h"
Invoke-Wsl 'Touch CodeGen PCH' @($wslCmake, '-E', 'touch', $pchFile)
Measure-WslBuild 'codegen-pch' $buildArguments

$timestamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss', $invariant)
$resultPath = Join-Path $resultsRoot "build-benchmark-$timestamp.csv"
$results | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding utf8

Write-Host ''
$results | Format-Table Scenario, Seconds, Jobs, Generator, BuildLocation -AutoSize
Write-Host "Results saved to: $resultPath"
