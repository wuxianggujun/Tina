<#
.SYNOPSIS
  Run the product-2d soft-shadow differential GPU visual gate.

.DESCRIPTION
  Runs the same deterministic product scene twice with finite-source soft shadows and
  twice with point-source hard shadows. Each mode must be internally repeatable, while
  the soft and hard RGBA8 fingerprints must differ.

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
        [Parameter(Mandatory = $true)][bool]$ForceHardShadows
    )

    $arguments = @("--frames=$SampleFrames", '--frame-delay-ms=0')
    if ($ForceHardShadows) {
        $arguments += '--force-hard-shadows'
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
    $expectedSoftShadowLightCount = if ($ForceHardShadows) { 0 } else { 2 }

    $errors = New-Object System.Collections.Generic.List[string]
    if ([string]$evidence.productGate -ne $expectedProductGate) {
        [void]$errors.Add(
            "productGate expected=$expectedProductGate actual=$($evidence.productGate)")
    }
    if ([int64]$evidence.frames -ne [int64]$SampleFrames) {
        [void]$errors.Add("frames expected=$SampleFrames actual=$($evidence.frames)")
    }
    if ([int]$evidence.evidenceSchema -ne 29) {
        [void]$errors.Add("evidenceSchema expected=29 actual=$($evidence.evidenceSchema)")
    }
    if (-not [bool]$evidence.sprite2DLightingConfigured) {
        [void]$errors.Add('sprite2DLightingConfigured must be true')
    }
    if ([int]$evidence.pointLight2DCount -ne 2) {
        [void]$errors.Add("pointLight2DCount expected=2 actual=$($evidence.pointLight2DCount)")
    }
    if ([int]$evidence.authoredPointLight2DCount -ne 3) {
        [void]$errors.Add(
            "authoredPointLight2DCount expected=3 actual=$($evidence.authoredPointLight2DCount)")
    }
    if ([int]$evidence.culledPointLight2DCount -ne 1) {
        [void]$errors.Add(
            "culledPointLight2DCount expected=1 actual=$($evidence.culledPointLight2DCount)")
    }
    if ([int]$evidence.shadowOccluder2DCount -ne 2) {
        [void]$errors.Add(
            "shadowOccluder2DCount expected=2 actual=$($evidence.shadowOccluder2DCount)")
    }
    if ([int]$evidence.softShadowPointLight2DCount -ne $expectedSoftShadowLightCount) {
        [void]$errors.Add(
            "softShadowPointLight2DCount expected=$expectedSoftShadowLightCount actual=$($evidence.softShadowPointLight2DCount)")
    }
    if ([int]$evidence.normalMappedSpriteCount -ne 1) {
        [void]$errors.Add(
            "normalMappedSpriteCount expected=1 actual=$($evidence.normalMappedSpriteCount)")
    }
    foreach ($field in @(
            'texturesUploaded',
            'spriteBindingTextures',
            'spriteTextureLeasesAcquired',
            'spriteTextureRetirementsAccepted',
            'spriteTextureHandlesInvalidated',
            'spriteTextureRetirementRecords',
            'spriteTextureRetirementReleased')) {
        if ([int]$evidence.$field -ne 3) {
            [void]$errors.Add("$field expected=3 actual=$($evidence.$field)")
        }
    }
    if (-not [bool]$evidence.spriteBindingRegistryReleased) {
        [void]$errors.Add('spriteBindingRegistryReleased must be true')
    }
    if ([int]$evidence.spriteTextureRetirementLive -ne 0) {
        [void]$errors.Add(
            "spriteTextureRetirementLive expected=0 actual=$($evidence.spriteTextureRetirementLive)")
    }
    if ([int64]$evidence.sceneLightingFrames -ne [int64]$evidence.submittedRenderFrames -or
        [int64]$evidence.submittedRenderFrames -lt 1 -or
        [int64]$evidence.renderExtractions -ne
            [int64]$evidence.submittedRenderFrames + [int64]$evidence.skippedSuspendedSurfaceFrames) {
        [void]$errors.Add(
            "lighting frame accounting invalid lighting/submitted/skipped/extracted=$($evidence.sceneLightingFrames)/$($evidence.submittedRenderFrames)/$($evidence.skippedSuspendedSurfaceFrames)/$($evidence.renderExtractions)")
    }
    if (-not [bool]$evidence.renderFrameAccountingValid) {
        [void]$errors.Add('renderFrameAccountingValid must be true')
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
        hardShadowsForced = $ForceHardShadows
        authoredPointLight2DCount = [int]$evidence.authoredPointLight2DCount
        pointLight2DCount = [int]$evidence.pointLight2DCount
        culledPointLight2DCount = [int]$evidence.culledPointLight2DCount
        shadowOccluder2DCount = [int]$evidence.shadowOccluder2DCount
        softShadowPointLight2DCount = [int]$evidence.softShadowPointLight2DCount
        sceneLightingFrames = [int64]$evidence.sceneLightingFrames
        renderExtractions = [int64]$evidence.renderExtractions
        width = [int]$evidence.pixelCaptureWidth
        height = [int]$evidence.pixelCaptureHeight
        bytes = [int64]$evidence.pixelCaptureBytes
        pixelFingerprint = [string]$evidence.pixelFingerprint
        evidenceFingerprint = [string]$evidence.evidenceFingerprint
    }
}

