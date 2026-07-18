[CmdletBinding()]
param(
    [ValidateRange(1, 100)]
    [int]$Samples = 15,

    [ValidateRange(0, 20)]
    [int]$Warmups = 2,

    [ValidateSet(0, 1)]
    [int]$IncludePython = 1
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
$compilerBuild = Join-Path $buildRoot 'compiler'
$resultsRoot = Join-Path $suiteRoot 'results'

foreach ($directory in @($buildRoot, $nativeOut, $javaOut, $dotnetOut, $dotnetBin, $dotnetObj, $resultsRoot)) {
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

function Link-NativeExecutable([string]$objectPath, [string]$executablePath) {
    $arguments = @(
        '/nologo',
        "/out:$executablePath",
        '/subsystem:console',
        '/stack:67108864',
        $objectPath,
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

Require-Command 'wsl.exe'
Require-Command 'link.exe'
Require-Command 'dotnet.exe'
Require-Command 'javac.exe'
Require-Command 'java.exe'
Require-Command 'node.exe'
if ($IncludePython -eq 1) { Require-Command 'python.exe' }

$wslCmake = Find-WslTool @('cmake')
$wslClang = Find-WslTool @('clang++-18', 'clang++')
$wslLlvmConfig = Find-WslTool @('llvm-config-18', 'llvm-config')
$llvmDirectory = (& wsl.exe $wslLlvmConfig --cmakedir | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0 -or -not $llvmDirectory) {
    throw 'Could not determine the LLVM CMake directory in WSL.'
}
$zstdIncludeResult = & wsl.exe sh -lc "find /usr/include /usr/local/include /root -type f -name zstd.h -print 2>/dev/null | head -n 1"
$zstdLibraryResult = & wsl.exe sh -lc "find /usr/lib /usr/local/lib /root -name libzstd.so -print 2>/dev/null | head -n 1"
$zstdInclude = if ($zstdIncludeResult) { ($zstdIncludeResult | Select-Object -Last 1).Trim() } else { '' }
$zstdLibrary = if ($zstdLibraryResult) { ($zstdLibraryResult | Select-Object -Last 1).Trim() } else { '' }

$repoWsl = Convert-ToWslPath $repoRoot
$compilerBuildWsl = Convert-ToWslPath $compilerBuild

$configureArguments = @(
    $wslCmake,
    '-S', $repoWsl,
    '-B', $compilerBuildWsl,
    '-DCMAKE_BUILD_TYPE=Release',
    '-DABSOLUTE_ENABLE_LLVM=ON',
    "-DLLVM_DIR=$llvmDirectory"
)
if ($zstdInclude -and $zstdLibrary) {
    $configureArguments += "-Dzstd_INCLUDE_DIR=$zstdInclude"
    $configureArguments += "-Dzstd_LIBRARY=$zstdLibrary"
}
Invoke-External 'Configure Absolute Release compiler' 'wsl.exe' $configureArguments
Invoke-External 'Build Absolute Release compiler' 'wsl.exe' @(
    $wslCmake,
    '--build', $compilerBuildWsl,
    '--parallel'
)

$absoluteCompiler = Join-Path $compilerBuild 'Release\absolutec'
if (-not (Test-Path -LiteralPath $absoluteCompiler)) {
    throw "Absolute compiler was not produced at $absoluteCompiler"
}
$absoluteCompilerWsl = Convert-ToWslPath $absoluteCompiler

$algorithms = @('scan', 'random-access', 'insertion-sort')
$expected = @{
    'scan' = '1063467781802240'
    'random-access' = '268750506018432'
    'insertion-sort' = '312098439810'
}

Write-Host '[Compile Absolute benchmarks]'
foreach ($algorithm in $algorithms) {
    $source = Join-Path $suiteRoot "absolute\$algorithm.abs"
    $llvmIr = Join-Path $nativeOut "absolute-$algorithm.ll"
    $object = Join-Path $nativeOut "absolute-$algorithm.obj"
    $executable = Join-Path $nativeOut "absolute-$algorithm.exe"

    & wsl.exe $absoluteCompilerWsl (Convert-ToWslPath $source) --emit-llvm -o (Convert-ToWslPath $llvmIr)
    if ($LASTEXITCODE -ne 0) { throw "Absolute compilation failed for $algorithm." }

    & wsl.exe $wslClang --target=x86_64-pc-windows-msvc -O3 -march=native -DNDEBUG `
        -c (Convert-ToWslPath $llvmIr) -o (Convert-ToWslPath $object)
    if ($LASTEXITCODE -ne 0) { throw "LLVM optimization failed for $algorithm." }

    Link-NativeExecutable $object $executable
}

Write-Host '[Compile C++ benchmark]'
$cppObject = Join-Path $nativeOut 'benchmark-cpp.obj'
$cppExecutable = Join-Path $nativeOut 'benchmark-cpp.exe'
& wsl.exe $wslClang --target=x86_64-pc-windows-msvc -O3 -march=native -DNDEBUG `
    -fno-exceptions -fno-rtti -c (Convert-ToWslPath (Join-Path $suiteRoot 'benchmark.cpp')) `
    -o (Convert-ToWslPath $cppObject)
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
Link-NativeExecutable $cppObject $cppExecutable

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
$optimizedLanguages = @('Absolute', 'C++', 'C#', 'Java', 'JavaScript')

function Measure-Benchmark([string]$language, [string]$algorithm) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    switch ($language) {
        'Absolute' { $output = & (Join-Path $nativeOut "absolute-$algorithm.exe") }
        'C++' { $output = & $cppExecutable $algorithm }
        'C#' { $output = & $dotnetExecutable $algorithm }
        'Java' { $output = & java.exe -cp $javaOut Benchmark $algorithm }
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
            Algorithm = $algorithm
            Language = $language
            Samples = $values.Count
            MedianSeconds = (Get-Median $values).ToString('F6', $invariant)
            MinSeconds = ($values | Measure-Object -Minimum).Minimum.ToString('F6', $invariant)
            MaxSeconds = ($values | Measure-Object -Maximum).Maximum.ToString('F6', $invariant)
            Checksum = $expected[$algorithm]
        }
    }
}

Write-Host ''
$rows | Format-Table Algorithm, Language, Samples, MedianSeconds, MinSeconds, MaxSeconds -AutoSize

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$resultPath = Join-Path $resultsRoot "array-benchmark-$timestamp.csv"
$rows | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding UTF8
Write-Host "Results saved to: $resultPath"
