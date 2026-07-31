<#
.SYNOPSIS
  Run the product-2d hard-shadow differential GPU visual gate.

.DESCRIPTION
  Runs the same deterministic product scene twice with shadow occluders enabled and
  twice with the existing occluder components inactive. Each mode must be internally
  repeatable, while the enabled and disabled RGBA8 fingerprints must differ.

  This is a same-host/backend differential gate. It proves that committed shadow
  segments affect captured GPU pixels without claiming a cross-GPU exact golden.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$BinDir = '',
    [int]$SampleFrames = 300,
    [string]$OutJson = ''
)
#Requires -Version 5.1

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
}
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
Set-Location -LiteralPath $SourceRoot

if ($SampleFrames -lt 1) {
    throw 'SampleFrames must be greater than zero'
}
if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = Join-Path $SourceRoot 'out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug'
} elseif (-not [System.IO.Path]::IsPathRooted($BinDir)) {
    $BinDir = Join-Path $SourceRoot $BinDir
}
$BinDir = [System.IO.Path]::GetFullPath($BinDir)
$samplePath = Join-Path $BinDir 'tina_sample_2d.exe'
if (-not (Test-Path -LiteralPath $samplePath -PathType Leaf)) {
    throw "missing executable: $samplePath"
}
$expectedProductGate = 'bgfx-physics-freetype-audio'

function Invoke-ShadowProbeRun {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$DisableShadowOccluders
    )

    $arguments = @("--frames=$SampleFrames", '--frame-delay-ms=0')
    if ($DisableShadowOccluders) {
        $arguments += '--disable-shadow-occluders'
    }
    $outputLines = @(& $samplePath @arguments 2>&1)
    $exitCode = $LASTEXITCODE
    $stdout = ($outputLines | Out-String).Trim()
    if ($exitCode -ne 0) {
        throw "$Name failed exit=$exitCode output=$stdout"
    }

    $jsonLine = $outputLines |
        ForEach-Object { [string]$_ } |
        Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_2d"' } |
        Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($jsonLine)) {
        throw "$Name did not emit product success JSON: $stdout"
    }
    $evidence = $jsonLine | ConvertFrom-Json
    $expectedOccluderCount = 2
    if ($DisableShadowOccluders) {
        $expectedOccluderCount = 0
    }

    $errors = New-Object System.Collections.Generic.List[string]
    if ([string]$evidence.productGate -ne $expectedProductGate) {
        [void]$errors.Add(
            "productGate expected=$expectedProductGate actual=$($evidence.productGate)")
    }
    if ([int64]$evidence.frames -ne [int64]$SampleFrames) {
        [void]$errors.Add("frames expected=$SampleFrames actual=$($evidence.frames)")
    }
    if ([int]$evidence.evidenceSchema -ne 16) {
        [void]$errors.Add("evidenceSchema expected=16 actual=$($evidence.evidenceSchema)")
    }
    if (-not [bool]$evidence.sprite2DLightingConfigured) {
        [void]$errors.Add('sprite2DLightingConfigured must be true')
    }
    if ([int]$evidence.pointLight2DCount -ne 2) {
        [void]$errors.Add("pointLight2DCount expected=2 actual=$($evidence.pointLight2DCount)")
    }
    if ([int]$evidence.shadowOccluder2DCount -ne $expectedOccluderCount) {
        [void]$errors.Add(
            "shadowOccluder2DCount expected=$expectedOccluderCount actual=$($evidence.shadowOccluder2DCount)")
    }
    if ([int64]$evidence.sceneLightingFrames -ne [int64]$evidence.renderExtractions) {
        [void]$errors.Add(
            "sceneLightingFrames must equal renderExtractions actual=$($evidence.sceneLightingFrames)/$($evidence.renderExtractions)")
    }
    if ([int64]$evidence.renderExtractions -lt 1) {
        [void]$errors.Add('renderExtractions must be greater than zero')
    }
    if (-not [bool]$evidence.pixelCaptureOk) {
        [void]$errors.Add('pixelCaptureOk must be true')
    }
    if ([int]$evidence.pixelCaptureWidth -lt 1 -or [int]$evidence.pixelCaptureHeight -lt 1 -or
        [int64]$evidence.pixelCaptureBytes -lt 4) {
        [void]$errors.Add('captured RGBA8 dimensions/byte count must be non-zero')
    }
    if ([string]$evidence.pixelFingerprint -notmatch '^[0-9a-f]{32}$') {
        [void]$errors.Add('pixelFingerprint must contain 32 lowercase hexadecimal characters')
    }
    $expectedCaptureBytes =
        [int64]$evidence.pixelCaptureWidth * [int64]$evidence.pixelCaptureHeight * 4
    if ([int64]$evidence.pixelCaptureBytes -ne $expectedCaptureBytes) {
        [void]$errors.Add(
            "pixelCaptureBytes expected=$expectedCaptureBytes actual=$($evidence.pixelCaptureBytes)")
    }
    if ([string]$evidence.evidenceFingerprint -notmatch '^[0-9a-f]{32}$') {
        [void]$errors.Add('evidenceFingerprint must contain 32 lowercase hexadecimal characters')
    }
    if ($errors.Count -ne 0) {
        throw "$Name evidence invalid: $($errors -join '; ')"
    }

    return [pscustomobject]@{
        name = $Name
        productGate = [string]$evidence.productGate
        frames = [int64]$evidence.frames
        shadowsDisabled = $DisableShadowOccluders
        shadowOccluder2DCount = [int]$evidence.shadowOccluder2DCount
        sceneLightingFrames = [int64]$evidence.sceneLightingFrames
        renderExtractions = [int64]$evidence.renderExtractions
        width = [int]$evidence.pixelCaptureWidth
        height = [int]$evidence.pixelCaptureHeight
        bytes = [int64]$evidence.pixelCaptureBytes
        pixelFingerprint = [string]$evidence.pixelFingerprint
        evidenceFingerprint = [string]$evidence.evidenceFingerprint
    }
}

