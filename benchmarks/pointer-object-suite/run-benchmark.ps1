[CmdletBinding()]
param(
    [ValidateRange(1, 100)]
    [int]$Samples = 10,

    [ValidateRange(0, 20)]
    [int]$Warmups = 2,

    [ValidateSet(0, 1)]
    [int]$IncludePython = 1,

    [ValidateSet('windows', 'wsl')]
    [string]$Backend = 'windows'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

$suiteRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $suiteRoot '..\..')).Path
$buildRoot = Join-Path $suiteRoot '.benchmark-build'
$nativeOut = Join-Path $buildRoot 'native'
$javaOut = Join-Path $buildRoot 'java'
$dotnetOut = Join-Path $buildRoot 'dotnet'
$dotnetBin = Join-Path $buildRoot 'dotnet-bin'
$dotnetObj = Join-Path $buildRoot 'dotnet-obj'
$localCompilerBuild = Join-Path $buildRoot 'compiler'
$arrayCompilerBuild = Join-Path $repoRoot 'benchmarks\array-suite\.benchmark-build\compiler'
$compilerBuild = if ($Backend -eq 'windows' -and
    (Test-Path -LiteralPath (Join-Path $arrayCompilerBuild 'CMakeCache.txt'))) {
    $arrayCompilerBuild
} else {
    $localCompilerBuild
}
$resultsRoot = Join-Path $suiteRoot 'results'

foreach ($directory in @($buildRoot, $nativeOut, $javaOut, $dotnetOut, $dotnetBin, $dotnetObj, $compilerBuild, $resultsRoot)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

function Require-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Required command '$name' was not found in PATH."
    }
}

function Invoke-External([string]$label, [string]$file, [string[]]$arguments) {
    Write-Host "[$label]"
    & $file @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$label failed with exit code $LASTEXITCODE."
    }
}

function Convert-ToWslPath([string]$path) {
    $normalized = [IO.Path]::GetFullPath($path).Replace('\', '/')
    $converted = & wsl.exe wslpath -a $normalized
    if ($LASTEXITCODE -ne 0 -or -not $converted) {
        throw "Could not convert path to WSL format: $path"
    }
    return ($converted | Select-Object -Last 1).Trim()
}

function Find-WslTool([string[]]$candidates) {
    foreach ($candidate in $candidates) {
        $found = & wsl.exe sh -lc "command -v $candidate" 2>$null
        if ($LASTEXITCODE -eq 0 -and $found) {
            return ($found | Select-Object -Last 1).Trim()
        }
    }
    throw "None of these WSL commands were found: $($candidates -join ', ')"
}

function Link-NativeExecutable(
    [string]$objectPath,
    [string]$executablePath,
    [string[]]$additionalObjects = @()
) {
    [string[]]$arguments = @(
        '/nologo',
        "/out:$executablePath",
        '/subsystem:console',
        '/stack:67108864',
        $objectPath
    ) + $additionalObjects + @(
        'libcmt.lib',
        'libvcruntime.lib',
        'libucrt.lib',
        'kernel32.lib'
    )
    & link.exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Native linker failed for $objectPath with exit code $LASTEXITCODE."
    }
}

Require-Command 'cl.exe'
Require-Command 'link.exe'
Require-Command 'dotnet.exe'
Require-Command 'javac.exe'
Require-Command 'java.exe'
Require-Command 'node.exe'
if ($IncludePython -eq 1) { Require-Command 'python.exe' }

$algorithms = @('managed-deref', 'heap-nodes', 'arena-graph', 'object-tree')
$absoluteAlgorithms = @('managed-deref', 'heap-nodes', 'arena-graph', 'object-tree')
$expected = @{
    'managed-deref' = '2007139840'
    'heap-nodes' = '765920640'
    'arena-graph' = '1147923655'
    'object-tree' = '1238767148'
}
$workUnits = @{
    'managed-deref' = 50000000
    'heap-nodes' = 20000000
    'arena-graph' = 5000000
    'object-tree' = 26214200
}
$unitNames = @{
    'managed-deref' = 'read-modify-write iterations'
    'heap-nodes' = 'random node mutations'
    'arena-graph' = 'graph link traversals'
    'object-tree' = 'virtual node visits'
}
$optimizedByAlgorithm = @{
    'managed-deref' = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')
    'heap-nodes' = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')
    'arena-graph' = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')
    'object-tree' = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')
}

