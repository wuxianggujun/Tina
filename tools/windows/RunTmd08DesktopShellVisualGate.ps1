#Requires -Version 5.1
<#
.SYNOPSIS
  TMD-08 Desktop Shell DPI visual gate.

.DESCRIPTION
  Captures the Desktop Shell at Dark/Light x Compact/Comfortable x
  960/1280/1600 logical widths. The gate verifies the real primary-monitor
  scale before building or launching the sample, then checks sample JSON,
  committed pane geometry, non-blank visual regions, theme/density raster
  differentials, and the icon resource lifecycle.

  Run once at 100% and once at 150%. Pass the first report to the second run
  with -PeerReportPath to verify that both reports use the same executable and
  publish identical logical geometry. This is a same-machine/backend DPI gate,
  not a cross-GPU golden.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(100, 150, 200)]
    [int]$ExpectedScalePercent,
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [ValidateSet('windows-vnext-bgfx-ui-freetype-debug', 'windows-vnext-bgfx-product-2d-debug')]
    [string]$BuildPreset = 'windows-vnext-bgfx-ui-freetype-debug',
    [string]$OutDir = 'artifacts/screenshots/tmd-08-desktop-shell',
    [int]$Frames = 120,
    [int]$FrameDelayMs = 16,
    [int]$WarmupMs = 800,
    [int]$CaptureCount = 3,
    [int]$WindowHeight = 800,
    [double]$ContentScaleTolerance = 0.02,
    [double]$MinimumThemeChannelDelta = 8.0,
    [double]$MinimumDensityMeanDelta = 0.35,
    [double]$GeometryTolerance = 0.01,
    [switch]$SkipBuild,
    [string]$PeerReportPath = '',
    [string]$OutJson = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if ($Frames -lt 65) { throw 'Frames must be at least 65 for the complete automated workflow' }
if ($FrameDelayMs -lt 1) { throw 'FrameDelayMs must be positive so captures occur before sample exit' }
if ($CaptureCount -lt 2) { throw 'CaptureCount must be at least 2' }
if ($WindowHeight -lt 640 -or $WindowHeight -gt 2160) { throw 'WindowHeight must be in the range 640..2160' }
if ($ContentScaleTolerance -lt 0.0) { throw 'ContentScaleTolerance must be non-negative' }
if ($MinimumThemeChannelDelta -le 0.0) { throw 'MinimumThemeChannelDelta must be positive' }
if ($MinimumDensityMeanDelta -le 0.0) { throw 'MinimumDensityMeanDelta must be positive' }
if ($GeometryTolerance -lt 0.0) { throw 'GeometryTolerance must be non-negative' }

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
}
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
Set-Location -LiteralPath $SourceRoot

$buildRootByPreset = @{
    'windows-vnext-bgfx-ui-freetype-debug' = 'out\build\windows-msvc-vnext-bgfx-ui-freetype'
    'windows-vnext-bgfx-product-2d-debug' = 'out\build\windows-msvc-vnext-bgfx-product-2d'
}
$expectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $SourceRoot $buildRootByPreset[$BuildPreset]))
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $expectedBuildRoot 'bin\Debug\tina_sample_desktop_shell.exe'
} elseif (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $SourceRoot $Exe
}
$Exe = [IO.Path]::GetFullPath($Exe)

if (-not ('TinaDesktopShellDpiProbe' -as [type])) {
    Add-Type @"
using System.Runtime.InteropServices;
public static class TinaDesktopShellDpiProbe {
    [DllImport("Shcore.dll")]
    public static extern int GetScaleFactorForDevice(int deviceType);
}
"@
}