$onA = Invoke-ShadowProbeRun -Name 'shadow-on-a' -DisableShadowOccluders $false
$onB = Invoke-ShadowProbeRun -Name 'shadow-on-b' -DisableShadowOccluders $false
$offA = Invoke-ShadowProbeRun -Name 'shadow-off-a' -DisableShadowOccluders $true
$offB = Invoke-ShadowProbeRun -Name 'shadow-off-b' -DisableShadowOccluders $true
$runs = @($onA, $onB, $offA, $offB)

$dimensionsMatch = $true
foreach ($run in $runs) {
    if ($run.width -ne $onA.width -or $run.height -ne $onA.height -or $run.bytes -ne $onA.bytes) {
        $dimensionsMatch = $false
        break
    }
}
$onRepeatable = $onA.pixelFingerprint -eq $onB.pixelFingerprint
$offRepeatable = $offA.pixelFingerprint -eq $offB.pixelFingerprint
$pixelDifferential = $onA.pixelFingerprint -ne $offA.pixelFingerprint
$evidenceRepeatable =
    $onA.evidenceFingerprint -eq $onB.evidenceFingerprint -and
    $offA.evidenceFingerprint -eq $offB.evidenceFingerprint
$modeEvidenceDifferential = $onA.evidenceFingerprint -ne $offA.evidenceFingerprint

$failures = New-Object System.Collections.Generic.List[string]
if (-not $dimensionsMatch) { [void]$failures.Add('capture dimensions differ between probe runs') }
if (-not $onRepeatable) { [void]$failures.Add('shadow-on pixel fingerprint is not repeatable') }
if (-not $offRepeatable) { [void]$failures.Add('shadow-off pixel fingerprint is not repeatable') }
if (-not $pixelDifferential) { [void]$failures.Add('shadow-on and shadow-off pixels are identical') }
if (-not $evidenceRepeatable) { [void]$failures.Add('structural evidence is not repeatable') }
if (-not $modeEvidenceDifferential) { [void]$failures.Add('shadow mode did not change structural evidence') }
if ($failures.Count -ne 0) {
    throw "product-2d shadow visual gate failed: $($failures -join '; ')"
}

$report = [ordered]@{
    schema = 1
    gate = 'product-2d-shadow-differential'
    head = (git rev-parse HEAD 2>$null)
    sourceRoot = $SourceRoot
    binDir = $BinDir
    expectedProductGate = $expectedProductGate
    sampleFrames = $SampleFrames
    captureDimensions = @($onA.width, $onA.height)
    shadowOnFingerprint = $onA.pixelFingerprint
    shadowOffFingerprint = $offA.pixelFingerprint
    shadowOnEvidenceFingerprint = $onA.evidenceFingerprint
    shadowOffEvidenceFingerprint = $offA.evidenceFingerprint
    dimensionsMatch = $dimensionsMatch
    shadowOnRepeatable = $onRepeatable
    shadowOffRepeatable = $offRepeatable
    pixelDifferential = $pixelDifferential
    evidenceRepeatable = $evidenceRepeatable
    modeEvidenceDifferential = $modeEvidenceDifferential
    runs = $runs
    limitations = @(
        'Same-host/backend differential evidence; not a cross-GPU exact golden',
        'The two modes differ only by ShadowOccluder2D.active in an otherwise identical deterministic scene'
    )
    ok = $true
}

if (-not [string]::IsNullOrWhiteSpace($OutJson)) {
    $outParent = Split-Path -Parent $OutJson
    if ($outParent -and -not (Test-Path -LiteralPath $outParent)) {
        New-Item -ItemType Directory -Path $outParent | Out-Null
    }
    ($report | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $OutJson -Encoding utf8
}

Write-Output ($report | ConvertTo-Json -Depth 6 -Compress)
Write-Host "product-2d shadow visual gate ok on=$($onA.pixelFingerprint) off=$($offA.pixelFingerprint)"
exit 0
