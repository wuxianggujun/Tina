#Requires -Version 5.1
<#
.SYNOPSIS
  UI-003 visual gate (product slice): capture tina_sample_2d and verify ROI fingerprints.

.DESCRIPTION
  Wraps CaptureSampleWindow.ps1, then samples relative ROIs on a useful non-blank
  client capture. Design baseline is 960x540; ROIs are fractions of client size so
  host DPI still hits the same logical HUD regions. Multi-monitor multi-DPI golden
  matrix remains open.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [string]$ArgString = '--frames=120 --frame-delay-ms=0',
    [string]$OutDir = 'artifacts/screenshots/ui-003-visual',
    [int]$WarmupMs = 1000,
    [int]$CaptureCount = 3,
    [int]$CaptureIntervalMs = 350,
    [int]$RequiredConsecutiveUsefulCaptures = 2,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
}
Set-Location -LiteralPath $SourceRoot

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $SourceRoot 'out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe'
}

function Invoke-Checked {
    param([string]$Name, [scriptblock]$Block)
    Write-Host "=== $Name ==="
    & $Block
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "step failed: $Name exit=$LASTEXITCODE"
    }
}

if (-not $SkipBuild) {
    Invoke-Checked 'build tina_sample_2d' {
        cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d -- /m:2 /v:m
    }
}

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "missing exe: $Exe"
}

$captureScript = Join-Path $SourceRoot 'tools\windows\CaptureSampleWindow.ps1'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runRoot = Join-Path $SourceRoot (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

Write-Host '=== CaptureSampleWindow ==='
& powershell -NoProfile -ExecutionPolicy Bypass -File $captureScript `
    -Exe $Exe `
    -ArgString $ArgString `
    -OutDir $runRoot `
    -WarmupMs $WarmupMs `
    -CaptureCount $CaptureCount `
    -CaptureIntervalMs $CaptureIntervalMs `
    -RequiredConsecutiveUsefulCaptures $RequiredConsecutiveUsefulCaptures `
    -RequireNonBlank
$captureExit = 0
if ($null -ne $LASTEXITCODE) { $captureExit = [int]$LASTEXITCODE }

$runDir = Get-ChildItem -LiteralPath $runRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $runDir) {
    throw "capture output directory not found under $runRoot"
}
$reportPath = Join-Path $runDir.FullName 'report.json'
if (-not (Test-Path -LiteralPath $reportPath)) {
    throw "missing report.json in $($runDir.FullName)"
}
$captureReport = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-RoiFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$PngPath,
        [Parameter(Mandatory = $true)][double]$X0,
        [Parameter(Mandatory = $true)][double]$Y0,
        [Parameter(Mandatory = $true)][double]$X1,
        [Parameter(Mandatory = $true)][double]$Y1,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $img = New-Object System.Drawing.Bitmap $PngPath
    try {
        [int]$w = $img.Width
        [int]$h = $img.Height
        [int]$left = [Math]::Max(0, [int][Math]::Floor($w * $X0))
        [int]$top = [Math]::Max(0, [int][Math]::Floor($h * $Y0))
        [int]$right = [Math]::Min($w, [int][Math]::Ceiling($w * $X1))
        [int]$bottom = [Math]::Min($h, [int][Math]::Ceiling($h * $Y1))
        [int]$roiW = $right - $left
        [int]$roiH = $bottom - $top
        if ($roiW -le 0 -or $roiH -le 0) {
            throw "empty ROI $Name"
        }

        [int64]$sumR = 0
        [int64]$sumG = 0
        [int64]$sumB = 0
        [int]$n = 0
        $hist = @{}
        [int]$stepX = [Math]::Max(1, [int][Math]::Floor($roiW / 24.0))
        [int]$stepY = [Math]::Max(1, [int][Math]::Floor($roiH / 24.0))

        for ([int]$y = $top; $y -lt $bottom; $y += $stepY) {
            for ([int]$x = $left; $x -lt $right; $x += $stepX) {
                $c = $img.GetPixel($x, $y)
                $sumR += $c.R
                $sumG += $c.G
                $sumB += $c.B
                $k = ('{0:X2}{1:X2}{2:X2}' -f $c.R, $c.G, $c.B)
                if ($hist.ContainsKey($k)) {
                    $hist[$k] = [int]$hist[$k] + 1
                } else {
                    $hist[$k] = 1
                }
                $n++
            }
        }

        $topEntry = $null
        $topCount = -1
        foreach ($entry in $hist.GetEnumerator()) {
            if ([int]$entry.Value -gt $topCount) {
                $topCount = [int]$entry.Value
                $topEntry = $entry
            }
        }

        $avgR = [Math]::Round(($sumR / [double]$n), 2)
        $avgG = [Math]::Round(($sumG / [double]$n), 2)
        $avgB = [Math]::Round(($sumB / [double]$n), 2)
        $domRatio = [Math]::Round(($topCount / [double]$n), 4)

        return [pscustomobject]@{
            name               = $Name
            left               = $left
            top                = $top
            width              = $roiW
            height             = $roiH
            samples            = $n
            avgR               = $avgR
            avgG               = $avgG
            avgB               = $avgB
            dominantColor      = [string]$topEntry.Key
            dominantRatio      = $domRatio
            uniqueSampleColors = [int]$hist.Count
        }
    } finally {
        $img.Dispose()
    }
}

