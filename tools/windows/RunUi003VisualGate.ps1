#Requires -Version 5.1
<#
.SYNOPSIS
  UI-003 visual gate (product slice): capture tina_sample_2d and verify ROI fingerprints.

.DESCRIPTION
  Wraps CaptureSampleWindow.ps1, then samples absolute-design ROIs mapped into the
  client capture (product-2d HUD is design-locked at 960x540). blankLike frames are
  excluded via CaptureSampleWindow. Parses sample JSON for GLFW logical/framebuffer
  /contentScale and asserts consistency. Does NOT change OS display scale; true
  OS 100/150/200% multi-DPI golden matrix remains open.
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
    [switch]$SkipBuild,
    # Compare ROI avgRgb / dominantColor / unique colors against a checked-in baseline.
    [string]$BaselinePath = 'tools/windows/baselines/ui-003-sample2d-960x540.json',
    # Max absolute avg channel delta allowed when a baseline is present.
    [double]$AvgRgbTolerance = 28.0,
    [switch]$WriteBaseline,
    [switch]$NoBaselineCompare,
    # Design-space absolute ROI (sample absolute layout is fixed to this design).
    [int]$DesignWidth = 960,
    [int]$DesignHeight = 540,
    # Expected --width/--height logical size (0 = do not assert against args).
    [int]$ExpectedLogicalWidth = 0,
    [int]$ExpectedLogicalHeight = 0,
    # Content-scale consistency: |fb - logical * scale| max pixel error per axis.
    [double]$ContentScalePixelTolerance = 2.0,
    [double]$ContentScaleMin = 0.5,
    [double]$ContentScaleMax = 4.0
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
if ([IO.Path]::IsPathRooted($OutDir)) {
    $runRoot = Join-Path $OutDir $stamp
} else {
    $runRoot = Join-Path $SourceRoot (Join-Path $OutDir $stamp)
}
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

# Absolute design ROIs (logical px in 960x540 sample layout). Map through client/design
# so --width/--height windows still hit the same HUD chrome (absolute, not percent layout).
function Get-DesignRoiFraction {
    param([int]$DesignCoord, [int]$DesignExtent, [int]$ClientExtent)
    if ($ClientExtent -le 0) { return 0.0 }
    # When client == design (1x content scale), fraction = coord/design.
    # When client is larger but UI stays absolute, fraction = coord/client.
    return [double]$DesignCoord / [double]$ClientExtent
}

$gateErrors = New-Object System.Collections.Generic.List[string]

$designRois = @(
    @{ Name = 'title_plate'; L = 16; T = 12; R = 336; B = 68 },
    @{ Name = 'settings_panel'; L = 668; T = 8; R = 944; B = 456 },
    @{ Name = 'progress_bar'; L = 700; T = 300; R = 940; B = 330 },
    @{ Name = 'playfield_lower'; L = 40; T = 300; R = 520; B = 500 }
)

