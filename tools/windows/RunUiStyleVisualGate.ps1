#Requires -Version 5.1
<#
.SYNOPSIS
  UI-STYLE-001 product Visual gate: Dark/Light header-accent ColorToken ROI differential.

.DESCRIPTION
  Captures tina_sample_ui_showcase once per theme (no --auto-demo so the theme stays put)
  and compares a fixed client-area ROI over the stylesheet-driven header accent strip.
  Proves Dark and Light resolve different ColorToken fills without claiming cross-GPU goldens.

  Same-host / same-backend differential evidence only.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [ValidateSet('windows-vnext-bgfx-ui-freetype-debug', 'windows-vnext-bgfx-debug')]
    [string]$BuildPreset = 'windows-vnext-bgfx-debug',
    [string]$OutDir = 'artifacts/screenshots/ui-style',
    [int]$Frames = 90,
    [int]$WarmupMs = 900,
    [int]$CaptureCount = 3,
    [double]$MinChannelDelta = 12.0,
    [switch]$SkipBuild,
    [string]$OutJson = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if ($Frames -lt 30) { throw 'Frames must be at least 30' }
if ($MinChannelDelta -lt 1.0) { throw 'MinChannelDelta must be positive' }

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
    'windows-vnext-bgfx-debug' = 'out\build\windows-msvc-vnext-bgfx'
    'windows-vnext-bgfx-ui-freetype-debug' = 'out\build\windows-msvc-vnext-bgfx-ui-freetype'
}
$expectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $SourceRoot $buildRootByPreset[$BuildPreset]))
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $expectedBuildRoot 'bin\Debug\tina_sample_ui_showcase.exe'
} elseif (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $SourceRoot $Exe
}
$Exe = [IO.Path]::GetFullPath($Exe)

