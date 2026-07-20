[CmdletBinding()]
param(
    [ValidateRange(1, 256)]
    [int]$Jobs = [Environment]::ProcessorCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$suiteRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $suiteRoot '..\..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $suiteRoot '.benchmark-build\windows-compiler'))
$resultsRoot = Join-Path $suiteRoot 'results'

foreach ($command in 'cmake.exe', 'cl.exe') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "$command was not found." }
}
if ($env:VisualStudioVersion -notmatch '^(\d+)\.') { throw 'Visual Studio environment is not initialized.' }
$generator = switch ([int]$Matches[1]) {
    18 { 'Visual Studio 18 2026' }
    17 { 'Visual Studio 17 2022' }
    16 { 'Visual Studio 16 2019' }
    default { throw "Unsupported Visual Studio version $env:VisualStudioVersion" }
}

$allowed = [IO.Path]::GetFullPath((Join-Path $suiteRoot '.benchmark-build')).TrimEnd('\') + '\'
if (-not $buildRoot.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean unexpected build directory: $buildRoot"
}
if (Test-Path -LiteralPath $buildRoot) { Remove-Item -LiteralPath $buildRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $resultsRoot | Out-Null

$compilerDescription = (& cmd.exe /d /c 'cl.exe 2>&1' | Select-Object -First 1).ToString().Trim()
$results = [Collections.Generic.List[object]]::new()
function Add-Result([string]$Scenario, [double]$Seconds) {
    $script:results.Add([pscustomobject]@{
        Timestamp = [DateTime]::Now.ToString('o', $invariant)
        Scenario = $Scenario
        Seconds = [Math]::Round($Seconds, 6)
        Compiler = $compilerDescription
        Generator = $generator
        Jobs = $Jobs
        BuildLocation = 'windows'
        BuildDirectory = $buildRoot
    })
}
function Measure-Command([string]$Scenario, [scriptblock]$Command) {
    Write-Host "[Measure: $Scenario]"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $Command
    $code = $LASTEXITCODE
    $timer.Stop()
    if ($code -ne 0) { throw "$Scenario failed with exit code $code" }
    Add-Result $Scenario $timer.Elapsed.TotalSeconds
}

Measure-Command 'configure-clean-release' {
    & cmake.exe -S $repoRoot -B $buildRoot -G $generator -A x64 `
        -DABSOLUTE_ENABLE_LLVM=ON -DABSOLUTE_BUILD_EXAMPLE_PLUGINS=ON
}
$buildArguments = @('--build', $buildRoot, '--config', 'Release', '--target', 'Absolute-Compiler', '--parallel', $Jobs)
Measure-Command 'clean-release' { & cmake.exe @buildArguments }
Measure-Command 'no-op' { & cmake.exe @buildArguments }

foreach ($entry in @(
    @('analyzer-unit', 'Absolute-Analyzer\src\analyzer_values.cpp'),
    @('codegen-unit', 'Absolute-CodeGen\src\codegen_values.cpp'),
    @('codegen-pch', 'Absolute-CodeGen\include\codegen_pch.h')
)) {
    & cmake.exe -E touch (Join-Path $repoRoot $entry[1])
    if ($LASTEXITCODE -ne 0) { throw "Could not touch $($entry[1])" }
    $scenario = $entry[0]
    Measure-Command $scenario { & cmake.exe @buildArguments }
}

$timestamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss', $invariant)
$resultPath = Join-Path $resultsRoot "build-benchmark-windows-$timestamp.csv"
$results | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding utf8
Write-Host ''
$results | Format-Table Scenario, Seconds, Jobs, Generator, BuildLocation -AutoSize
Write-Host "Results saved to: $resultPath"