$useful = @()
foreach ($c in @($captureReport.captures)) {
    $blank = [bool]$c.blankLike
    $unique = [int]$c.uniqueSampleColors
    $usefulFlag = $false
    if ($null -ne $c.usefulNonBlank) { $usefulFlag = [bool]$c.usefulNonBlank }
    if ($usefulFlag -or ((-not $blank) -and ($unique -ge 3))) {
        $useful += $c
    }
}
if ($useful.Count -lt 1) {
    foreach ($c in @($captureReport.captures)) {
        if (-not [bool]$c.blankLike) { $useful += $c }
    }
}
if ($useful.Count -lt 1) {
    throw 'no useful non-blank capture for ROI analysis'
}

$primary = $useful[$useful.Count - 1]
[int]$clientW = $primary.width
[int]$clientH = $primary.height
$pngPath = [string]$primary.path
if (-not (Test-Path -LiteralPath $pngPath)) {
    $pngPath = Join-Path $runDir.FullName (Split-Path -Leaf ([string]$primary.path))
}
if (-not (Test-Path -LiteralPath $pngPath)) {
    throw "capture png not found: $pngPath"
}

$roiList = @(
    (Get-RoiFingerprint -PngPath $pngPath -X0 0.01 -Y0 0.01 -X1 0.35 -Y1 0.14 -Name 'title_plate'),
    (Get-RoiFingerprint -PngPath $pngPath -X0 0.68 -Y0 0.01 -X1 0.99 -Y1 0.85 -Name 'settings_panel'),
    (Get-RoiFingerprint -PngPath $pngPath -X0 0.70 -Y0 0.55 -X1 0.97 -Y1 0.62 -Name 'progress_bar'),
    # Lower playfield (ground tiles / props); mid-sky is often a single clear color.
    (Get-RoiFingerprint -PngPath $pngPath -X0 0.05 -Y0 0.55 -X1 0.55 -Y1 0.92 -Name 'playfield_lower')
)

$gateErrors = New-Object System.Collections.Generic.List[string]
$progress = $roiList | Where-Object { $_.name -eq 'progress_bar' } | Select-Object -First 1
$settings = $roiList | Where-Object { $_.name -eq 'settings_panel' } | Select-Object -First 1
$playfield = $roiList | Where-Object { $_.name -eq 'playfield_lower' } | Select-Object -First 1