$roiList = @()
foreach ($d in $designRois) {
    $x0 = Get-DesignRoiFraction -DesignCoord ([int]$d.L) -DesignExtent $DesignWidth -ClientExtent $clientW
    $y0 = Get-DesignRoiFraction -DesignCoord ([int]$d.T) -DesignExtent $DesignHeight -ClientExtent $clientH
    $x1 = Get-DesignRoiFraction -DesignCoord ([int]$d.R) -DesignExtent $DesignWidth -ClientExtent $clientW
    $y1 = Get-DesignRoiFraction -DesignCoord ([int]$d.B) -DesignExtent $DesignHeight -ClientExtent $clientH
    if ($x1 -le $x0 -or $y1 -le $y0 -or $x0 -ge 1.0 -or $y0 -ge 1.0) {
        [void]$gateErrors.Add(("ROI {0} outside client {1}x{2} (design {3}x{4})" -f $d.Name, $clientW, $clientH, $DesignWidth, $DesignHeight))
        continue
    }
    $x0 = [Math]::Max(0.0, [Math]::Min(0.999, $x0))
    $y0 = [Math]::Max(0.0, [Math]::Min(0.999, $y0))
    $x1 = [Math]::Max($x0 + 0.001, [Math]::Min(1.0, $x1))
    $y1 = [Math]::Max($y0 + 0.001, [Math]::Min(1.0, $y1))
    $roiList += ,(Get-RoiFingerprint -PngPath $pngPath -X0 $x0 -Y0 $y0 -X1 $x1 -Y1 $y1 -Name ([string]$d.Name))
}
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
$sampleMetrics = [pscustomobject]@{
    parsed = $false
    status = $null
    windowLogicalWidth = $null
    windowLogicalHeight = $null
    logicalPixelWidth = $null
    logicalPixelHeight = $null
    framebufferPixelWidth = $null
    framebufferPixelHeight = $null
    contentScaleX = $null
    contentScaleY = $null
    surfacePixelWidth = $null
    surfacePixelHeight = $null
    contentScaleConsistent = $null
    contentScaleMode = $null
}
if (Test-Path -LiteralPath $stdoutPath) {
    $tail = Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8
    if ($tail -match '"status"\s*:\s*"ok"') { $productOk = $true }
    if ($tail -match '"accessibilityPublished"\s*:\s*true') { $accessibilityPublished = $true }

    $jsonLine = $null
    foreach ($line in ($tail -split "`r?`n")) {
        if ($line -match '^\s*\{' -and $line -match '"status"\s*:') {
            $jsonLine = $line.Trim()
        }
    }
    if ($null -ne $jsonLine) {
        try {
            $sampleJson = $jsonLine | ConvertFrom-Json
            $sampleMetrics.parsed = $true
            $sampleMetrics.status = [string]$sampleJson.status
            if ($null -ne $sampleJson.windowLogicalWidth) { $sampleMetrics.windowLogicalWidth = [int]$sampleJson.windowLogicalWidth }
            if ($null -ne $sampleJson.windowLogicalHeight) { $sampleMetrics.windowLogicalHeight = [int]$sampleJson.windowLogicalHeight }
            if ($null -ne $sampleJson.logicalPixelWidth) { $sampleMetrics.logicalPixelWidth = [int]$sampleJson.logicalPixelWidth }
            if ($null -ne $sampleJson.logicalPixelHeight) { $sampleMetrics.logicalPixelHeight = [int]$sampleJson.logicalPixelHeight }
            if ($null -ne $sampleJson.framebufferPixelWidth) { $sampleMetrics.framebufferPixelWidth = [int]$sampleJson.framebufferPixelWidth }
            if ($null -ne $sampleJson.framebufferPixelHeight) { $sampleMetrics.framebufferPixelHeight = [int]$sampleJson.framebufferPixelHeight }
            if ($null -ne $sampleJson.contentScaleX) { $sampleMetrics.contentScaleX = [double]$sampleJson.contentScaleX }
            if ($null -ne $sampleJson.contentScaleY) { $sampleMetrics.contentScaleY = [double]$sampleJson.contentScaleY }
            if ($null -ne $sampleJson.surfacePixelWidth) { $sampleMetrics.surfacePixelWidth = [int]$sampleJson.surfacePixelWidth }
            if ($null -ne $sampleJson.surfacePixelHeight) { $sampleMetrics.surfacePixelHeight = [int]$sampleJson.surfacePixelHeight }
        } catch {
            [void]$gateErrors.Add("sample JSON parse failed: $($_.Exception.Message)")
        }
    }
}
if (-not $productOk) {
    [void]$gateErrors.Add('sample stdout missing status=ok JSON')
}

