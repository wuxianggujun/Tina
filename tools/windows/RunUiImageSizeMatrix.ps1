#Requires -Version 5.1
<#
.SYNOPSIS
  UI-IMAGE-001 Dark/Light content-scale-like size matrix for tina_sample_ui_showcase.

.DESCRIPTION
  Captures three logical client footprints at 1.0x, 1.25x, and 1.5x for both
  product themes. Each case requires two consecutive useful captures with the
  same PNG hash and validates the sample's image/resolver/render/metrics JSON.

  This does not change Windows display scale. True OS DPI and mixed-monitor
  evidence remains tracked by UI-003.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [switch]$SkipBuild,
    [string]$OutDir = 'artifacts/screenshots/ui-image-size-matrix'
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
}
Set-Location -LiteralPath $SourceRoot

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $SourceRoot `
        'out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe'
}
$captureScript = Join-Path $SourceRoot 'tools\windows\CaptureSampleWindow.ps1'
if (-not (Test-Path -LiteralPath $captureScript)) {
    throw "missing $captureScript"
}

if (-not $SkipBuild) {
    Write-Host '=== build tina_sample_ui_showcase ==='
    & cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
        --target tina_sample_ui_showcase --parallel 2 -- /nr:false
    if ($LASTEXITCODE -ne 0) {
        throw "tina_sample_ui_showcase build failed exit=$LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "missing executable: $Exe"
}

$sizes = @(
    [pscustomobject]@{ Label = 'design-1x'; Width = 1280; Height = 980; ScaleLike = 1.0 },
    [pscustomobject]@{ Label = 'scale-like-1.25x'; Width = 1600; Height = 1225; ScaleLike = 1.25 },
    [pscustomobject]@{ Label = 'scale-like-1.5x'; Width = 1920; Height = 1470; ScaleLike = 1.5 }
)
$themes = @('dark', 'light')
$sampleFrames = 120
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$matrixRoot = if ([IO.Path]::IsPathRooted($OutDir)) {
    Join-Path $OutDir $stamp
} else {
    Join-Path $SourceRoot (Join-Path $OutDir $stamp)
}
New-Item -ItemType Directory -Path $matrixRoot -Force | Out-Null

function Test-ContentScaleConsistency {
    param([object]$Sample)

    $logicalWidth = [double]$Sample.logicalPixelWidth
    $logicalHeight = [double]$Sample.logicalPixelHeight
    $framebufferWidth = [double]$Sample.framebufferPixelWidth
    $framebufferHeight = [double]$Sample.framebufferPixelHeight
    $scaleX = [double]$Sample.contentScaleX
    $scaleY = [double]$Sample.contentScaleY
    if ($logicalWidth -lt 1 -or $logicalHeight -lt 1 -or $framebufferWidth -lt 1 -or
        $framebufferHeight -lt 1 -or $scaleX -lt 0.5 -or $scaleX -gt 4.0 -or
        $scaleY -lt 0.5 -or $scaleY -gt 4.0) {
        return $false
    }
    $scaled = [Math]::Abs($framebufferWidth - $logicalWidth * $scaleX) -le 2.0 -and
        [Math]::Abs($framebufferHeight - $logicalHeight * $scaleY) -le 2.0
    $equal = [Math]::Abs($framebufferWidth - $logicalWidth) -le 2.0 -and
        [Math]::Abs($framebufferHeight - $logicalHeight) -le 2.0
    return $scaled -or $equal
}

function Get-ConsecutiveStableHash {
    param([object[]]$Captures)

    $previousHash = $null
    $previousPath = $null
    foreach ($capture in $Captures) {
        if (-not [bool]$capture.usefulNonBlank) {
            $previousHash = $null
            $previousPath = $null
            continue
        }
        $path = [string]$capture.path
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($null -ne $previousHash -and $hash -eq $previousHash) {
            return [pscustomobject]@{
                Passed = $true
                Hash = $hash
                Paths = @($previousPath, $path)
            }
        }
        $previousHash = $hash
        $previousPath = $path
    }
    return [pscustomobject]@{ Passed = $false; Hash = $null; Paths = @() }
}

$results = @()
$failedCases = 0
foreach ($size in $sizes) {
    foreach ($theme in $themes) {
        $label = "{0}-{1}" -f $size.Label, $theme
        $caseRoot = Join-Path $matrixRoot $label
        $args = "--frames=$sampleFrames --frame-delay-ms=16 --theme=$theme --width=$($size.Width) --height=$($size.Height)"
        Write-Host "=== UI image size case $label ==="

        & powershell -NoProfile -ExecutionPolicy Bypass -File $captureScript `
            -Exe $Exe `
            -ArgString $args `
            -OutDir $caseRoot `
            -WarmupMs 500 `
            -CaptureCount 3 `
            -CaptureIntervalMs 250 `
            -RequiredConsecutiveUsefulCaptures 2 `
            -RequireNonBlank
        $captureExit = if ($null -eq $LASTEXITCODE) { 1 } else { [int]$LASTEXITCODE }

        $reportFile = Get-ChildItem -LiteralPath $caseRoot -Recurse -Filter report.json -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        $errors = New-Object System.Collections.Generic.List[string]
        $report = $null
        $sample = $null
        $stable = [pscustomobject]@{ Passed = $false; Hash = $null; Paths = @() }
        if ($captureExit -ne 0) {
            [void]$errors.Add("CaptureSampleWindow exit=$captureExit")
        }
        if ($null -eq $reportFile) {
            [void]$errors.Add('capture report.json missing')
        } else {
            $report = Get-Content -LiteralPath $reportFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            $stable = Get-ConsecutiveStableHash -Captures @($report.captures)
            if (-not [bool]$stable.Passed) {
                [void]$errors.Add('no consecutive useful captures had the same SHA-256')
            }
            $stdoutPath = Join-Path ([string]$report.outDir) 'stdout.txt'
            if (-not (Test-Path -LiteralPath $stdoutPath)) {
                [void]$errors.Add('sample stdout.txt missing')
            } else {
                $jsonLine = Get-Content -LiteralPath $stdoutPath -Encoding UTF8 |
                    Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_ui_showcase"' } |
                    Select-Object -Last 1
                if ($null -eq $jsonLine) {
                    [void]$errors.Add('sample status=ok JSON missing')
                } else {
                    $sample = $jsonLine | ConvertFrom-Json
                }
            }
        }

        if ($null -ne $sample) {
            if ([int]$sample.frames -ne $sampleFrames -or [int]$sample.targetFrames -ne $sampleFrames -or
                [string]$sample.initialTheme -ne $theme -or [string]$sample.finalTheme -ne $theme) {
                [void]$errors.Add('sample frame/theme evidence does not match the requested case')
            }
            if ([int]$sample.logicalPixelWidth -ne [int]$size.Width -or
                [int]$sample.logicalPixelHeight -ne [int]$size.Height) {
                [void]$errors.Add('sample logical extent does not match the requested case')
            }
            if (-not (Test-ContentScaleConsistency -Sample $sample)) {
                [void]$errors.Add('logical/framebuffer/contentScale metrics are inconsistent')
            }
            $usefulCaptures = @($report.captures | Where-Object { [bool]$_.usefulNonBlank })
            foreach ($capture in $usefulCaptures) {
                $matchesLogical = [int]$capture.width -eq [int]$sample.logicalPixelWidth -and
                    [int]$capture.height -eq [int]$sample.logicalPixelHeight
                $matchesFramebuffer = [int]$capture.width -eq [int]$sample.framebufferPixelWidth -and
                    [int]$capture.height -eq [int]$sample.framebufferPixelHeight
                if (-not $matchesLogical -and -not $matchesFramebuffer) {
                    [void]$errors.Add('useful capture size matches neither logical nor framebuffer extent')
                    break
                }
            }
            if ([int]$sample.imageProducts -ne 4 -or [int]$sample.imageFrames -ne $sampleFrames -or
                [int]$sample.imageFreeFrames -ne 0 -or [int]$sample.imageResolverCalls -ne $sampleFrames -or
                [int]$sample.imageResolverHits -ne $sampleFrames -or [int]$sample.imageResolverUnavailable -ne 0 -or
                [int]$sample.maxImageQuads -ne 12 -or [int]$sample.maxImageBatches -ne 4 -or
                [int]$sample.maxUniqueImageResources -ne 1) {
                [void]$errors.Add('image command/resolver evidence does not match the frozen showcase inventory')
            }
            if (-not [bool]$sample.imageAtlasUploaded -or -not [bool]$sample.imageAtlasReleased -or
                -not [bool]$sample.imageAtlasInvalidated -or [int]$sample.imageFrameBorrowsAtRelease -ne 0 -or
                -not [bool]$sample.imageLinear -or -not [bool]$sample.imageNearest -or
                [uint64]$sample.paintOrderChecksum -eq 0) {
                [void]$errors.Add('image resource/sampling/checksum evidence is incomplete')
            }
        }

        $ok = $errors.Count -eq 0
        if (-not $ok) {
            ++$failedCases
        }
        $results += [pscustomobject]@{
            label = $label
            theme = $theme
            scaleLike = [double]$size.ScaleLike
            requestedLogical = @([int]$size.Width, [int]$size.Height)
            ok = [bool]$ok
            errors = @($errors)
            stableCapture = [pscustomobject]@{
                passed = [bool]$stable.Passed
                sha256 = $stable.Hash
                paths = @($stable.Paths)
            }
            captureReport = if ($null -ne $reportFile) { [string]$reportFile.FullName } else { $null }
            sample = $sample
        }
    }
}

$matrixOk = $failedCases -eq 0
$matrixReport = [pscustomobject]@{
    schema = 1
    gate = 'UI-IMAGE-001-size-matrix'
    ok = [bool]$matrixOk
    tip = [string](git rev-parse HEAD 2>$null)
    cases = $results
    proven = @(
        'Dark/Light product capture at 1280x980, 1600x1225, and 1920x1470 logical client footprints',
        'Two consecutive useful captures with an identical SHA-256 per case',
        'Showcase image inventory, resolver dedupe, sampling, atlas lifetime, pin release, and checksum invariants',
        'GLFW logical/framebuffer/contentScale consistency in logical-times-scale or logical-equals-framebuffer mode'
    )
    open = @(
        'OS Settings display scale 100/150/200% true DPI matrix',
        'Multi-monitor mixed-DPI capture matrix',
        'Cross-GPU pixel golden'
    )
}
$matrixPath = Join-Path $matrixRoot 'matrix-report.json'
$matrixReport | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $matrixPath -Encoding utf8
$summaryDir = Join-Path $SourceRoot 'artifacts\gates'
New-Item -ItemType Directory -Path $summaryDir -Force | Out-Null
$summaryPath = Join-Path $summaryDir ("ui-image-size-matrix-{0}.json" -f $stamp)
Copy-Item -LiteralPath $matrixPath -Destination $summaryPath -Force

Write-Host "matrix report: $matrixPath"
Write-Host "summary: $summaryPath"
Write-Host ("ok={0} failedCases={1}" -f $matrixOk, $failedCases)
if (-not $matrixOk) { exit 1 }
exit 0