$actualScalePercent = [TinaDesktopShellDpiProbe]::GetScaleFactorForDevice(0)
if ($actualScalePercent -ne $ExpectedScalePercent) {
    throw ("Primary monitor scale is {0}%, expected {1}%. No build or capture was started." -f `
        $actualScalePercent, $ExpectedScalePercent)
}

if (-not $SkipBuild) {
    & cmake --build --preset $BuildPreset --target tina_sample_desktop_shell --parallel 2 -- /nr:false
    if ($LASTEXITCODE -ne 0) {
        throw "tina_sample_desktop_shell build failed exit=$LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "missing executable: $Exe"
}

$exeSha256 = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()

function Resolve-UiFontFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$BuildRoot
    )

    $candidates = @()
    $cachePath = Join-Path $BuildRoot 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        foreach ($line in (Get-Content -LiteralPath $cachePath -Encoding utf8)) {
            if ($line -match '^TINA_UI_FONT_PATH:FILEPATH=(.+)$' -and
                -not [string]::IsNullOrWhiteSpace([string]$Matches[1])) {
                $candidates += [pscustomobject]@{ path = [string]$Matches[1]; source = 'CMakeCache' }
                break
            }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:TINA_UI_FONT_PATH)) {
        $candidates += [pscustomobject]@{ path = [string]$env:TINA_UI_FONT_PATH; source = 'env:TINA_UI_FONT_PATH' }
    }
    $candidates += [pscustomobject]@{
        path = Join-Path $Root 'resources\fonts\SourceHanSansSC-Regular.otf'
        source = 'repo-fixture'
    }

    foreach ($candidate in $candidates) {
        $path = [string]$candidate.path
        if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $Root $path }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $resolved = (Resolve-Path -LiteralPath $path).Path
        $item = Get-Item -LiteralPath $resolved
        return [pscustomobject]@{
            source = [string]$candidate.source
            path = $resolved
            fileName = $item.Name
            sizeBytes = [int64]$item.Length
            sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    throw 'No UI font resolved; TMD-08 visual evidence requires the FreeType product graph and a real OTF/TTF'
}

$fontFingerprint = Resolve-UiFontFingerprint -Root $SourceRoot -BuildRoot $expectedBuildRoot
$gitCommit = (& git rev-parse HEAD 2>$null | Select-Object -First 1)
$gitStatus = @(& git status --short 2>$null)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ([IO.Path]::IsPathRooted($OutDir)) {
    $runRoot = Join-Path $OutDir ("{0}pct-{1}" -f $ExpectedScalePercent, $stamp)
} else {
    $runRoot = Join-Path $SourceRoot (Join-Path $OutDir ("{0}pct-{1}" -f $ExpectedScalePercent, $stamp))
}
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$captureScript = Join-Path $SourceRoot 'tools\windows\CaptureSampleWindow.ps1'
$themes = @('dark', 'light')
$densities = @('compact', 'comfortable')
$widths = @(960, 1280, 1600)

function Get-LastSampleJson {
    param([Parameter(Mandatory = $true)][string]$StdoutPath)

    if (-not (Test-Path -LiteralPath $StdoutPath -PathType Leaf)) { return $null }
    $result = $null
    foreach ($line in (Get-Content -LiteralPath $StdoutPath -Encoding utf8)) {
        if ($line -notmatch '^\s*\{') { continue }
        try {
            $candidate = $line | ConvertFrom-Json
            if ($candidate.PSObject.Properties.Name -contains 'sample' -and
                [string]$candidate.sample -eq 'tina_sample_desktop_shell') {
                $result = $candidate
            }
        } catch {
        }
    }
    return $result
}

function Get-UsefulPngPath {
    param([Parameter(Mandatory = $true)]$CaptureReport)

    if ($CaptureReport.PSObject.Properties.Name -contains 'consecutiveUsefulSameSizeGate' -and
        $null -ne $CaptureReport.consecutiveUsefulSameSizeGate -and
        $CaptureReport.consecutiveUsefulSameSizeGate.paths) {
        $path = [string]$CaptureReport.consecutiveUsefulSameSizeGate.paths[-1]
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
    if ($CaptureReport.PSObject.Properties.Name -contains 'captures') {
        $useful = @($CaptureReport.captures | Where-Object { $_.usefulNonBlank -eq $true })
        if ($useful.Count -gt 0) {
            $path = [string]$useful[-1].path
            if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
        }
    }
    return $null
}

function Get-RegionStatistics {
    param(
        [Parameter(Mandatory = $true)][string]$PngPath,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][double[]]$NormalizedRect
    )

    $image = [Drawing.Bitmap]::FromFile($PngPath)
    try {
        $left = [Math]::Max(0, [int][Math]::Floor($NormalizedRect[0] * $image.Width))
        $top = [Math]::Max(0, [int][Math]::Floor($NormalizedRect[1] * $image.Height))
        $right = [Math]::Min($image.Width, [int][Math]::Ceiling($NormalizedRect[2] * $image.Width))
        $bottom = [Math]::Min($image.Height, [int][Math]::Ceiling($NormalizedRect[3] * $image.Height))
        if ($right -le $left -or $bottom -le $top) { throw "empty visual region: $Name" }

        $stepX = [Math]::Max(1, [int][Math]::Floor(($right - $left) / 36.0))
        $stepY = [Math]::Max(1, [int][Math]::Floor(($bottom - $top) / 28.0))
        [int64]$sumR = 0
        [int64]$sumG = 0
        [int64]$sumB = 0
        $count = 0
        $histogram = @{}
        for ($y = $top; $y -lt $bottom; $y += $stepY) {
            for ($x = $left; $x -lt $right; $x += $stepX) {
                $color = $image.GetPixel($x, $y)
                $sumR += $color.R
                $sumG += $color.G
                $sumB += $color.B
                $key = '{0:X2}{1:X2}{2:X2}' -f $color.R, $color.G, $color.B
                if ($histogram.ContainsKey($key)) { $histogram[$key]++ } else { $histogram[$key] = 1 }
                $count++
            }
        }
        $dominant = $histogram.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1
        return [pscustomobject]@{
            name = $Name
            left = $left
            top = $top
            width = $right - $left
            height = $bottom - $top
            samples = $count
            uniqueColors = $histogram.Count
            dominantRatio = [Math]::Round($dominant.Value / [double]$count, 4)
            avgRgb = @(
                [Math]::Round($sumR / [double]$count, 2),
                [Math]::Round($sumG / [double]$count, 2),
                [Math]::Round($sumB / [double]$count, 2)
            )
        }
    } finally {
        $image.Dispose()
    }
}

function Get-NormalizedImageMeanDelta {
    param(
        [Parameter(Mandatory = $true)][string]$FirstPath,
        [Parameter(Mandatory = $true)][string]$SecondPath
    )

    $first = [Drawing.Bitmap]::FromFile($FirstPath)
    $second = [Drawing.Bitmap]::FromFile($SecondPath)
    try {
        [double]$sum = 0.0
        $count = 0
        for ($row = 0; $row -lt 32; ++$row) {
            $firstY = [Math]::Min($first.Height - 1, [int](($row + 0.5) * $first.Height / 32.0))
            $secondY = [Math]::Min($second.Height - 1, [int](($row + 0.5) * $second.Height / 32.0))
            for ($column = 0; $column -lt 48; ++$column) {
                $firstX = [Math]::Min($first.Width - 1, [int](($column + 0.5) * $first.Width / 48.0))
                $secondX = [Math]::Min($second.Width - 1, [int](($column + 0.5) * $second.Width / 48.0))
                $a = $first.GetPixel($firstX, $firstY)
                $b = $second.GetPixel($secondX, $secondY)
                $sum += [Math]::Abs([int]$a.R - [int]$b.R)
                $sum += [Math]::Abs([int]$a.G - [int]$b.G)
                $sum += [Math]::Abs([int]$a.B - [int]$b.B)
                $count += 3
            }
        }
        return [Math]::Round($sum / [double]$count, 4)
    } finally {
        $first.Dispose()
        $second.Dispose()
    }
}

function Add-CaseError {
    param(
        [Parameter(Mandatory = $true)][System.Collections.Generic.List[string]]$Errors,
        [Parameter(Mandatory = $true)][string]$Message
    )
    [void]$Errors.Add($Message)
}

function Invoke-ShellCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Theme,
        [Parameter(Mandatory = $true)][string]$Density,
        [Parameter(Mandatory = $true)][int]$Width
    )

    $key = '{0}-{1}-{2}' -f $Theme, $Density, $Width
    $caseRoot = Join-Path $runRoot $key
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $arguments = "--frames=$Frames --frame-delay-ms=$FrameDelayMs --auto-demo --theme=$Theme --density=$Density --width=$Width --height=$WindowHeight"

    & powershell -NoProfile -ExecutionPolicy Bypass -File $captureScript `
        -Exe $Exe `
        -ArgString $arguments `
        -OutDir $caseRoot `
        -WarmupMs $WarmupMs `
        -CaptureCount $CaptureCount `
        -CaptureIntervalMs 160 `
        -TimeoutMs 60000 `
        -RequiredConsecutiveUsefulCaptures 2 `
        -RequireNonBlank
    $captureExit = $LASTEXITCODE

    $reportFile = Get-ChildItem -LiteralPath $caseRoot -Recurse -Filter report.json |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $reportFile) { throw "missing capture report for $key" }
    $captureReport = Get-Content -LiteralPath $reportFile.FullName -Raw -Encoding utf8 | ConvertFrom-Json
    $pngPath = Get-UsefulPngPath -CaptureReport $captureReport
    $stdoutPath = Join-Path (Split-Path -Parent $reportFile.FullName) 'stdout.txt'
    $sample = Get-LastSampleJson -StdoutPath $stdoutPath
    $errors = New-Object System.Collections.Generic.List[string]

    if ($captureExit -ne 0 -or -not [bool]$captureReport.ok) {
        Add-CaseError -Errors $errors -Message "capture failed exit=$captureExit"
    }
    if ([string]::IsNullOrWhiteSpace($pngPath)) {
        Add-CaseError -Errors $errors -Message 'no useful non-blank PNG'
    }
    if ($null -eq $sample) {
        Add-CaseError -Errors $errors -Message 'missing Desktop Shell status JSON'
    } else {
        if ([string]$sample.status -ne 'ok') { Add-CaseError -Errors $errors -Message 'sample status is not ok' }
        if ([string]$sample.theme -ne $Theme) { Add-CaseError -Errors $errors -Message 'sample theme mismatch' }
        if ([string]$sample.density -ne $Density) { Add-CaseError -Errors $errors -Message 'sample density mismatch' }
        if ([int]$sample.logicalPixelWidth -ne $Width -or [int]$sample.logicalPixelHeight -ne $WindowHeight) {
            Add-CaseError -Errors $errors -Message ("logical client extent {0}x{1} != requested {2}x{3}" -f `
                $sample.logicalPixelWidth, $sample.logicalPixelHeight, $Width, $WindowHeight)
        }
        $expectedScale = $ExpectedScalePercent / 100.0
        if ([Math]::Abs([double]$sample.contentScaleX - $expectedScale) -gt $ContentScaleTolerance -or
            [Math]::Abs([double]$sample.contentScaleY - $expectedScale) -gt $ContentScaleTolerance) {
            Add-CaseError -Errors $errors -Message ("content scale {0}x{1} != expected {2}" -f `
                $sample.contentScaleX, $sample.contentScaleY, $expectedScale)
        }
        $scaledWidth = [double]$sample.logicalPixelWidth * [double]$sample.contentScaleX
        $scaledHeight = [double]$sample.logicalPixelHeight * [double]$sample.contentScaleY
        $framebufferMatchesLogical =
            [Math]::Abs([double]$sample.framebufferPixelWidth - [double]$sample.logicalPixelWidth) -le 2.0 -and
            [Math]::Abs([double]$sample.framebufferPixelHeight - [double]$sample.logicalPixelHeight) -le 2.0
        $framebufferMatchesScaled =
            [Math]::Abs([double]$sample.framebufferPixelWidth - $scaledWidth) -le 2.0 -and
            [Math]::Abs([double]$sample.framebufferPixelHeight - $scaledHeight) -le 2.0
        if (-not $framebufferMatchesLogical -and -not $framebufferMatchesScaled) {
            Add-CaseError -Errors $errors -Message 'framebuffer extent matches neither logical nor logical x contentScale'
        }
        $expectedTier = $(if ($Width -ge 1280) { 'full' } else { 'compressed' })
        if ([string]$sample.tier -ne $expectedTier) { Add-CaseError -Errors $errors -Message 'responsive tier mismatch' }
        if (-not [bool]$sample.viewportUnobstructed -or [double]$sample.viewportWidth -lt 480.0 -or
            [double]$sample.viewportHeight -lt 320.0) {
            Add-CaseError -Errors $errors -Message 'viewport is obstructed or below its minimum extent'
        }
        if (-not [bool]$sample.autoDemo -or -not [bool]$sample.initialFocusApplied -or
            -not [bool]$sample.menuOpenObserved -or -not [bool]$sample.dialogOpenObserved -or
            -not [bool]$sample.dialogDismissed -or -not [bool]$sample.tooltipOpenObserved -or
            -not [bool]$sample.tooltipDismissed -or -not [bool]$sample.tooltipWithinMaxWidth -or
            -not [bool]$sample.splitterMovedGeometry -or -not [bool]$sample.splitterMinimumClamped) {
            Add-CaseError -Errors $errors -Message 'automated focus/overlay/splitter workflow is incomplete'
        }
        if (-not [bool]$sample.iconAtlasUploaded -or -not [bool]$sample.iconAtlasReleased -or
            [int64]$sample.iconResolverCalls -le 0 -or
            [int64]$sample.iconResolverHits -ne [int64]$sample.iconResolverCalls) {
            Add-CaseError -Errors $errors -Message 'icon atlas resolve/release lifecycle is incomplete'
        }
    }

    $regions = @()
    if (-not [string]::IsNullOrWhiteSpace($pngPath)) {
        $regionSpecs = @(
            @('commandBar', 0.00, 0.00, 1.00, 0.12, 4),
            @('workspace', 0.00, 0.14, 1.00, 0.95, 6),
            @('leftDock', 0.00, 0.16, 0.22, 0.84, 3),
            @('viewport', 0.25, 0.18, 0.68, 0.72, 2),
            @('inspector', 0.76, 0.16, 1.00, 0.84, 3),
            @('statusBar', 0.00, 0.96, 1.00, 1.00, 2)
        )
        foreach ($spec in $regionSpecs) {
            $stats = Get-RegionStatistics -PngPath $pngPath -Name ([string]$spec[0]) `
                -NormalizedRect @([double]$spec[1], [double]$spec[2], [double]$spec[3], [double]$spec[4])
            $regions += $stats
            if ([int]$stats.uniqueColors -lt [int]$spec[5] -or [double]$stats.dominantRatio -ge 0.998) {
                Add-CaseError -Errors $errors -Message ("visual region {0} lacks content diversity: colors={1} dominant={2}" -f `
                    $stats.name, $stats.uniqueColors, $stats.dominantRatio)
            }
        }
    }

    return [pscustomobject]@{
        key = $key
        theme = $Theme
        density = $Density
        requestedWidth = $Width
        requestedHeight = $WindowHeight
        passed = $errors.Count -eq 0
        errors = @($errors | ForEach-Object { [string]$_ })
        captureReport = $reportFile.FullName
        png = $pngPath
        sample = $sample
        regions = $regions
    }
}