if ($sampleMetrics.parsed) {
    if ($null -eq $sampleMetrics.contentScaleX -or $null -eq $sampleMetrics.contentScaleY -or
        $null -eq $sampleMetrics.logicalPixelWidth -or $null -eq $sampleMetrics.framebufferPixelWidth) {
        [void]$gateErrors.Add('sample JSON missing logical/framebuffer/contentScale fields (rebuild tina_sample_2d)')
    } else {
        $sx = [double]$sampleMetrics.contentScaleX
        $sy = [double]$sampleMetrics.contentScaleY
        if ($sx -lt $ContentScaleMin -or $sx -gt $ContentScaleMax -or $sy -lt $ContentScaleMin -or $sy -gt $ContentScaleMax) {
            [void]$gateErrors.Add(("contentScale out of range: x={0} y={1} (expected {2}..{3})" -f $sx, $sy, $ContentScaleMin, $ContentScaleMax))
        }
        $lw = [double]$sampleMetrics.logicalPixelWidth
        $lh = [double]$sampleMetrics.logicalPixelHeight
        $fw = [double]$sampleMetrics.framebufferPixelWidth
        $fh = [double]$sampleMetrics.framebufferPixelHeight
        # Windows GLFW may report either:
        #   (A) classic DPI: framebuffer ≈ logical * contentScale
        #   (B) already-pixel extents: framebuffer ≈ logical (contentScale is OS hint only)
        $errScaledX = [Math]::Abs($fw - ($lw * $sx))
        $errScaledY = [Math]::Abs($fh - ($lh * $sy))
        $errEqualX = [Math]::Abs($fw - $lw)
        $errEqualY = [Math]::Abs($fh - $lh)
        $modeA = ($errScaledX -le $ContentScalePixelTolerance) -and ($errScaledY -le $ContentScalePixelTolerance)
        $modeB = ($errEqualX -le $ContentScalePixelTolerance) -and ($errEqualY -le $ContentScalePixelTolerance)
        $scaleOk = $modeA -or $modeB
        $sampleMetrics.contentScaleConsistent = [bool]$scaleOk
        $sampleMetrics.contentScaleMode = if ($modeA) { 'logical_times_scale' } elseif ($modeB) { 'logical_equals_framebuffer' } else { 'inconsistent' }
        if (-not $scaleOk) {
            [void]$gateErrors.Add(("contentScale metrics inconsistent: fb={0}x{1} logical={2}x{3} scale={4}x{5} (need fb≈logical*scale or fb≈logical)" -f `
                [int]$fw, [int]$fh, [int]$lw, [int]$lh, $sx, $sy))
        }
        if ($ExpectedLogicalWidth -gt 0 -and [int]$sampleMetrics.windowLogicalWidth -ne $ExpectedLogicalWidth) {
            [void]$gateErrors.Add(("windowLogicalWidth {0} != expected {1}" -f $sampleMetrics.windowLogicalWidth, $ExpectedLogicalWidth))
        }
        if ($ExpectedLogicalHeight -gt 0 -and [int]$sampleMetrics.windowLogicalHeight -ne $ExpectedLogicalHeight) {
            [void]$gateErrors.Add(("windowLogicalHeight {0} != expected {1}" -f $sampleMetrics.windowLogicalHeight, $ExpectedLogicalHeight))
        }
        # Capture client size is typically logical (PrintWindow client); allow fb when scale≈1.
        if ($null -ne $sampleMetrics.logicalPixelWidth) {
            $matchLogical = ($clientW -eq [int]$sampleMetrics.logicalPixelWidth -and $clientH -eq [int]$sampleMetrics.logicalPixelHeight)
            $matchFb = ($clientW -eq [int]$sampleMetrics.framebufferPixelWidth -and $clientH -eq [int]$sampleMetrics.framebufferPixelHeight)
            if (-not $matchLogical -and -not $matchFb) {
                [void]$gateErrors.Add(("capture client {0}x{1} matches neither logical {2}x{3} nor framebuffer {4}x{5}" -f `
                    $clientW, $clientH,
                    [int]$sampleMetrics.logicalPixelWidth, [int]$sampleMetrics.logicalPixelHeight,
                    [int]$sampleMetrics.framebufferPixelWidth, [int]$sampleMetrics.framebufferPixelHeight))
            }
        }
    }
}

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

$baselineCompare = [pscustomobject]@{
    enabled = $false
    path = $null
    matched = $null
    avgRgbTolerance = [double]$AvgRgbTolerance
    diffs = @()
}

$resolvedBaseline = $BaselinePath
if (-not [string]::IsNullOrWhiteSpace($BaselinePath) -and -not [IO.Path]::IsPathRooted($BaselinePath)) {
    $resolvedBaseline = Join-Path $SourceRoot $BaselinePath
}

