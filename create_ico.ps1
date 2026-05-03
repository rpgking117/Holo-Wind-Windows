# create_ico.ps1
# Reads HolotapeMorrowind01_d.dds (uncompressed BGRA 1024x1024),
# scales to 256/48/32/16 px, and writes HolloWindSetup.ico next to this script.

param([string]$OutPath = "")

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $OutPath) {
    $OutPath = Join-Path $scriptDir "HolloWindSetup.ico"
}

# Search for the DDS texture in likely locations
$ddsCandidates = @(
    (Join-Path $scriptDir "..\..\Fallout 4\Data\Textures\HolloWind\HolotapeMorrowind01_d.dds"),
    (Join-Path $scriptDir "fo4_data_src\Textures\HolloWind\HolotapeMorrowind01_d.dds"),
    (Join-Path $scriptDir "HolotapeMorrowind01_d.dds")
)

$ddsPath = $null
foreach ($c in $ddsCandidates) {
    $resolved = [System.IO.Path]::GetFullPath($c)
    if (Test-Path $resolved) { $ddsPath = $resolved; break }
}

if (-not $ddsPath) {
    Write-Warning "DDS texture not found - icon will not be created"
    exit 0
}

Add-Type -AssemblyName System.Drawing

# ---- Load uncompressed BGRA DDS into a 32bpp bitmap -------------------------
$data = [System.IO.File]::ReadAllBytes($ddsPath)
if ($data.Length -lt 128 -or
    [System.Text.Encoding]::ASCII.GetString($data, 0, 4) -ne "DDS ") {
    Write-Warning "Not a valid DDS file: $ddsPath"
    exit 0
}

$imgH = [System.BitConverter]::ToInt32($data, 12)
$imgW = [System.BitConverter]::ToInt32($data, 16)

if ($imgW -le 0 -or $imgH -le 0 -or $data.Length -lt (128 + $imgW * $imgH * 4)) {
    Write-Warning "DDS dimensions look wrong ($imgW x $imgH)"
    exit 0
}

# Format32bppArgb stores B,G,R,A in memory order — matches DDS BGRA layout exactly
$src = New-Object System.Drawing.Bitmap($imgW, $imgH,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

$bd = $src.LockBits(
    [System.Drawing.Rectangle]::new(0, 0, $imgW, $imgH),
    [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

[System.Runtime.InteropServices.Marshal]::Copy($data, 128, $bd.Scan0, $imgW * $imgH * 4)
$src.UnlockBits($bd)

# ---- Build one PNG blob per size -------------------------------------------
$sizes     = @(256, 48, 32, 16)
$pngBlobs  = @()

# Crop the center square of the source (the holotape is centred)
$cropSz = [Math]::Min($imgW, $imgH)
$cropX  = [int](($imgW - $cropSz) / 2)
$cropY  = [int](($imgH - $cropSz) / 2)
$srcRect = [System.Drawing.Rectangle]::new($cropX, $cropY, $cropSz, $cropSz)

foreach ($sz in $sizes) {
    $scaled = New-Object System.Drawing.Bitmap($sz, $sz,
                  [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($scaled)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $dstRect = [System.Drawing.Rectangle]::new(0, 0, $sz, $sz)
    $g.DrawImage($src, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $scaled.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngBlobs += ,$ms.ToArray()
    $ms.Dispose()
    $scaled.Dispose()
}
$src.Dispose()

# ---- Write ICO file --------------------------------------------------------
# Layout: ICONDIR (6 bytes) + N * ICONDIRENTRY (16 bytes each) + image data
$n       = $sizes.Count
$dataOff = 6 + $n * 16

$fs     = [System.IO.File]::Create($OutPath)
$writer = New-Object System.IO.BinaryWriter($fs)

# ICONDIR
$writer.Write([uint16]0)   # reserved
$writer.Write([uint16]1)   # type = 1 (ICO)
$writer.Write([uint16]$n)  # image count

# ICONDIRENTRYs
$off = $dataOff
for ($i = 0; $i -lt $n; $i++) {
    $sz      = $sizes[$i]
    $blobLen = $pngBlobs[$i].Length
    $w8 = if ($sz -ge 256) { [byte]0 } else { [byte]$sz }
    $h8 = if ($sz -ge 256) { [byte]0 } else { [byte]$sz }
    $writer.Write([byte]$w8)          # width  (0 = 256)
    $writer.Write([byte]$h8)          # height (0 = 256)
    $writer.Write([byte]0)            # color count
    $writer.Write([byte]0)            # reserved
    $writer.Write([uint16]1)          # planes
    $writer.Write([uint16]32)         # bit depth
    $writer.Write([uint32]$blobLen)   # size of image data
    $writer.Write([uint32]$off)       # offset of image data
    $off += $blobLen
}

# Image data (PNG blobs)
foreach ($blob in $pngBlobs) {
    $writer.Write($blob)
}

$writer.Close()
$fs.Dispose()

Write-Host "OK  Created $OutPath  ($($sizes -join ', ') px,  $([Math]::Round((Get-Item $OutPath).Length/1KB)) KB)"