$softA = Invoke-ShadowProbeRun -Name 'soft-shadow-a' -ForceHardShadows $false
$softB = Invoke-ShadowProbeRun -Name 'soft-shadow-b' -ForceHardShadows $false
$hardA = Invoke-ShadowProbeRun -Name 'hard-shadow-a' -ForceHardShadows $true
$hardB = Invoke-ShadowProbeRun -Name 'hard-shadow-b' -ForceHardShadows $true
$runs = @($softA, $softB, $hardA, $hardB)

$dimensionsMatch = $true
foreach ($run in $runs) {
    if ($run.width -ne $softA.width -or $run.height -ne $softA.height -or $run.bytes -ne $softA.bytes) {
        $dimensionsMatch = $false
        break
    }
}
$softRepeatable = $softA.pixelFingerprint -eq $softB.pixelFingerprint
$hardRepeatable = $hardA.pixelFingerprint -eq $hardB.pixelFingerprint
$pixelDifferential = $softA.pixelFingerprint -ne $hardA.pixelFingerprint
$evidenceRepeatable =
    $softA.evidenceFingerprint -eq $softB.evidenceFingerprint -and
    $hardA.evidenceFingerprint -eq $hardB.evidenceFingerprint
$modeEvidenceDifferential = $softA.evidenceFingerprint -ne $hardA.evidenceFingerprint

$failures = New-Object System.Collections.Generic.List[string]
if (-not $dimensionsMatch) { [void]$failures.Add('capture dimensions differ between probe runs') }
if (-not $softRepeatable) { [void]$failures.Add('soft-shadow pixel fingerprint is not repeatable') }
if (-not $hardRepeatable) { [void]$failures.Add('hard-shadow pixel fingerprint is not repeatable') }
if (-not $pixelDifferential) { [void]$failures.Add('soft and hard shadow pixels are identical') }
if (-not $evidenceRepeatable) { [void]$failures.Add('structural evidence is not repeatable') }
if (-not $modeEvidenceDifferential) { [void]$failures.Add('shadow mode did not change structural evidence') }
if ($failures.Count -ne 0) {
    throw "product-2d shadow visual gate failed: $($failures -join '; ')"
}

$report = [ordered]@{
    schema = 2
    gate = 'product-2d-soft-shadow-differential'
    head = (git rev-parse HEAD 2>$null)
    sourceRoot = $SourceRoot
    binDir = $BinDir
    expectedProductGate = $expectedProductGate
    sampleFrames = $SampleFrames
    captureDimensions = @($softA.width, $softA.height)
    softShadowFingerprint = $softA.pixelFingerprint
    hardShadowFingerprint = $hardA.pixelFingerprint
    softShadowEvidenceFingerprint = $softA.evidenceFingerprint
    hardShadowEvidenceFingerprint = $hardA.evidenceFingerprint
    dimensionsMatch = $dimensionsMatch
    softShadowRepeatable = $softRepeatable
    hardShadowRepeatable = $hardRepeatable
    pixelDifferential = $pixelDifferential
    evidenceRepeatable = $evidenceRepeatable
    modeEvidenceDifferential = $modeEvidenceDifferential
    runs = $runs
    limitations = @(
        'Same-host/backend differential evidence; not a cross-GPU exact golden',
        'The two modes differ only by PointLight2D source radius in an otherwise identical deterministic scene'
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
Write-Host "product-2d soft-shadow visual gate ok soft=$($softA.pixelFingerprint) hard=$($hardA.pixelFingerprint)"
exit 0