$cases = @()
$gateErrors = New-Object System.Collections.Generic.List[string]
foreach ($theme in $themes) {
    foreach ($density in $densities) {
        foreach ($width in $widths) {
            Write-Host ("=== TMD-08 capture theme={0} density={1} width={2} scale={3}% ===" -f `
                $theme, $density, $width, $ExpectedScalePercent)
            try {
                $case = Invoke-ShellCapture -Theme $theme -Density $density -Width $width
                $cases += $case
                if (-not [bool]$case.passed) {
                    [void]$gateErrors.Add(("case {0}: {1}" -f $case.key, ($case.errors -join '; ')))
                }
            } catch {
                $message = "case $theme-$density-$width threw: $($_.Exception.Message)"
                [void]$gateErrors.Add($message)
                $cases += [pscustomobject]@{
                    key = "$theme-$density-$width"
                    theme = $theme
                    density = $density
                    requestedWidth = $width
                    requestedHeight = $WindowHeight
                    passed = $false
                    errors = @($message)
                    captureReport = $null
                    png = $null
                    sample = $null
                    regions = @()
                }
            }
        }
    }
}

function Find-Case {
    param(
        [Parameter(Mandatory = $true)]$CaseSet,
        [Parameter(Mandatory = $true)][string]$Theme,
        [Parameter(Mandatory = $true)][string]$Density,
        [Parameter(Mandatory = $true)][int]$Width
    )
    return @($CaseSet | Where-Object {
            [string]$_.theme -eq $Theme -and [string]$_.density -eq $Density -and
            [int]$_.requestedWidth -eq $Width
        } | Select-Object -First 1)[0]
}

$themeComparisons = @()
foreach ($density in $densities) {
    foreach ($width in $widths) {
        $dark = Find-Case -CaseSet $cases -Theme 'dark' -Density $density -Width $width
        $light = Find-Case -CaseSet $cases -Theme 'light' -Density $density -Width $width
        if ($null -eq $dark -or $null -eq $light -or
            [string]::IsNullOrWhiteSpace([string]$dark.png) -or
            [string]::IsNullOrWhiteSpace([string]$light.png)) {
            [void]$gateErrors.Add("theme comparison missing PNG for density=$density width=$width")
            continue
        }
        $delta = Get-NormalizedImageMeanDelta -FirstPath $dark.png -SecondPath $light.png
        $passed = $delta -ge $MinimumThemeChannelDelta
        if (-not $passed) {
            [void]$gateErrors.Add("theme raster delta $delta < $MinimumThemeChannelDelta for density=$density width=$width")
        }
        $themeComparisons += [pscustomobject]@{
            density = $density
            width = $width
            meanChannelDelta = $delta
            minimum = $MinimumThemeChannelDelta
            passed = $passed
        }
    }
}

$densityComparisons = @()
foreach ($theme in $themes) {
    foreach ($width in $widths) {
        $compact = Find-Case -CaseSet $cases -Theme $theme -Density 'compact' -Width $width
        $comfortable = Find-Case -CaseSet $cases -Theme $theme -Density 'comfortable' -Width $width
        if ($null -eq $compact -or $null -eq $comfortable -or
            [string]::IsNullOrWhiteSpace([string]$compact.png) -or
            [string]::IsNullOrWhiteSpace([string]$comfortable.png)) {
            [void]$gateErrors.Add("density comparison missing PNG for theme=$theme width=$width")
            continue
        }
        $delta = Get-NormalizedImageMeanDelta -FirstPath $compact.png -SecondPath $comfortable.png
        $passed = $delta -ge $MinimumDensityMeanDelta
        if (-not $passed) {
            [void]$gateErrors.Add("density raster delta $delta < $MinimumDensityMeanDelta for theme=$theme width=$width")
        }
        $densityComparisons += [pscustomobject]@{
            theme = $theme
            width = $width
            meanChannelDelta = $delta
            minimum = $MinimumDensityMeanDelta
            passed = $passed
        }
    }
}

$peerComparison = $null
if (-not [string]::IsNullOrWhiteSpace($PeerReportPath)) {
    if (-not [IO.Path]::IsPathRooted($PeerReportPath)) { $PeerReportPath = Join-Path $SourceRoot $PeerReportPath }
    $PeerReportPath = (Resolve-Path -LiteralPath $PeerReportPath).Path
    $peer = Get-Content -LiteralPath $PeerReportPath -Raw -Encoding utf8 | ConvertFrom-Json
    $peerErrors = New-Object System.Collections.Generic.List[string]
    if ([string]$peer.gate -ne 'tmd-08-desktop-shell-visual') { [void]$peerErrors.Add('peer gate identity mismatch') }
    if ([string]$peer.status -ne 'ok') { [void]$peerErrors.Add('peer report status is not ok') }
    if ([string]$peer.exeSha256 -ne $exeSha256) { [void]$peerErrors.Add('peer executable SHA-256 mismatch') }
    if ($null -eq $peer.fontFingerprint -or
        [string]$peer.fontFingerprint.sha256 -ne [string]$fontFingerprint.sha256) {
        [void]$peerErrors.Add('peer font SHA-256 mismatch')
    }
    $scalePair = @([int]$peer.actualScalePercent, $ExpectedScalePercent) | Sort-Object
    if ($scalePair.Count -ne 2 -or $scalePair[0] -ne 100 -or $scalePair[1] -ne 150) {
        [void]$peerErrors.Add('peer/current scale pair must be exactly 100% and 150%')
    }
    $geometryProperties = @('logicalPixelWidth', 'logicalPixelHeight', 'leftDockWidth', 'viewportWidth',
                            'viewportHeight', 'inspectorWidth', 'timelineHeight', 'statusBarHeight')
    foreach ($case in $cases) {
        $peerCase = Find-Case -CaseSet $peer.cases -Theme $case.theme -Density $case.density `
            -Width ([int]$case.requestedWidth)
        if ($null -eq $peerCase -or $null -eq $peerCase.sample -or $null -eq $case.sample) {
            [void]$peerErrors.Add("missing peer geometry case $($case.key)")
            continue
        }
        foreach ($property in $geometryProperties) {
            $currentValue = [double]$case.sample.$property
            $peerValue = [double]$peerCase.sample.$property
            if ([Math]::Abs($currentValue - $peerValue) -gt $GeometryTolerance) {
                [void]$peerErrors.Add(("geometry mismatch {0}.{1}: peer={2} current={3}" -f `
                    $case.key, $property, $peerValue, $currentValue))
            }
        }
    }
    if ($peerErrors.Count -gt 0) {
        foreach ($errorMessage in $peerErrors) { [void]$gateErrors.Add("peer: $errorMessage") }
    }
    $peerComparison = [pscustomobject]@{
        report = $PeerReportPath
        peerScalePercent = [int]$peer.actualScalePercent
        sameExecutable = [string]$peer.exeSha256 -eq $exeSha256
        sameFont = $null -ne $peer.fontFingerprint -and
                   [string]$peer.fontFingerprint.sha256 -eq [string]$fontFingerprint.sha256
        geometryTolerance = $GeometryTolerance
        passed = $peerErrors.Count -eq 0
        errors = @($peerErrors | ForEach-Object { [string]$_ })
    }
}

$passed = $gateErrors.Count -eq 0
$result = [ordered]@{
    schema = 1
    status = $(if ($passed) { 'ok' } else { 'fail' })
    gate = 'tmd-08-desktop-shell-visual'
    sample = 'tina_sample_desktop_shell'
    timestamp = (Get-Date).ToString('o')
    expectedScalePercent = $ExpectedScalePercent
    actualScalePercent = $actualScalePercent
    buildPreset = $BuildPreset
    exe = $Exe
    exeSha256 = $exeSha256
    fontFingerprint = $fontFingerprint
    gitCommit = [string]$gitCommit
    gitStatus = @($gitStatus | ForEach-Object { [string]$_ })
    windowHeight = $WindowHeight
    widths = $widths
    themes = $themes
    densities = $densities
    cases = $cases
    themeComparisons = $themeComparisons
    densityComparisons = $densityComparisons
    peerComparison = $peerComparison
    errors = @($gateErrors | ForEach-Object { [string]$_ })
    note = 'Same-machine/backend real-DPI evidence. This report does not claim mixed-monitor or cross-GPU coverage.'
}

if ([string]::IsNullOrWhiteSpace($OutJson)) {
    $OutJson = Join-Path $runRoot ("tmd-08-desktop-shell-visual-{0}pct.json" -f $ExpectedScalePercent)
} elseif (-not [IO.Path]::IsPathRooted($OutJson)) {
    $OutJson = Join-Path $SourceRoot $OutJson
}
$outJsonParent = Split-Path -Parent $OutJson
if (-not [string]::IsNullOrWhiteSpace($outJsonParent) -and
    -not (Test-Path -LiteralPath $outJsonParent -PathType Container)) {
    New-Item -ItemType Directory -Path $outJsonParent -Force | Out-Null
}
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutJson -Encoding utf8
Write-Host ("TMD-08 Desktop Shell visual gate status={0} scale={1}% cases={2} json={3}" -f `
    $result.status, $actualScalePercent, $cases.Count, $OutJson)

if (-not $passed) {
    throw ("TMD-08 Desktop Shell visual gate failed: {0}" -f ($result.errors -join '; '))
}

exit 0
