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

# Real-time antivirus scanning inspects every file as it is written, which
# dominates the cost of unpacking a toolchain of this size: on CI runners the
# download finishes in seconds while extraction runs for tens of minutes.
# Excluding the toolchains tree is best effort: a machine without Defender, or
# without the privileges to configure it, should still extract, just slowly.
try {
    Add-MpPreference -ExclusionPath $toolchainsRoot -ErrorAction Stop
    Write-Host "Defender exclusion added for $toolchainsRoot"
} catch {
    Write-Host "Defender exclusion unavailable, continuing without it: $($_.Exception.Message)"
}

Write-Host 'Extracting LLVM SDK...'
$extractStarted = Get-Date

# bsdtar decodes xz on a single thread and has been observed taking more than 25
# minutes on CI for this archive, with the download itself finishing in seconds
# and antivirus scanning ruled out. 7-Zip is present on the hosted Windows
# images and decodes xz in parallel, so split the work: xz first, then the plain
# tar. Fall back to tar.exe wherever 7-Zip is unavailable or unhappy.
$sevenZip = $null
$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($sevenZipCommand) {
    $sevenZip = $sevenZipCommand.Source
} else {
    $candidates = @()
    if ($env:ProgramFiles) { $candidates += (Join-Path $env:ProgramFiles '7-Zip\7z.exe') }
    if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} '7-Zip\7z.exe') }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { $sevenZip = $candidate; break }
    }
}

$extracted = $false
if ($sevenZip) {
    Write-Host "Decompressing with 7-Zip: $sevenZip"
    $tarPath = Join-Path $downloadsRoot ([IO.Path]::GetFileNameWithoutExtension($archiveName))
    if (Test-Path -LiteralPath $tarPath) { Remove-Item -LiteralPath $tarPath -Force }
    & $sevenZip e $archive "-o$downloadsRoot" -y | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $tarPath)) {
        Write-Host ("  xz decoded in {0:n0}s" -f ((Get-Date) - $extractStarted).TotalSeconds)
        $tarStarted = Get-Date
        & $sevenZip x $tarPath "-o$extractRoot" -y | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host ("  tar expanded in {0:n0}s" -f ((Get-Date) - $tarStarted).TotalSeconds)
            $extracted = $true
        }
        Remove-Item -LiteralPath $tarPath -Force -ErrorAction SilentlyContinue
    }
    if (-not $extracted) { Write-Host '7-Zip extraction did not complete, falling back to tar.exe' }
}

if (-not $extracted) {
    & tar.exe -xf $archive -C $extractRoot
    if ($LASTEXITCODE -ne 0) { throw "LLVM extraction failed with exit code $LASTEXITCODE" }
}
Write-Host ("Extracted in {0:n0}s" -f ((Get-Date) - $extractStarted).TotalSeconds)

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
