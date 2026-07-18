# Generates the PD Hybrid Synth app icon: a green-phosphor CRT oscilloscope
# tile, matching the editor's "CZ Terminal" palette (bg #020805, screen edge
# #1c3a2b, grid #123322, phosphor trace #4be08a). Renders a large icon.png for
# JUCE's ICON_BIG and a simplified icon_small.png for ICON_SMALL.
#
#   powershell -ExecutionPolicy Bypass -File scripts/make_icon.ps1
#
# Pure .NET System.Drawing (GDI+), no external tools.

Add-Type -AssemblyName System.Drawing

$OutDir = Join-Path $PSScriptRoot "..\assets"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function New-RoundedRect([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2.0 * $r
    $p.AddArc($x,           $y,           $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y,           $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d,   0, 90)
    $p.AddArc($x,           $y + $h - $d, $d, $d,  90, 90)
    $p.CloseFigure()
    return $p
}

function Draw-Icon([int]$size, [string]$file, [bool]$simple) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))   # transparent canvas

    $s = [single]$size

    # --- Bezel: rounded app tile ---------------------------------------------
    $m  = $s * 0.055
    $bw = $s - 2 * $m
    $br = $s * 0.20
    $bezel = New-RoundedRect $m $m $bw $bw $br

    # Vertical bezel gradient (top a touch lighter, like moulded plastic).
    $bezTop    = New-Object System.Drawing.PointF([single]$m, [single]$m)
    $bezBottom = New-Object System.Drawing.PointF([single]$m, [single]($m + $bw))
    $bezBrush  = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $bezTop, $bezBottom,
        [System.Drawing.Color]::FromArgb(255, 16, 26, 20),
        [System.Drawing.Color]::FromArgb(255, 5, 10, 8))
    $g.FillPath($bezBrush, $bezel)

    # Green rim light around the bezel.
    $rimPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(180, 43, 107, 70), ($s * 0.006))
    $g.DrawPath($rimPen, $bezel)

    # --- Screen: inset CRT --------------------------------------------------
    $sm = $s * 0.135
    $sw = $s - 2 * $sm
    $sr = $s * 0.10
    $screen = New-RoundedRect $sm $sm $sw $sw $sr
    $g.FillPath((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 2, 8, 5))), $screen)

    # Everything screen-ish is clipped to the tube face.
    $g.SetClip($screen)

    # Faint radial glow from the centre (phosphor bloom).
    $cx = $s * 0.5; $cy = $s * 0.5
    $glowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $glowPath.AddEllipse($cx - $sw * 0.6, $cy - $sw * 0.6, $sw * 1.2, $sw * 1.2)
    $glow = New-Object System.Drawing.Drawing2D.PathGradientBrush($glowPath)
    $glow.CenterPoint    = New-Object System.Drawing.PointF($cx, $cy)
    $glow.CenterColor    = [System.Drawing.Color]::FromArgb(70, 40, 120, 80)
    $glow.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 2, 8, 5))
    $g.FillRectangle($glow, 0, 0, $s, $s)

    # Graticule: centre cross + quarter lines.
    $gridPen  = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 18, 51, 34), ($s * 0.004))
    $gridPenF = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(140, 18, 51, 34), ($s * 0.003))
    $g.DrawLine($gridPen,  $sm, $cy, $sm + $sw, $cy)
    $g.DrawLine($gridPen,  $cx, $sm, $cx, $sm + $sw)
    $g.DrawLine($gridPenF, $sm + $sw * 0.25, $sm, $sm + $sw * 0.25, $sm + $sw)
    $g.DrawLine($gridPenF, $sm + $sw * 0.75, $sm, $sm + $sw * 0.75, $sm + $sw)

    # --- Waveform: a Casio-style phase-distortion trace ----------------------
    $N = 240
    $pts = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    $cycles = 2.0
    $knee   = 0.30                 # PD knee: leans the sine into a saw
    $amp    = $sw * 0.34
    for ($i = 0; $i -le $N; $i++) {
        $t     = $i / [double]$N
        $phase = $t * $cycles
        $frac  = $phase - [math]::Floor($phase)
        if ($frac -lt $knee) { $p = 0.5 * $frac / $knee }
        else                 { $p = 0.5 + 0.5 * ($frac - $knee) / (1.0 - $knee) }
        $y = [math]::Sin(2.0 * [math]::PI * $p)
        $px = $sm + $t * $sw
        $py = $cy - $y * $amp
        $pts.Add((New-Object System.Drawing.PointF([single]$px, [single]$py)))
    }
    $wave = New-Object System.Drawing.Drawing2D.GraphicsPath
    $wave.AddCurve($pts.ToArray(), 0.5)

    # Phosphor glow: wide soft passes under a crisp bright core.
    $cap = [System.Drawing.Drawing2D.LineCap]::Round
    $glowPasses = if ($simple) { @(@(0.055, 40)) } else { @(@(0.075, 26), @(0.045, 55)) }
    foreach ($pass in $glowPasses) {
        $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb([int]$pass[1], 75, 224, 138), ($s * $pass[0]))
        $pen.StartCap = $cap; $pen.EndCap = $cap
        $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $g.DrawPath($pen, $wave)
    }
    $core = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 130, 245, 175), ($s * 0.018))
    $core.StartCap = $cap; $core.EndCap = $cap
    $core.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($core, $wave)

    # Scanlines over the whole tube (skipped on the small icon for legibility).
    if (-not $simple) {
        $step = [math]::Max(3, [int]($s / 150))
        $slPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(46, 0, 0, 0), 1.0)
        for ($yy = [int]$sm; $yy -lt ($sm + $sw); $yy += $step) {
            $g.DrawLine($slPen, $sm, $yy, $sm + $sw, $yy)
        }
        # Vignette: darken the tube corners.
        $vPath = New-Object System.Drawing.Drawing2D.GraphicsPath
        $vPath.AddEllipse($sm - $sw * 0.15, $sm - $sw * 0.15, $sw * 1.3, $sw * 1.3)
        $vig = New-Object System.Drawing.Drawing2D.PathGradientBrush($vPath)
        $vig.CenterPoint    = New-Object System.Drawing.PointF($cx, $cy)
        $vig.CenterColor    = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)
        $vig.SurroundColors = @([System.Drawing.Color]::FromArgb(180, 0, 0, 0))
        $g.FillRectangle($vig, 0, 0, $s, $s)
    }

    $g.ResetClip()

    # Thin inner screen frame on top of everything.
    $screenEdge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 28, 58, 43), ($s * 0.006))
    $g.DrawPath($screenEdge, $screen)

    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "wrote $file ($size x $size)"
}

Draw-Icon 1024 (Join-Path $OutDir "icon.png")       $false
Draw-Icon 256  (Join-Path $OutDir "icon_small.png") $true
