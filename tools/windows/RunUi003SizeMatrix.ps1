#Requires -Version 5.1
<#
.SYNOPSIS
  UI-003 multi-size logical / content-scale-like visual matrix for tina_sample_2d.

.DESCRIPTION
  Runs RunUi003VisualGate.ps1 for several logical window sizes that approximate
  content-scale client footprints without changing OS display scale:

    design-1x        960x540   (product absolute layout gold)
    scale-like-1.25x 1200x675
    scale-like-1.5x  1440x810
    desktop-720p     1280x720
    scale-like-2x    1920x1080

  Absolute UI layout is design-locked at 960x540; larger windows keep HUD at
  design coords and ROI mapping uses design/client fractions. Per-size ROI
  baselines live under tools/windows/baselines/ when CompareBaseline is set.

  NOT proven: OS Settings DPI 100/150/200% multi-monitor multi-DPI goldens.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [switch]$SkipBuild,
    [string]$OutDir = 'artifacts/screenshots/ui-003-size-matrix',
    # When set, regenerate checked-in per-size baselines (commit only after local review).
    [switch]$WriteBaselines,
    # Skip baseline compare even when a baseline file exists for the case.
    [switch]$NoBaselineCompare
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
    $Exe = Join-Path $SourceRoot 'out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe'
}

$gateScript = Join-Path $SourceRoot 'tools\windows\RunUi003VisualGate.ps1'
if (-not (Test-Path -LiteralPath $gateScript)) {
    throw "missing $gateScript"
}

if (-not $SkipBuild) {
    Write-Host '=== build tina_sample_2d ==='
    cmake --build --preset windows-vnext-bgfx-debug --parallel 2 --target tina_sample_2d -- /nr:false
    if ($LASTEXITCODE -ne 0) { throw "build failed exit=$LASTEXITCODE" }
}

# Content-scale-like logical sizes (16:9). Product absolute UI stays at design coords.
$sizes = @(
    @{
        W = 960; H = 540; Label = 'design-1x'
        ScaleLike = 1.0
        BaselineRel = 'tools/windows/baselines/ui-003-sample2d-960x540.json'
        CompareBaseline = $true
    },
    @{
        W = 1200; H = 675; Label = 'scale-like-1.25x'
        ScaleLike = 1.25
        BaselineRel = 'tools/windows/baselines/ui-003-sample2d-1200x675.json'
        CompareBaseline = $true
    },
    @{
        W = 1440; H = 810; Label = 'scale-like-1.5x'
        ScaleLike = 1.5
        BaselineRel = 'tools/windows/baselines/ui-003-sample2d-1440x810.json'
        CompareBaseline = $true
    },
    @{
        W = 1280; H = 720; Label = 'desktop-720p'
        ScaleLike = 1.3333
        BaselineRel = 'tools/windows/baselines/ui-003-sample2d-1280x720.json'
        CompareBaseline = $true
    },
    @{
        W = 1920; H = 1080; Label = 'scale-like-2x'
        ScaleLike = 2.0
        BaselineRel = 'tools/windows/baselines/ui-003-sample2d-1920x1080.json'
        CompareBaseline = $true
    }
)

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ([IO.Path]::IsPathRooted($OutDir)) {
    $matrixRoot = Join-Path $OutDir $stamp
} else {
    $matrixRoot = Join-Path $SourceRoot (Join-Path $OutDir $stamp)
}
New-Item -ItemType Directory -Path $matrixRoot -Force | Out-Null

