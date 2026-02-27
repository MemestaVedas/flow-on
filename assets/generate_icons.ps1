# generate_icons.ps1 — Creates placeholder ICO files using .NET System.Drawing
# Run from the project root: pwsh assets\generate_icons.ps1
# Requires .NET with System.Drawing support (included in Windows PowerShell / .NET Framework)

Add-Type -AssemblyName System.Drawing

function New-SolidIcon {
    param(
        [string]$OutputPath,
        [System.Drawing.Color]$Color,
        [int[]]$Sizes = @(256, 128, 64, 48, 32, 16)
    )

    # Create bitmaps at each size
    $bitmaps = foreach ($size in $Sizes) {
        $bmp = New-Object System.Drawing.Bitmap($size, $size)
        $g   = [System.Drawing.Graphics]::FromImage($bmp)
        $g.Clear($Color)
        # Draw a simple circle in the center
        $margin = [int]($size * 0.15)
        $brush  = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
        $g.FillEllipse($brush, $margin, $margin, $size - 2*$margin, $size - 2*$margin)
        $brush.Dispose()
        $g.Dispose()
        $bmp
    }

    # Write ICO file manually (ICO format: header + directory + BMP/PNG data)
    $stream = [System.IO.File]::OpenWrite($OutputPath)
    $writer = New-Object System.IO.BinaryWriter($stream)

    # ICO header
    $writer.Write([uint16]0)          # reserved
    $writer.Write([uint16]1)          # type: ICO
    $writer.Write([uint16]$Sizes.Count)

    # Calculate data offset: header(6) + directory(16 * n)
    $headerSize = 6 + 16 * $Sizes.Count
    $offset     = $headerSize

    $pngStreams = @()
    foreach ($bmp in $bitmaps) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngStreams += $ms
    }

    # Write directory entries
    for ($i = 0; $i -lt $Sizes.Count; $i++) {
        $sz   = $Sizes[$i]
        $data = $pngStreams[$i].ToArray()
        $writer.Write([byte]$(if ($sz -ge 256) { 0 } else { $sz }))  # width
        $writer.Write([byte]$(if ($sz -ge 256) { 0 } else { $sz }))  # height
        $writer.Write([byte]0)           # color count
        $writer.Write([byte]0)           # reserved
        $writer.Write([uint16]1)         # planes
        $writer.Write([uint16]32)        # bit count
        $writer.Write([uint32]$data.Length)
        $writer.Write([uint32]$offset)
        $offset += $data.Length
    }

    # Write image data
    foreach ($ms in $pngStreams) {
        $writer.Write($ms.ToArray())
        $ms.Dispose()
    }

    $writer.Close()
    $stream.Close()
    foreach ($bmp in $bitmaps) { $bmp.Dispose() }
    Write-Host "Created: $OutputPath"
}

$projectRoot = Split-Path -Parent $PSScriptRoot

# app_icon.ico — dark blue background
New-SolidIcon `
    -OutputPath (Join-Path $projectRoot "assets\app_icon.ico") `
    -Color ([System.Drawing.Color]::FromArgb(255, 15, 25, 50))

# Tray idle — dark background with white circle
New-SolidIcon `
    -OutputPath (Join-Path $projectRoot "flow-on.ico") `
    -Color ([System.Drawing.Color]::FromArgb(255, 20, 20, 30)) `
    -Sizes @(32, 16)

# Tray recording — red background
New-SolidIcon `
    -OutputPath (Join-Path $projectRoot "small.ico") `
    -Color ([System.Drawing.Color]::FromArgb(255, 200, 30, 30)) `
    -Sizes @(32, 16)

Write-Host ""
Write-Host "Placeholder icons generated. Replace with real artwork before shipping."
Write-Host "See assets\README.txt for tools and instructions."
