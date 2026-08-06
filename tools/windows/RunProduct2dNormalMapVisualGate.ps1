<#
.SYNOPSIS
  Run the product-2d normal-map differential GPU visual gate.

.DESCRIPTION
  Runs the deterministic product scene twice with character normal mapping enabled
  and twice with only the character SpriteRenderer2D normalTexture handle cleared.
  Each mode must be internally repeatable, while enabled and disabled RGBA8 and
  structural evidence fingerprints must differ.

  The independent normal atlas is cooked, loaded, uploaded, registered, and retired
  in both modes. This is same-host/backend differential evidence, not a cross-GPU golden.
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

function Invoke-NormalMapProbeRun {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$DisableNormalMaps
    )

    $arguments = @("--frames=$SampleFrames", '--frame-delay-ms=0')
    if ($DisableNormalMaps) {
        $arguments += '--disable-normal-maps'
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
    $expectedNormalMappedSpriteCount = if ($DisableNormalMaps) { 0 } else { 1 }

    $errors = New-Object System.Collections.Generic.List[string]
    if ([string]$evidence.productGate -ne $expectedProductGate) {
        [void]$errors.Add(
            "productGate expected=$expectedProductGate actual=$($evidence.productGate)")
    }
    if ([int64]$evidence.frames -ne [int64]$SampleFrames) {
        [void]$errors.Add("frames expected=$SampleFrames actual=$($evidence.frames)")
    }
    if ([int]$evidence.evidenceSchema -ne 24) {
        [void]$errors.Add("evidenceSchema expected=24 actual=$($evidence.evidenceSchema)")
    }
    if ([int]$evidence.normalMappedSpriteCount -ne $expectedNormalMappedSpriteCount) {
        [void]$errors.Add(
            "normalMappedSpriteCount expected=$expectedNormalMappedSpriteCount actual=$($evidence.normalMappedSpriteCount)")
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
    if (-not [bool]$evidence.sprite2DLightingConfigured) {
        [void]$errors.Add('sprite2DLightingConfigured must be true')
    }
    foreach ($expectedCount in @(
            @{ Field = 'authoredPointLight2DCount'; Value = 3 },
            @{ Field = 'pointLight2DCount'; Value = 2 },
            @{ Field = 'culledPointLight2DCount'; Value = 1 },
            @{ Field = 'shadowOccluder2DCount'; Value = 2 },
            @{ Field = 'softShadowPointLight2DCount'; Value = 2 })) {
        $field = [string]$expectedCount.Field
        $value = [int]$expectedCount.Value
        if ([int]$evidence.$field -ne $value) {
            [void]$errors.Add("$field expected=$value actual=$($evidence.$field)")
        }
    }
    if ([int64]$evidence.sceneLightingFrames -ne [int64]$evidence.renderExtractions -or
        [int64]$evidence.renderExtractions -lt 1) {
        [void]$errors.Add(
            "sceneLightingFrames must equal non-zero renderExtractions actual=$($evidence.sceneLightingFrames)/$($evidence.renderExtractions)")
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
        normalMapsDisabled = $DisableNormalMaps
        normalMappedSpriteCount = [int]$evidence.normalMappedSpriteCount
        texturesUploaded = [int]$evidence.texturesUploaded
        spriteBindingTextures = [int]$evidence.spriteBindingTextures
        width = [int]$evidence.pixelCaptureWidth
        height = [int]$evidence.pixelCaptureHeight
        bytes = [int64]$evidence.pixelCaptureBytes
        pixelFingerprint = [string]$evidence.pixelFingerprint
        evidenceFingerprint = [string]$evidence.evidenceFingerprint
    }
}

$onA = Invoke-NormalMapProbeRun -Name 'normal-map-on-a' -DisableNormalMaps $false
$onB = Invoke-NormalMapProbeRun -Name 'normal-map-on-b' -DisableNormalMaps $false
$offA = Invoke-NormalMapProbeRun -Name 'normal-map-off-a' -DisableNormalMaps $true
$offB = Invoke-NormalMapProbeRun -Name 'normal-map-off-b' -DisableNormalMaps $true
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
if (-not $onRepeatable) { [void]$failures.Add('normal-map-on pixel fingerprint is not repeatable') }
if (-not $offRepeatable) { [void]$failures.Add('normal-map-off pixel fingerprint is not repeatable') }
if (-not $pixelDifferential) { [void]$failures.Add('normal-map-on and normal-map-off pixels are identical') }
if (-not $evidenceRepeatable) { [void]$failures.Add('structural evidence is not repeatable') }
if (-not $modeEvidenceDifferential) { [void]$failures.Add('normal-map mode did not change structural evidence') }
if ($failures.Count -ne 0) {
    throw "product-2d normal-map visual gate failed: $($failures -join '; ')"
}

$report = [ordered]@{
    schema = 1
    gate = 'product-2d-normal-map-differential'
    head = (git rev-parse HEAD 2>$null)
    sourceRoot = $SourceRoot
    binDir = $BinDir
    expectedProductGate = $expectedProductGate
    sampleFrames = $SampleFrames
    captureDimensions = @($onA.width, $onA.height)
    normalMapOnFingerprint = $onA.pixelFingerprint
    normalMapOffFingerprint = $offA.pixelFingerprint
    normalMapOnEvidenceFingerprint = $onA.evidenceFingerprint
    normalMapOffEvidenceFingerprint = $offA.evidenceFingerprint
    dimensionsMatch = $dimensionsMatch
    normalMapOnRepeatable = $onRepeatable
    normalMapOffRepeatable = $offRepeatable
    pixelDifferential = $pixelDifferential
    evidenceRepeatable = $evidenceRepeatable
    modeEvidenceDifferential = $modeEvidenceDifferential
    runs = $runs
    limitations = @(
        'Same-host/backend differential evidence; not a cross-GPU exact golden',
        'The two modes differ only by clearing SpriteRenderer2D.normalTexture; normal atlas lifecycle remains identical'
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
Write-Host "product-2d normal-map visual gate ok on=$($onA.pixelFingerprint) off=$($offA.pixelFingerprint)"
exit 0
