$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$dir = $PSScriptRoot
foreach ($name in @('ship', 'atlas')) {
    $bmpPath = Join-Path $dir "$name.bmp"
    $pngPath = Join-Path $dir "$name.png"
    $bmp = [System.Drawing.Bitmap]::FromFile($bmpPath)
    try {
        $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "Wrote $pngPath"
    } finally {
        $bmp.Dispose()
    }
}