$cppExecutable = Join-Path $nativeOut 'benchmark-cpp.exe'
if ($Backend -eq 'windows') {
    & (Join-Path $repoRoot 'scripts\windows\build-windows.ps1') -Mode release -NoTest
    $absoluteCompiler = Join-Path $repoRoot '.absolute\build\windows-release\Release\absolutec.exe'
    if (-not (Test-Path -LiteralPath $absoluteCompiler)) {
        throw "Absolute compiler was not produced at $absoluteCompiler"
    }
    Write-Host '[Compile Absolute benchmarks natively]'
    foreach ($algorithm in $absoluteAlgorithms) {
        $source = Join-Path $suiteRoot "absolute\$algorithm.abs"
        $executable = Join-Path $nativeOut "absolute-$algorithm.exe"
        & $absoluteCompiler $source --build-exe -o $executable
        if ($LASTEXITCODE -ne 0) { throw "Absolute compilation failed for $algorithm." }
    }
    Write-Host '[Compile C++ benchmark with MSVC]'
    & cl.exe /nologo /std:c++20 /O2 /Ob3 /GL /DNDEBUG `
        (Join-Path $suiteRoot 'benchmark.cpp') "/Fe:$cppExecutable" /link /LTCG /stack:67108864
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
}
else {
    Require-Command 'wsl.exe'
    $wslCmake = Find-WslTool @('cmake')
    $wslClang = Find-WslTool @('clang++-18', 'clang++')
    $wslLlvmConfig = Find-WslTool @('llvm-config-18', 'llvm-config')
    $llvmDirectory = (& wsl.exe $wslLlvmConfig --cmakedir | Select-Object -Last 1).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $llvmDirectory) { throw 'Could not determine LLVM CMake directory in WSL.' }
    $repoWsl = Convert-ToWslPath $repoRoot
    $compilerBuildWsl = Convert-ToWslPath $compilerBuild
    Invoke-External 'Configure Absolute Release compiler' 'wsl.exe' @(
        $wslCmake, '-S', $repoWsl, '-B', $compilerBuildWsl,
        '-DCMAKE_BUILD_TYPE=Release', '-DABSOLUTE_ENABLE_LLVM=ON', "-DLLVM_DIR=$llvmDirectory"
    )
    Invoke-External 'Build Absolute Release compiler' 'wsl.exe' @(
        $wslCmake, '--build', $compilerBuildWsl, '--parallel',
        '--target', 'Absolute-Compiler'
    )
    $absoluteCompiler = Join-Path $compilerBuild 'Release\absolutec'
    $absoluteCompilerWsl = Convert-ToWslPath $absoluteCompiler
    Write-Host '[Compile Absolute pointer runtime for Windows]'
    $runtimeObject = Join-Path $nativeOut 'absolute-pointer-runtime.obj'
    & cl.exe /nologo /O2 /MT /EHsc /std:c++20 /c `
        (Join-Path $repoRoot 'Absolute-Runtime\src\pointers.cpp') "/Fo$runtimeObject"
    if ($LASTEXITCODE -ne 0) { throw 'Absolute pointer runtime compilation failed.' }
    Write-Host '[Compile Absolute benchmarks through WSL]'
    foreach ($algorithm in $absoluteAlgorithms) {
        $source = Join-Path $suiteRoot "absolute\$algorithm.abs"
        $llvmIr = Join-Path $nativeOut "absolute-$algorithm.ll"
        $object = Join-Path $nativeOut "absolute-$algorithm.obj"
        $executable = Join-Path $nativeOut "absolute-$algorithm.exe"
        & wsl.exe $absoluteCompilerWsl (Convert-ToWslPath $source) --emit-llvm -o (Convert-ToWslPath $llvmIr)
        if ($LASTEXITCODE -ne 0) { throw "Absolute compilation failed for $algorithm." }
        & wsl.exe $wslClang --target=x86_64-pc-windows-msvc -O3 -march=native -DNDEBUG `
            -c (Convert-ToWslPath $llvmIr) -o (Convert-ToWslPath $object)
        if ($LASTEXITCODE -ne 0) { throw "LLVM optimization failed for $algorithm." }
        if ($algorithm -eq 'managed-deref') { Link-NativeExecutable $object $executable @($runtimeObject) }
        else { Link-NativeExecutable $object $executable }
    }
    Write-Host '[Compile C++ benchmark through WSL]'
    $cppObject = Join-Path $nativeOut 'benchmark-cpp.obj'
    & wsl.exe $wslClang --target=x86_64-pc-windows-msvc -O3 -march=native -DNDEBUG `
        -fno-exceptions -fno-rtti -c (Convert-ToWslPath (Join-Path $suiteRoot 'benchmark.cpp')) `
        -o (Convert-ToWslPath $cppObject)
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    Link-NativeExecutable $cppObject $cppExecutable
}

Invoke-External 'Compile C# Release benchmark' 'dotnet.exe' @(
    'publish',
    (Join-Path $suiteRoot 'Benchmark.csproj'),
    '-c', 'Release',
    '-o', $dotnetOut,
    '--nologo',
    "-p:BaseOutputPath=$dotnetBin\",
    "-p:BaseIntermediateOutputPath=$dotnetObj\"
)

Invoke-External 'Compile Java benchmark' 'javac.exe' @(
    '-d', $javaOut,
    (Join-Path $suiteRoot 'Benchmark.java')
)

$dotnetExecutable = Join-Path $dotnetOut 'Benchmark.exe'
$javascriptSource = Join-Path $suiteRoot 'benchmark.js'
$pythonSource = Join-Path $suiteRoot 'benchmark.py'

function Measure-Benchmark([string]$language, [string]$algorithm) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    switch ($language) {
        'Absolute' { $output = & (Join-Path $nativeOut "absolute-$algorithm.exe") }
        'C++' { $output = & $cppExecutable $algorithm }
        'C#' { $output = & $dotnetExecutable $algorithm }
        'Java' { $output = & java.exe -Xms512m -Xmx512m -cp $javaOut Benchmark $algorithm }
        'JavaScript' { $output = & node.exe $javascriptSource $algorithm }
        'Python' { $output = & python.exe $pythonSource $algorithm }
        default { throw "Unknown benchmark language: $language" }
    }
    $stopwatch.Stop()

    if ($LASTEXITCODE -ne 0) {
        throw "$language failed on $algorithm with exit code $LASTEXITCODE."
    }
    $actual = ($output | Select-Object -Last 1).ToString().Trim()
    if ($actual -ne $expected[$algorithm]) {
        throw "Checksum mismatch for $language/${algorithm}: expected $($expected[$algorithm]), got $actual."
    }
    return $stopwatch.Elapsed.TotalSeconds
}

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return [double]$sorted[$middle] }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Get-Representation([string]$algorithm, [string]$language) {
    if ($algorithm -eq 'object-tree') {
        if ($language -eq 'Absolute') { return 'raw class hierarchy with vtable dispatch' }
        return 'Node/BinaryNode/Leaf/AddNode/XorNode objects'
    }
    if ($algorithm -eq 'arena-graph') {
        if ($language -eq 'Absolute') { return 'raw pointers over global arenas' }
        if ($language -eq 'C++') { return 'raw pointers over static arenas' }
        return 'primitive arrays'
    }
    if ($algorithm -eq 'heap-nodes') {
        if ($language -eq 'Absolute') { return 'raw int64* heap nodes' }
        if ($language -eq 'C++') { return 'Box* heap nodes' }
        return 'Box object references'
    }
    if ($language -eq 'Absolute') { return 'generation-checked managed pointer' }
    if ($language -eq 'C++') { return 'Box* pointer' }
    return 'Box object reference'
}

$measurements = @{}
foreach ($algorithm in $algorithms) {
    [string[]]$languages = $optimizedByAlgorithm[$algorithm]
    foreach ($language in $languages) {
        $measurements["$algorithm|$language"] = [System.Collections.Generic.List[double]]::new()
    }

    if ($Warmups -gt 0) {
        Write-Host "[Warm up: $algorithm]"
        foreach ($language in $languages) {
            for ($round = 0; $round -lt $Warmups; ++$round) {
                [void](Measure-Benchmark $language $algorithm)
            }
        }
    }

    Write-Host "[Measure: $algorithm, $Samples samples per supported optimized language]"
    for ($round = 0; $round -lt $Samples; ++$round) {
        for ($offset = 0; $offset -lt $languages.Count; ++$offset) {
            $language = $languages[($round + $offset) % $languages.Count]
            $measurements["$algorithm|$language"].Add((Measure-Benchmark $language $algorithm))
        }
    }
}

if ($IncludePython -eq 1) {
    Write-Host '[Measure Python: one sample per algorithm]'
    foreach ($algorithm in $algorithms) {
        $list = [System.Collections.Generic.List[double]]::new()
        $list.Add((Measure-Benchmark 'Python' $algorithm))
        $measurements["$algorithm|Python"] = $list
    }
}

$allLanguages = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')
if ($IncludePython -eq 1) { $allLanguages += 'Python' }
$rows = @()
foreach ($algorithm in $algorithms) {
    $medianByLanguage = @{}
    foreach ($language in $allLanguages) {
        $key = "$algorithm|$language"
        if ($measurements.ContainsKey($key)) {
            [double[]]$values = $measurements[$key].ToArray()
            $medianByLanguage[$language] = Get-Median $values
        }
    }
    $fastest = ($medianByLanguage.Values | Measure-Object -Minimum).Minimum

    foreach ($language in $allLanguages) {
        $key = "$algorithm|$language"
        if (-not $measurements.ContainsKey($key)) {
            $rows += [pscustomobject]@{
                Algorithm = $algorithm
                Language = $language
                Representation = Get-Representation $algorithm $language
                Samples = 0
                MedianSeconds = 'N/A'
                MinSeconds = 'N/A'
                MaxSeconds = 'N/A'
                RelativeToFastest = 'N/A'
                WorkUnits = $workUnits[$algorithm]
                Unit = $unitNames[$algorithm]
                MillionUnitsPerSecond = 'N/A'
                Checksum = $expected[$algorithm]
                Status = 'unsupported'
            }
            continue
        }

        [double[]]$values = $measurements[$key].ToArray()
        [double]$median = $medianByLanguage[$language]
        $rows += [pscustomobject]@{
            Algorithm = $algorithm
            Language = $language
            Representation = Get-Representation $algorithm $language
            Samples = $values.Count
            MedianSeconds = $median.ToString('F6', $invariant)
            MinSeconds = ($values | Measure-Object -Minimum).Minimum.ToString('F6', $invariant)
            MaxSeconds = ($values | Measure-Object -Maximum).Maximum.ToString('F6', $invariant)
            RelativeToFastest = ($median / $fastest).ToString('F2', $invariant) + 'x'
            WorkUnits = $workUnits[$algorithm]
            Unit = $unitNames[$algorithm]
            MillionUnitsPerSecond = ($workUnits[$algorithm] / $median / 1000000.0).ToString('F2', $invariant)
            Checksum = $expected[$algorithm]
            Status = 'ok'
        }
    }
}

Write-Host ''
$rows | Format-Table Algorithm, Language, Samples, MedianSeconds, MillionUnitsPerSecond, RelativeToFastest, Status -AutoSize
Write-Host ''

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$resultPath = Join-Path $resultsRoot "pointer-object-benchmark-$timestamp.csv"
$rows | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding UTF8
Write-Host "Results saved to: $resultPath"
