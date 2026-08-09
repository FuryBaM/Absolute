[CmdletBinding()]
param(
    [ValidateRange(1, 100)]
    [int]$Samples = 11,

    [ValidateRange(0, 20)]
    [int]$Warmups = 2,

    [ValidateSet(0, 1)]
    [int]$IncludePython = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

$suiteRoot     = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$repoRoot      = (Resolve-Path -LiteralPath (Join-Path $suiteRoot '..\..') ).Path
$buildRoot     = Join-Path $suiteRoot '.benchmark-build'
$nativeOut     = Join-Path $buildRoot 'native'
$javaOut       = Join-Path $buildRoot 'java'
$dotnetOut     = Join-Path $buildRoot 'dotnet'
$resultsRoot   = Join-Path $suiteRoot 'results'
$llvmBin       = Join-Path $repoRoot '.absolute\toolchains\llvm-18.1.8\bin'
$absoluteCompiler = Join-Path $repoRoot '.absolute\build\windows-release\Release\absolutec.exe'
$absoluteRuntime  = Join-Path $repoRoot '.absolute\build\windows-release\Absolute-Runtime.lib'

foreach ($dir in @($buildRoot, $nativeOut, $javaOut, $dotnetOut, $resultsRoot)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

function Require-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Required command '$name' was not found in PATH."
    }
}

function Invoke-External([string]$label, [string]$file, [string[]]$arguments) {
    Write-Host "[$label]"
    & $file @arguments
    if ($LASTEXITCODE -ne 0) { throw "$label failed with exit code $LASTEXITCODE." }
}

Require-Command 'link.exe'
Require-Command 'cl.exe'
Require-Command 'dotnet.exe'
Require-Command 'javac.exe'
Require-Command 'java.exe'
Require-Command 'node.exe'
if ($IncludePython -eq 1) { Require-Command 'python.exe' }

if (-not (Test-Path -LiteralPath $absoluteCompiler)) {
    throw "Absolute compiler not found: $absoluteCompiler. Run build-windows.bat first."
}
if (-not (Test-Path -LiteralPath $absoluteRuntime)) {
    throw "Absolute runtime not found: $absoluteRuntime. Run build-windows.bat first."
}

$algorithms = @('vector-push-sum', 'vector-sort', 'hashmap-insert-lookup')
$expected = @{
    'vector-push-sum'       = '8499995000000'
    'vector-sort'           = '43156608257'
    'hashmap-insert-lookup' = '260000100000'
}

# ---- Compile Absolute benchmarks ----
Write-Host '[Compile Absolute benchmarks — emit LLVM IR]'
$clangExe = Join-Path $llvmBin 'clang++.exe'
$lldExe   = Join-Path $llvmBin 'lld-link.exe'

# absolutec resolves 'std/...' imports relative to its CWD, so run from repoRoot.
Push-Location $repoRoot
try {
    foreach ($algorithm in $algorithms) {
        $source     = Join-Path $suiteRoot "absolute\$algorithm.abs"
        $llvmIr     = Join-Path $nativeOut  "absolute-$algorithm.ll"

        & $absoluteCompiler $source --emit-llvm -o $llvmIr
        if ($LASTEXITCODE -ne 0) { throw "Absolute LLVM IR emission failed for $algorithm." }
    }
    $unsafeSortSource = Join-Path $suiteRoot 'absolute\vector-sort-unsafe.abs'
    $unsafeSortIr = Join-Path $nativeOut 'absolute-vector-sort-unsafe.ll'
    & $absoluteCompiler $unsafeSortSource --emit-llvm -o $unsafeSortIr
    if ($LASTEXITCODE -ne 0) { throw 'Absolute LLVM IR emission failed for vector-sort-unsafe.' }
}
finally {
    Pop-Location
}