if ($WriteBaseline) {
    $baselineDir = Split-Path -Parent $resolvedBaseline
    if ($baselineDir -and -not (Test-Path -LiteralPath $baselineDir)) {
        New-Item -ItemType Directory -Path $baselineDir -Force | Out-Null
    }
    $baselineObj = [pscustomobject]@{
        schema = 2
        gate = 'UI-003-visual-roi-baseline'
        designLogicalSize = @([int]$DesignWidth, [int]$DesignHeight)
        clientSize = @([int]$clientW, [int]$clientH)
        aspect = [math]::Round($aspect, 4)
        sampleMetrics = $sampleMetrics
        rois = $roiJson
        notes = @(
            'Host-recorded ROI fingerprints for regression on same machine/class of GPU',
            'avgRgb compared with absolute channel tolerance; uniqueSampleColors is a soft floor',
            'Absolute UI layout is design-locked; per-size baselines are not OS DPI goldens'
        )
    }
    $baselineObj | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedBaseline -Encoding utf8
    Write-Host "wrote baseline: $resolvedBaseline"
}

if (-not $NoBaselineCompare -and -not [string]::IsNullOrWhiteSpace($resolvedBaseline) -and (Test-Path -LiteralPath $resolvedBaseline)) {
    $baselineCompare.enabled = $true
    $baselineCompare.path = $resolvedBaseline
    $baseline = Get-Content -LiteralPath $resolvedBaseline -Raw -Encoding UTF8 | ConvertFrom-Json
    $diffs = New-Object System.Collections.Generic.List[string]
    foreach ($roi in $roiJson) {
        $baseRoi = $null
        foreach ($b in @($baseline.rois)) {
            if ([string]$b.name -eq [string]$roi.name) { $baseRoi = $b; break }
        }
        if ($null -eq $baseRoi) {
            [void]$diffs.Add("baseline missing ROI $($roi.name)")
            continue
        }
        $baseAvg = @($baseRoi.avgRgb)
        $curAvg = @($roi.avgRgb)
        for ($i = 0; $i -lt 3; $i++) {
            $delta = [Math]::Abs([double]$curAvg[$i] - [double]$baseAvg[$i])
            if ($delta -gt $AvgRgbTolerance) {
                [void]$diffs.Add(("{0} avgRgb[{1}] delta={2:N2} (tol={3})" -f $roi.name, $i, $delta, $AvgRgbTolerance))
            }
        }
        # Soft floor: current uniqueness should not collapse vs baseline.
        $baseUnique = [int]$baseRoi.uniqueSampleColors
        $curUnique = [int]$roi.uniqueSampleColors
        if ($baseUnique -ge 4 -and $curUnique -lt [Math]::Max(2, [int][Math]::Floor($baseUnique * 0.35))) {
            [void]$diffs.Add(("{0} uniqueSampleColors collapsed {1} -> {2}" -f $roi.name, $baseUnique, $curUnique))
        }
    }
    $baselineCompare.diffs = @($diffs | ForEach-Object { [string]$_ })
    $baselineCompare.matched = ($diffs.Count -eq 0)
    if (-not $baselineCompare.matched) {
        foreach ($d in $diffs) { [void]$gateErrors.Add("baseline: $d") }
    }
}

$captureOk = $false
if ($null -ne $captureReport.ok) { $captureOk = [bool]$captureReport.ok }
$ok = ($captureExit -eq 0) -and $captureOk -and ($gateErrors.Count -eq 0)

$gateReport = [pscustomobject]@{
    schema = 2
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
    sampleMetrics = $sampleMetrics
    designLogicalSize = @([int]$DesignWidth, [int]$DesignHeight)
    rois = $roiJson
    baselineCompare = $baselineCompare
    errors = @($gateErrors | ForEach-Object { [string]$_ })
    notes = @(
        'Absolute design ROIs mapped into client capture (product-2d HUD design-locked 960x540)',
        'Proven: single-host ROI + optional baseline + GLFW contentScale consistency when fields present',
        'Open: OS Settings display scale 100/150/200% multi-DPI golden matrix (requires host scale change)',
        'blankLike frames excluded via CaptureSampleWindow usefulNonBlank / RequireNonBlank',
        'Logical --width/--height matrix is not OS DPI; larger windows leave absolute-UI margin'
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
