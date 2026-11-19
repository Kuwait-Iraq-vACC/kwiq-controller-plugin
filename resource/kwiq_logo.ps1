# kwiq_logo.ps1 - makes the KWIQ button's logo images (kwiq_logo_28.bmp ... kwiq_logo_56.bmp) from kwiq_logo.png.
#
# kwiq_logo.png is the vACC's logo, 4096 px square with a wide transparent margin. For each size the button is
# drawn at (28 px at 100% Windows scaling, 35 at 125%, 42 at 150%, 49 at 175%, 56 at 200%), this crops the logo
# to its visible part, centres that in a square and scales it down (high-quality bicubic), so the button needs
# no scaling at those sizes and stays crisp. Each is written as a 32-bit BMP with straight (not premultiplied)
# alpha; kwiq.rc embeds them and KWIQPlugin.cpp premultiplies and alpha-blends them.
#
# Run from the repo root in Windows PowerShell: powershell -NoProfile -ExecutionPolicy Bypass -File resource/kwiq_logo.ps1
# Only needed when the logo changes; the BMPs are committed.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = [System.Drawing.Bitmap]::FromFile((Join-Path $root 'kwiq_logo.png'))

# The visible part: the bounding box of every pixel that isn't fully transparent
$data = $src.LockBits((New-Object System.Drawing.Rectangle 0, 0, $src.Width, $src.Height),
                      [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $src.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$src.UnlockBits($data)
$minX = $src.Width; $minY = $src.Height; $maxX = -1; $maxY = -1
for ($y = 0; $y -lt $src.Height; $y++) {
    $row = $y * $data.Stride
    for ($x = 0; $x -lt $src.Width; $x++) {
        if ($bytes[$row + $x * 4 + 3] -ne 0) {
            if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
        }
    }
}
if ($maxX -lt 0) { throw 'kwiq_logo.png is fully transparent' }
$w = $maxX - $minX + 1; $h = $maxY - $minY + 1; $side = [math]::Max($w, $h)
"visible part: ${w}x${h} at $minX,$minY; square side $side"

foreach ($size in 28, 35, 42, 49, 56) {
    $dst = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppPArgb)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $attr = New-Object System.Drawing.Imaging.ImageAttributes
    $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)     # no dark fringe at the edges
    # the visible part, centred in a square of $side source pixels, scaled to $size
    $scale = $size / $side
    $dw = $w * $scale; $dh = $h * $scale
    $dest = New-Object System.Drawing.RectangleF (($size - $dw) / 2), (($size - $dh) / 2), $dw, $dh
    $g.DrawImage($src, [System.Drawing.Rectangle]::Round($dest), $minX, $minY, $w, $h, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    $g.Dispose()

    # 32-bit BMP, bottom-up, BGRA with straight alpha
    $lock = $dst.LockBits((New-Object System.Drawing.Rectangle 0, 0, $size, $size),
                          [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $px = New-Object byte[] ($size * $size * 4)
    for ($y = 0; $y -lt $size; $y++) {
        [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]($lock.Scan0.ToInt64() + $y * $lock.Stride), $px, ($size - 1 - $y) * $size * 4, $size * 4)
    }
    $dst.UnlockBits($lock); $dst.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $bw.Write([uint16]0x4D42); $bw.Write([uint32](14 + 40 + $px.Length)); $bw.Write([uint32]0); $bw.Write([uint32](14 + 40))
    $bw.Write([uint32]40); $bw.Write([int32]$size); $bw.Write([int32]$size); $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]0); $bw.Write([uint32]$px.Length); $bw.Write([int32]3780); $bw.Write([int32]3780); $bw.Write([uint32]0); $bw.Write([uint32]0)
    $bw.Write($px); $bw.Flush()
    [System.IO.File]::WriteAllBytes((Join-Path $root "kwiq_logo_$size.bmp"), $ms.ToArray())
    "kwiq_logo_$size.bmp"
}
$src.Dispose()