if (-not $SkipBuild) {
    & cmake --build --preset $BuildPreset --target tina_sample_ui_showcase --parallel 2 -- /nr:false
    if ($LASTEXITCODE -ne 0) {
        throw "tina_sample_ui_showcase build failed exit=$LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "missing executable: $Exe"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runRoot = Join-Path $SourceRoot (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

# Design 1280x980: header accent is an 8px strip under background padding (24,20).
$designWidth = 1280
$designHeight = 980
$headerAccentRoi = @(24, 20, 32, 92)

function Get-RoiAverage {
    param(
        [Parameter(Mandatory = $true)][string]$PngPath,
        [Parameter(Mandatory = $true)][int[]]$DesignRect
    )
    $image = [Drawing.Bitmap]::FromFile($PngPath)
    try {
        $left = [Math]::Max(0, [int][Math]::Floor($DesignRect[0] * $image.Width / [double]$designWidth))
        $top = [Math]::Max(0, [int][Math]::Floor($DesignRect[1] * $image.Height / [double]$designHeight))
        $right = [Math]::Min($image.Width, [int][Math]::Ceiling($DesignRect[2] * $image.Width / [double]$designWidth))
        $bottom = [Math]::Min($image.Height, [int][Math]::Ceiling($DesignRect[3] * $image.Height / [double]$designHeight))
        $width = $right - $left
        $height = $bottom - $top
        if ($width -lt 1 -or $height -lt 1) {
            throw "empty header accent ROI on $PngPath"
        }
        [int64]$sumR = 0
        [int64]$sumG = 0
        [int64]$sumB = 0
        $count = 0
        for ($y = $top; $y -lt $bottom; ++$y) {
            for ($x = $left; $x -lt $right; ++$x) {
                $c = $image.GetPixel($x, $y)
                $sumR += $c.R
                $sumG += $c.G
                $sumB += $c.B
                $count++
            }
        }
        return [pscustomobject]@{
            left = $left
            top = $top
            width = $width
            height = $height
            avgRgb = @(
                [Math]::Round($sumR / [double]$count, 2),
                [Math]::Round($sumG / [double]$count, 2),
                [Math]::Round($sumB / [double]$count, 2)
            )
            pixels = $count
        }
    } finally {
        $image.Dispose()
    }
}

function Invoke-ThemeCapture {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('dark', 'light')][string]$Theme
    )
    $themeDir = Join-Path $runRoot $Theme
    New-Item -ItemType Directory -Path $themeDir -Force | Out-Null
    $argString = "--frames=$Frames --frame-delay-ms=0 --theme=$Theme"
    $captureScript = Join-Path $SourceRoot 'tools\windows\CaptureSampleWindow.ps1'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $captureScript `
        -Exe $Exe `
        -ArgString $argString `
        -OutDir $themeDir `
        -WarmupMs $WarmupMs `
        -CaptureCount $CaptureCount `
        -CaptureIntervalMs 120 `
        -TimeoutMs 60000 `
        -RequiredConsecutiveUsefulCaptures 1 `
        -RequireNonBlank
    if ($LASTEXITCODE -ne 0) {
        throw "CaptureSampleWindow failed for theme=$Theme exit=$LASTEXITCODE"
    }
    $report = Get-ChildItem -LiteralPath $themeDir -Recurse -Filter report.json |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -eq $report) {
        throw "missing capture report for theme=$Theme under $themeDir"
    }
    $reportObj = Get-Content -LiteralPath $report.FullName -Raw -Encoding utf8 | ConvertFrom-Json
    if (-not $reportObj.ok) {
        throw ("capture report not ok for theme={0} path={1}" -f $Theme, $report.FullName)
    }
    $png = $null
    # Prefer stable useful captures (skip blank PrintWindow first frames).
    if ($reportObj.PSObject.Properties.Name -contains 'consecutiveUsefulSameSizeGate' -and
        $reportObj.consecutiveUsefulSameSizeGate -and
        $reportObj.consecutiveUsefulSameSizeGate.paths) {
        $png = [string]$reportObj.consecutiveUsefulSameSizeGate.paths[-1]
    }
    if ([string]::IsNullOrWhiteSpace($png) -and
        $reportObj.PSObject.Properties.Name -contains 'captures' -and $reportObj.captures) {
        $useful = @($reportObj.captures | Where-Object { $_.usefulNonBlank -eq $true })
        if ($useful.Count -gt 0) {
            $png = [string]$useful[-1].path
        }
    }
    if ([string]::IsNullOrWhiteSpace($png)) {
        $png = Get-ChildItem -LiteralPath (Split-Path -Parent $report.FullName) -Filter 'frame-*.png' |
            Sort-Object Name |
            Select-Object -Last 1 -ExpandProperty FullName
    }
    if ([string]::IsNullOrWhiteSpace($png) -or -not (Test-Path -LiteralPath $png -PathType Leaf)) {
        throw "missing PNG for theme=$Theme"
    }
    $stdoutPath = Join-Path (Split-Path -Parent $report.FullName) 'stdout.txt'
    $sampleJson = $null
    if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) {
        $stdout = Get-Content -LiteralPath $stdoutPath -Raw -Encoding utf8
        if ($stdout -match '\{"status":"ok".*\}') {
            $sampleJson = $Matches[0] | ConvertFrom-Json
        }
    }
    $roi = Get-RoiAverage -PngPath $png -DesignRect $headerAccentRoi
    return [pscustomobject]@{
        theme = $Theme
        reportPath = $report.FullName
        pngPath = $png
        sampleJson = $sampleJson
        roi = $roi
    }
}

$dark = Invoke-ThemeCapture -Theme dark
$light = Invoke-ThemeCapture -Theme light

foreach ($case in @($dark, $light)) {
    if ($null -ne $case.sampleJson) {
        if (-not [bool]$case.sampleJson.stylesheetInstalled) {
            throw "theme=$($case.theme) sample JSON missing stylesheetInstalled=true"
        }
        if ([int64]$case.sampleJson.styleTokenUpdates -lt 1) {
            throw "theme=$($case.theme) sample JSON styleTokenUpdates < 1"
        }
    }
}

$dAvg = $dark.roi.avgRgb
$lAvg = $light.roi.avgRgb
$delta = @(
    [Math]::Abs($dAvg[0] - $lAvg[0]),
    [Math]::Abs($dAvg[1] - $lAvg[1]),
    [Math]::Abs($dAvg[2] - $lAvg[2])
)
$maxDelta = ($delta | Measure-Object -Maximum).Maximum
$passed = $maxDelta -ge $MinChannelDelta

$result = [ordered]@{
    status = $(if ($passed) { 'ok' } else { 'fail' })
    gate = 'ui-style-visual'
    sample = 'tina_sample_ui_showcase'
    buildPreset = $BuildPreset
    exe = $Exe
    designRoi = $headerAccentRoi
    minChannelDelta = $MinChannelDelta
    maxChannelDelta = [Math]::Round([double]$maxDelta, 2)
    dark = [ordered]@{
        png = $dark.pngPath
        avgRgb = $dAvg
        stylesheetInstalled = $(if ($dark.sampleJson) { [bool]$dark.sampleJson.stylesheetInstalled } else { $null })
        styleTokenUpdates = $(if ($dark.sampleJson) { [int64]$dark.sampleJson.styleTokenUpdates } else { $null })
    }
    light = [ordered]@{
        png = $light.pngPath
        avgRgb = $lAvg
        stylesheetInstalled = $(if ($light.sampleJson) { [bool]$light.sampleJson.stylesheetInstalled } else { $null })
        styleTokenUpdates = $(if ($light.sampleJson) { [int64]$light.sampleJson.styleTokenUpdates } else { $null })
    }
    channelDelta = @(
        [Math]::Round($delta[0], 2),
        [Math]::Round($delta[1], 2),
        [Math]::Round($delta[2], 2)
    )
    note = 'Same-host Dark/Light header-accent ROI differential for stylesheet ColorToken; not cross-GPU golden.'
}

if ([string]::IsNullOrWhiteSpace($OutJson)) {
    $OutJson = Join-Path $runRoot 'ui-style-visual-gate.json'
} elseif (-not [IO.Path]::IsPathRooted($OutJson)) {
    $OutJson = Join-Path $SourceRoot $OutJson
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutJson -Encoding utf8
Write-Host ("UI-STYLE visual gate status={0} maxChannelDelta={1} json={2}" -f $result.status, $result.maxChannelDelta, $OutJson)

if (-not $passed) {
    throw ("UI-STYLE visual gate failed: maxChannelDelta={0} < min={1} dark={2} light={3}" -f `
        $result.maxChannelDelta, $MinChannelDelta, ($dAvg -join ','), ($lAvg -join ','))
}

exit 0
