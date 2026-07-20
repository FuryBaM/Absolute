[CmdletBinding()]
param([string]$Version = '1.13.2')

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$installRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\toolchains\ninja'))
$downloadsRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.absolute\downloads'))
$ninja = Join-Path $installRoot 'ninja.exe'
if (Test-Path -LiteralPath $ninja) {
    Write-Output $ninja
    exit 0
}
New-Item -ItemType Directory -Force -Path $installRoot, $downloadsRoot | Out-Null
$archive = Join-Path $downloadsRoot "ninja-win-$Version.zip"
if (-not (Test-Path -LiteralPath $archive)) {
    $url = "https://github.com/ninja-build/ninja/releases/download/v$Version/ninja-win.zip"
    & curl.exe --fail --location --retry 3 --output $archive $url
    if ($LASTEXITCODE -ne 0) { throw "Ninja download failed with exit code $LASTEXITCODE" }
}
Expand-Archive -LiteralPath $archive -DestinationPath $installRoot -Force
if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja extraction failed: $ninja" }
Write-Host "Installed Ninja ${Version}: $ninja"
Write-Output $ninja