foreach ($algorithm in $algorithms) {
    $llvmIr     = Join-Path $nativeOut  "absolute-$algorithm.ll"
    $object     = Join-Path $nativeOut  "absolute-$algorithm.obj"
    $executable = Join-Path $nativeOut  "absolute-$algorithm.exe"

    cmd /c "`"$clangExe`" --target=x86_64-pc-windows-msvc -O3 -march=native -DNDEBUG -c `"$llvmIr`" -o `"$object`" 2>nul"
    if ($LASTEXITCODE -ne 0) { throw "Clang optimization failed for $algorithm." }

    $linkArgs = @(
        '/nologo',
        "/out:$executable",
        '/subsystem:console',
        '/stack:67108864',
        $object,
        $absoluteRuntime,
        'libcmt.lib',
        'libvcruntime.lib',
        'libucrt.lib',
        'kernel32.lib'
    )
    & link.exe @linkArgs 2>&1 | Where-Object { $_ -notmatch 'LNK(4098|4217)' } | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "Linker failed for $algorithm." }
}

$unsafeSortIr = Join-Path $nativeOut 'absolute-vector-sort-unsafe.ll'
$unsafeSortObject = Join-Path $nativeOut 'absolute-vector-sort-unsafe.obj'
$unsafeSortExecutable = Join-Path $nativeOut 'absolute-vector-sort-unsafe.exe'
cmd /c "`"$clangExe`" --target=x86_64-pc-windows-msvc -O3 -march=native -DNDEBUG -c `"$unsafeSortIr`" -o `"$unsafeSortObject`" 2>nul"
if ($LASTEXITCODE -ne 0) { throw 'Clang optimization failed for vector-sort-unsafe.' }
$unsafeLinkArgs = @(
    '/nologo',
    "/out:$unsafeSortExecutable",
    '/subsystem:console',
    '/stack:67108864',
    $unsafeSortObject,
    $absoluteRuntime,
    'libcmt.lib',
    'libvcruntime.lib',
    'libucrt.lib',
    'kernel32.lib'
)
& link.exe @unsafeLinkArgs 2>&1 | Where-Object { $_ -notmatch 'LNK(4098|4217)' } | Write-Host
if ($LASTEXITCODE -ne 0) { throw 'Linker failed for vector-sort-unsafe.' }

# ---- Compile C++ benchmark ----
Write-Host '[Compile C++ benchmark with MSVC]'
$cppExecutable = Join-Path $nativeOut 'benchmark-cpp.exe'
& cl.exe /nologo /std:c++20 /O2 /Ob3 /GL /DNDEBUG `
    (Join-Path $suiteRoot 'benchmark.cpp') "/Fe:$cppExecutable" /link /LTCG /stack:67108864
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }

# ---- Compile C# benchmark ----
Invoke-External 'Compile C# Release benchmark' 'dotnet.exe' @(
    'build',
    (Join-Path $suiteRoot 'Benchmark.csproj'),
    '-c', 'Release',
    '--nologo'
)
$dotnetExecutable = Get-ChildItem (Join-Path $suiteRoot 'bin') -Recurse -Filter 'Benchmark.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'Release' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $dotnetExecutable) {
    throw "Could not find Benchmark.exe after C# build. Expected under $(Join-Path $suiteRoot 'bin')."
}

# ---- Compile Java benchmark ----
Invoke-External 'Compile Java benchmark' 'javac.exe' @(
    '-d', $javaOut,
    (Join-Path $suiteRoot 'Benchmark.java')
)

$javascriptSource = Join-Path $suiteRoot 'benchmark.js'
$pythonSource     = Join-Path $suiteRoot 'benchmark.py'
$optimizedLanguages = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')

function Measure-Benchmark([string]$language, [string]$algorithm) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    switch ($language) {
        'Absolute'   { $output = & (Join-Path $nativeOut "absolute-$algorithm.exe") }
        'Absolute unchecked' { $output = & $unsafeSortExecutable }
        'C++'        { $output = & $cppExecutable $algorithm }
        'C#'         { $output = & $dotnetExecutable $algorithm }
        'Java'       { $output = & java.exe -cp $javaOut Benchmark $algorithm }
        'JavaScript' { $output = & node.exe $javascriptSource $algorithm }
        'Python'     { $output = & python.exe $pythonSource $algorithm }
        default      { throw "Unknown benchmark language: $language" }
    }
    $stopwatch.Stop()
    if ($LASTEXITCODE -ne 0) {
        throw "$language failed on $algorithm with exit code $LASTEXITCODE."
    }
    $actual = ($output | Select-Object -Last 1).ToString().Trim()
    if ($actual -ne $expected[$algorithm]) {
        throw "Checksum mismatch for $language/$algorithm`: expected $($expected[$algorithm]), got $actual."
    }
    return $stopwatch.Elapsed.TotalSeconds
}

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return [double]$sorted[$middle] }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