$results = @()
$fail = 0
$sampleFrames = 120 # Covers the sample's complete Idle -> Walk -> HitWall verification cycle.
foreach ($s in $sizes) {
    $label = [string]$s.Label
    $w = [int]$s.W
    $h = [int]$s.H
    $caseRel = Join-Path (Join-Path $OutDir $stamp) $label
    $args = "--frames=$sampleFrames --frame-delay-ms=0 --width=$w --height=$h"
    Write-Host "=== UI-003 size case $label (${w}x${h}) scaleLike=$($s.ScaleLike) ==="

    $baselineRel = [string]$s.BaselineRel
    $baselineAbs = Join-Path $SourceRoot $baselineRel
    $hasBaseline = Test-Path -LiteralPath $baselineAbs
    $wantCompare = [bool]$s.CompareBaseline -and -not $NoBaselineCompare -and ($hasBaseline -or $WriteBaselines)

    $extra = @()
    if ($WriteBaselines) {
        $extra += '-WriteBaseline'
    }
    if (-not $wantCompare -or $NoBaselineCompare) {
        if (-not $WriteBaselines) {
            $extra += '-NoBaselineCompare'
        }
    }

    & powershell -NoProfile -ExecutionPolicy Bypass -File $gateScript `
        -SourceRoot $SourceRoot `
        -Exe $Exe `
        -ArgString $args `
        -OutDir $caseRel `
        -SkipBuild `
        -DesignWidth 960 `
        -DesignHeight 540 `
        -ExpectedLogicalWidth $w `
        -ExpectedLogicalHeight $h `
        -BaselinePath $baselineRel `
        -WarmupMs 900 `
        -CaptureCount 3 `
        -RequiredConsecutiveUsefulCaptures 2 `
        @extra
    $code = 0
    if ($null -ne $LASTEXITCODE) { $code = [int]$LASTEXITCODE }
    $ok = ($code -eq 0)
    if (-not $ok) { $fail++ }

    $caseAbs = if ([IO.Path]::IsPathRooted($caseRel)) { $caseRel } else { Join-Path $SourceRoot $caseRel }
    $summary = Get-ChildItem -Path $caseAbs -Recurse -Filter 'ui-003-gate.json' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $client = $null
    $sampleMetrics = $null
    $baselineCompare = $null
    if ($summary) {
        $j = Get-Content -LiteralPath $summary.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
        $client = $j.clientSize
        $sampleMetrics = $j.sampleMetrics
        $baselineCompare = $j.baselineCompare
        $ok = [bool]$j.ok
        if (-not $ok -and $code -eq 0) { $fail++ }
    }

    $results += [pscustomobject]@{
        label = $label
        scaleLike = [double]$s.ScaleLike
        requestedLogical = @($w, $h)
        clientSize = $client
        sampleMetrics = $sampleMetrics
        baselinePath = $baselineRel
        baselinePresent = [bool]$hasBaseline
        baselineCompare = $baselineCompare
        ok = [bool]$ok
        exitCode = $code
        report = if ($summary) { [string]$summary.FullName } else { $null }
    }
}

$matrixOk = ($fail -eq 0)
$matrixReport = [pscustomobject]@{
    schema = 3
    gate = 'UI-003-size-matrix'
    ok = [bool]$matrixOk
    tip = [string](git rev-parse HEAD 2>$null)
    cases = $results
    proven = @(
        'Logical window size matrix via --width/--height (content-scale-like client footprints)',
        'Absolute UI design-locked ROI mapping + soft/hard ROI signal gates',
        'Per-size ROI baselines under tools/windows/baselines when present (schema 3 + fontFingerprint)',
        'Sample JSON contentScale/logical/framebuffer consistency (when fields present)',
        'Font identity fingerprint (path/sha256/env TINA_UI_FONT_PATH/FreeType-on) in gate + baseline; mismatch fails when baseline expects it',
        'blankLike exclusion via CaptureSampleWindow'
    )
    open = @(
        'OS Settings display scale 100/150/200% true multi-DPI golden matrix',
        'Multi-monitor mixed-DPI capture matrix',
        'Cross-GPU pixel golden (font fingerprint is identity metadata, not pixel golden)'
    )
    notes = @(
        'Larger logical windows leave empty margin; HUD stays at 960x540 design coords',
        'scale-like-* sizes approximate content-scale client sizes without OS DPI change',
        'design-1x (960x540) remains product absolute-layout verification gold',
        'fontFingerprint is shared identity across sizes; per-size baselines still store full fingerprint for self-contained compare'
    )
}

$matrixPath = Join-Path $matrixRoot 'matrix-report.json'
$matrixReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $matrixPath -Encoding utf8
$summaryDir = Join-Path $SourceRoot 'artifacts\gates'
if (-not (Test-Path -LiteralPath $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir -Force | Out-Null
}
$summaryPath = Join-Path $summaryDir ("ui-003-size-matrix-{0}.json" -f $stamp)
Copy-Item -LiteralPath $matrixPath -Destination $summaryPath -Force

Write-Host "matrix report: $matrixPath"
Write-Host "summary: $summaryPath"
Write-Host ("ok={0} failCases={1}" -f $matrixOk, $fail)
if (-not $matrixOk) { exit 1 }
exit 0
