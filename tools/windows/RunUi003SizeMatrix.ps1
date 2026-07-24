#Requires -Version 5.1
<#
.SYNOPSIS
  UI-003 multi-size logical window matrix for tina_sample_2d.

.DESCRIPTION
  Runs RunUi003VisualGate.ps1 for several logical window sizes. Absolute UI
  layout is designed at 960x540; larger windows keep HUD at design coords and
  ROI mapping uses client/design fractions. Baseline compare only applies to the
  default 960x540 entry.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [switch]$SkipBuild,
    [string]$OutDir = 'artifacts/screenshots/ui-003-size-matrix'
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
    cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d -- /m:2 /v:m
    if ($LASTEXITCODE -ne 0) { throw "build failed exit=$LASTEXITCODE" }
}

# Logical sizes: design baseline + larger "scaled desktop" windows (absolute UI stays put).
$sizes = @(
    @{ W = 960; H = 540; Label = 'design-1x'; CompareBaseline = $true },
    @{ W = 1280; H = 720; Label = 'desktop-720p'; CompareBaseline = $false },
    @{ W = 1440; H = 810; Label = 'design-1.5x-window'; CompareBaseline = $false }
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
foreach ($s in $sizes) {
    $label = [string]$s.Label
    $w = [int]$s.W
    $h = [int]$s.H
    # Relative OutDir under matrix stamp for nested CaptureSampleWindow paths.
    $caseRel = Join-Path (Join-Path $OutDir $stamp) $label
    $args = "--frames=90 --frame-delay-ms=0 --width=$w --height=$h"
    Write-Host "=== UI-003 size case $label (${w}x${h}) ==="

    $noBaseline = (-not [bool]$s.CompareBaseline)
    $extra = @()
    if ($noBaseline) { $extra += '-NoBaselineCompare' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $gateScript `
        -SourceRoot $SourceRoot `
        -Exe $Exe `
        -ArgString $args `
        -OutDir $caseRel `
        -SkipBuild `
        -DesignWidth 960 `
        -DesignHeight 540 `
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
    if ($summary) {
        $j = Get-Content -LiteralPath $summary.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
        $client = $j.clientSize
        $ok = [bool]$j.ok
    }

    $results += [pscustomobject]@{
        label = $label
        requestedLogical = @($w, $h)
        clientSize = $client
        ok = [bool]$ok
        exitCode = $code
        report = if ($summary) { [string]$summary.FullName } else { $null }
    }
}

$matrixOk = ($fail -eq 0)
$matrixReport = [pscustomobject]@{
    schema = 1
    gate = 'UI-003-size-matrix'
    ok = [bool]$matrixOk
    tip = [string](git rev-parse HEAD 2>$null)
    cases = $results
    notes = @(
        'Logical window size matrix via --width/--height',
        'Absolute UI layout is design-locked; larger windows leave empty margin',
        'Baseline compare only on design-1x (960x540)',
        'Not a system DPI 100/150/200% matrix (requires OS scale change)'
    )
}

$matrixPath = Join-Path $matrixRoot 'matrix-report.json'
$matrixReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $matrixPath -Encoding utf8
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