# Progress bar fill is green-ish; allow dark panels but require some G signal.
if ($null -eq $progress -or [double]$progress.avgG -lt 35.0) {
    [void]$gateErrors.Add('progress_bar ROI lacks green-channel signal (expected filled bar)')
}
if ($null -eq $settings -or [int]$settings.uniqueSampleColors -lt 4) {
    [void]$gateErrors.Add('settings_panel ROI too flat (expected multi-control chrome)')
}
if ($null -eq $playfield -or [int]$playfield.uniqueSampleColors -lt 2) {
    [void]$gateErrors.Add('playfield_lower ROI too flat (expected tile ground variation)')
}
if ($clientW -lt 320 -or $clientH -lt 180) {
    [void]$gateErrors.Add("client capture too small: ${clientW}x${clientH}")
}

[double]$aspect = $clientW / [Math]::Max(1.0, [double]$clientH)
if ($aspect -lt 1.4 -or $aspect -gt 2.0) {
    [void]$gateErrors.Add("unexpected client aspect=$aspect (expected ~16:9)")
}

$stdoutPath = Join-Path $runDir.FullName 'stdout.txt'
$productOk = $false
$accessibilityPublished = $false
if (Test-Path -LiteralPath $stdoutPath) {
    $tail = Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8
    if ($tail -match '"status"\s*:\s*"ok"') { $productOk = $true }
    if ($tail -match '"accessibilityPublished"\s*:\s*true') { $accessibilityPublished = $true }
}
if (-not $productOk) {
    [void]$gateErrors.Add('sample stdout missing status=ok JSON')
}

$captureOk = $false
if ($null -ne $captureReport.ok) { $captureOk = [bool]$captureReport.ok }
$ok = ($captureExit -eq 0) -and $captureOk -and ($gateErrors.Count -eq 0)

$roiJson = @()
foreach ($r in $roiList) {
    $roiJson += [pscustomobject]@{
        name = [string]$r.name
        rectPx = @([int]$r.left, [int]$r.top, [int]$r.width, [int]$r.height)
        samples = [int]$r.samples
        avgRgb = @([double]$r.avgR, [double]$r.avgG, [double]$r.avgB)
        dominantColor = [string]$r.dominantColor
        dominantRatio = [double]$r.dominantRatio
        uniqueSampleColors = [int]$r.uniqueSampleColors
    }
}

$gateReport = [pscustomobject]@{
    schema = 1
    gate = 'UI-003-visual-roi'
    ok = [bool]$ok
    tip = [string](git rev-parse HEAD 2>$null)
    captureReportPath = [string]$reportPath
    captureOk = [bool]$captureOk
    captureExit = [int]$captureExit
    clientSize = @([int]$clientW, [int]$clientH)
    aspect = [math]::Round($aspect, 4)
    productStdoutOk = [bool]$productOk
    accessibilityPublished = [bool]$accessibilityPublished
    rois = $roiJson
    errors = @($gateErrors | ForEach-Object { [string]$_ })
    notes = @(
        'ROI fractions relative to client capture (design baseline 960x540)',
        'Single-host visual gate; multi-monitor multi-DPI golden matrix still open',
        'blankLike frames excluded via CaptureSampleWindow usefulNonBlank gate'
    )
}

$gatePath = Join-Path $runDir.FullName 'ui-003-gate.json'
$gateReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $gatePath -Encoding utf8

$summaryDir = Join-Path $SourceRoot 'artifacts\gates'
if (-not (Test-Path -LiteralPath $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir -Force | Out-Null
}
$summaryPath = Join-Path $summaryDir ("ui-003-visual-{0}.json" -f $runDir.Name)
Copy-Item -LiteralPath $gatePath -Destination $summaryPath -Force

Write-Host "ui-003 gate report: $gatePath"
Write-Host "summary: $summaryPath"
Write-Host ("ok={0} client={1}x{2} roiErrors={3}" -f $ok, $clientW, $clientH, $gateErrors.Count)
if (-not $ok) { exit 1 }
exit 0
