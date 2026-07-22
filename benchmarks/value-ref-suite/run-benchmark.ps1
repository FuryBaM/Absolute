[CmdletBinding()]
param(
    [ValidateRange(3, 100)]
    [int]$Samples = 9,

    [ValidateRange(0, 20)]
    [int]$Warmups = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

$suiteRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $suiteRoot '..\..')).Path
$buildRoot = Join-Path $suiteRoot '.benchmark-build'
$resultsRoot = Join-Path $suiteRoot 'results'
New-Item -ItemType Directory -Force -Path $buildRoot, $resultsRoot | Out-Null

$compilerCandidates = @(
    (Join-Path $repoRoot '.absolute\build\windows-release\Release\absolutec.exe'),
    (Join-Path $repoRoot '.absolute\build\msvc-release\Release\absolutec.exe')
)
$compiler = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $compiler) {
    & (Join-Path $repoRoot 'scripts\windows\build-windows.ps1') -Mode release -NoTest
    if ($LASTEXITCODE -ne 0) { throw 'Absolute Release compiler build failed.' }
    $compiler = $compilerCandidates[0]
}

$executables = @{
    'by-value' = Join-Path $buildRoot 'by-value.exe'
    'raw-borrow' = Join-Path $buildRoot 'raw-borrow.exe'
}
foreach ($mode in @('by-value', 'raw-borrow')) {
    $source = Join-Path $suiteRoot "absolute\$mode.abs"
    & $compiler $source --build-exe -o $executables[$mode]
    if ($LASTEXITCODE -ne 0) { throw "Absolute compilation failed for $mode." }
}

function Invoke-Workload([string]$mode) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = & $executables[$mode]
    $watch.Stop()
    if ($LASTEXITCODE -ne 0) { throw "$mode failed with exit code $LASTEXITCODE." }
    return [pscustomobject]@{
        Seconds = $watch.Elapsed.TotalSeconds
        Checksum = ($output | Select-Object -Last 1).ToString().Trim()
    }
}

for ($warmup = 0; $warmup -lt $Warmups; ++$warmup) {
    Invoke-Workload 'by-value' | Out-Null
    Invoke-Workload 'raw-borrow' | Out-Null
}

$measurements = @{'by-value' = @(); 'raw-borrow' = @()}
$expectedChecksum = $null
for ($sample = 0; $sample -lt $Samples; ++$sample) {
    $order = if (($sample % 2) -eq 0) { @('by-value', 'raw-borrow') } else { @('raw-borrow', 'by-value') }
    foreach ($mode in $order) {
        $result = Invoke-Workload $mode
        if ($null -eq $expectedChecksum) { $expectedChecksum = $result.Checksum }
        if ($result.Checksum -ne $expectedChecksum) {
            throw "Checksum mismatch for ${mode}: expected $expectedChecksum, got $($result.Checksum)."
        }
        $measurements[$mode] += $result.Seconds
    }
}

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return [double]$sorted[$middle] }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

$copyMedian = Get-Median $measurements['by-value']
$borrowMedian = Get-Median $measurements['raw-borrow']
$speedup = $copyMedian / $borrowMedian
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$csvPath = Join-Path $resultsRoot "value-ref-$timestamp.csv"
$rows = foreach ($mode in @('by-value', 'raw-borrow')) {
    $values = [double[]]$measurements[$mode]
    [pscustomobject]@{
        mode = $mode
        aggregate_bytes = 128
        calls = 20000000
        samples = $Samples
        median_seconds = (Get-Median $values).ToString('F6', $invariant)
        min_seconds = (($values | Measure-Object -Minimum).Minimum).ToString('F6', $invariant)
        max_seconds = (($values | Measure-Object -Maximum).Maximum).ToString('F6', $invariant)
        checksum = $expectedChecksum
    }
}
$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding utf8

Write-Host "by-value median : $($copyMedian.ToString('F6', $invariant)) s"
Write-Host "raw borrow median: $($borrowMedian.ToString('F6', $invariant)) s"
Write-Host "borrow speedup   : $($speedup.ToString('F2', $invariant))x"
Write-Host "results          : $csvPath"