$measurements = @{}
foreach ($algorithm in $algorithms) {
    foreach ($language in $optimizedLanguages) {
        $measurements["$algorithm|$language"] = [System.Collections.Generic.List[double]]::new()
    }

    if ($Warmups -gt 0) {
        Write-Host "[Warm up: $algorithm]"
        foreach ($language in $optimizedLanguages) {
            for ($round = 0; $round -lt $Warmups; ++$round) {
                [void](Measure-Benchmark $language $algorithm)
            }
        }
    }

    Write-Host "[Measure: $algorithm, $Samples samples per optimized language]"
    for ($round = 0; $round -lt $Samples; ++$round) {
        for ($offset = 0; $offset -lt $optimizedLanguages.Count; ++$offset) {
            $language = $optimizedLanguages[($round + $offset) % $optimizedLanguages.Count]
            $measurements["$algorithm|$language"].Add((Measure-Benchmark $language $algorithm))
        }
    }
}

$unsafeMeasurements = [System.Collections.Generic.List[double]]::new()
if ($Warmups -gt 0) {
    Write-Host '[Warm up: vector-sort, Absolute unchecked]'
    for ($round = 0; $round -lt $Warmups; ++$round) {
        [void](Measure-Benchmark 'Absolute unchecked' 'vector-sort')
    }
}
Write-Host "[Measure: vector-sort, $Samples samples for Absolute unchecked]"
for ($round = 0; $round -lt $Samples; ++$round) {
    $unsafeMeasurements.Add(
        (Measure-Benchmark 'Absolute unchecked' 'vector-sort'))
}

if ($IncludePython -eq 1) {
    Write-Host '[Measure Python: one sample per algorithm]'
    foreach ($algorithm in $algorithms) {
        $list = [System.Collections.Generic.List[double]]::new()
        $list.Add((Measure-Benchmark 'Python' $algorithm))
        $measurements["$algorithm|Python"] = $list
    }
}

$allLanguages = @($optimizedLanguages)
if ($IncludePython -eq 1) { $allLanguages += 'Python' }
$rows = @()
foreach ($algorithm in $algorithms) {
    foreach ($language in $allLanguages) {
        [double[]]$values = $measurements["$algorithm|$language"].ToArray()
        $rows += [pscustomobject]@{
            Algorithm   = $algorithm
            Language    = $language
            Samples     = $values.Count
            MedianSeconds = (Get-Median $values).ToString('F6', $invariant)
            MinSeconds  = ($values | Measure-Object -Minimum).Minimum.ToString('F6', $invariant)
            MaxSeconds  = ($values | Measure-Object -Maximum).Maximum.ToString('F6', $invariant)
            Checksum    = $expected[$algorithm]
        }
    }
}
$unsafeValues = $unsafeMeasurements.ToArray()
$rows += [pscustomobject]@{
    Algorithm     = 'vector-sort'
    Language      = 'Absolute unchecked'
    Samples       = $unsafeValues.Count
    MedianSeconds = (Get-Median $unsafeValues).ToString('F6', $invariant)
    MinSeconds    = ($unsafeValues | Measure-Object -Minimum).Minimum.ToString('F6', $invariant)
    MaxSeconds    = ($unsafeValues | Measure-Object -Maximum).Maximum.ToString('F6', $invariant)
    Checksum      = $expected['vector-sort']
}

Write-Host ''
$rows | Format-Table Algorithm, Language, Samples, MedianSeconds, MinSeconds, MaxSeconds -AutoSize

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$resultPath = Join-Path $resultsRoot "collection-benchmark-$timestamp.csv"
$rows | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding UTF8
Write-Host "Results saved to: $resultPath"
