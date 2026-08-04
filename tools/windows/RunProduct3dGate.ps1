<#
.SYNOPSIS
  Build and run the complete Windows product-3d gate (TEST-003 topology).

.DESCRIPTION
  Configure/build the bgfx + FreeType graph, run the affected Core, Scene,
  Asset, Render, and retained UI GoogleTest executables directly, then run the
  tina_sample_3d 300-frame product smoke with automated Dark -> Light -> Dark
  switching plus ListView/TreeView collection interaction. The final JSON is
  validated as evidence schema 13. Short IBL on/on and off/off runs additionally
  prove machine-local pixel stability within each mode and a visible A/B change.

  Does not use CTest. Does not clean-first wipe. Exits non-zero on first failure.

.PARAMETER BinDir
  Directory containing the selected build preset's executables. Relative paths
  are resolved from SourceRoot. Pass this explicitly when custom presets use a
  different binary directory.

.PARAMETER IblComparisonFrames
  Frame count for each short IBL on/off repeatability probe. The default keeps
  the four differential runs bounded while allowing the frame pipeline to settle.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$BuildPreset = 'windows-vnext-bgfx-ui-freetype-debug',
    [string]$ConfigurePreset = 'windows-msvc-vnext-bgfx-ui-freetype',
    [int]$SampleFrames = 300,
    [int]$IblComparisonFrames = 30,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [string]$OutJson = '',
    [string]$BinDir = ''
)
#Requires -Version 5.1

$ErrorActionPreference = 'Stop'
if ($SampleFrames -lt 3) {
    throw 'SampleFrames must be at least 3 because --ui-theme-demo has two scheduled transitions'
}
if ($IblComparisonFrames -lt 1) {
    throw 'IblComparisonFrames must be at least 1'
}

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
}
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
Set-Location -LiteralPath $SourceRoot

if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = Join-Path $SourceRoot 'out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug'
} elseif (-not [System.IO.Path]::IsPathRooted($BinDir)) {
    $BinDir = Join-Path $SourceRoot $BinDir
}
$BinDir = [System.IO.Path]::GetFullPath($BinDir)

$targets = @(
    'tina_tests',
    'tina_scene_tests',
    'tina_asset_format_tests',
    'tina_asset_tests',
    'tina_render_scene_tests',
    'tina_render_bgfx_tests',
    'tina_ui_tests',
    'tina_runtime_ui_tests',
    'tina_ui_render_integration_tests',
    'tina_ui_freetype_tests',
    'tina_ui_uia_tests',
    'tina_sample_3d'
)
$testExes = @(
    'tina_tests.exe',
    'tina_scene_tests.exe',
    'tina_asset_format_tests.exe',
    'tina_asset_tests.exe',
    'tina_render_scene_tests.exe',
    'tina_render_bgfx_tests.exe',
    'tina_ui_tests.exe',
    'tina_runtime_ui_tests.exe',
    'tina_ui_render_integration_tests.exe',
    'tina_ui_freetype_tests.exe',
    'tina_ui_uia_tests.exe'
)

$report = [ordered]@{
    schema          = 1
    gate            = 'product-3d-bgfx-freetype-ui'
    startedAtUtc    = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot      = $SourceRoot
    configurePreset = $ConfigurePreset
    buildPreset     = $BuildPreset
    binDir          = $BinDir
    sampleFrames    = $SampleFrames
    iblComparisonFrames = $IblComparisonFrames
    head            = (git rev-parse HEAD 2>$null)
    steps           = @()
    ok              = $false
}

function Add-Step {
    param([string]$Name, [int]$ExitCode, [string]$Detail = '')
    $script:report.steps += [ordered]@{
        name     = $Name
        exitCode = $ExitCode
        detail   = $Detail
        ok       = ($ExitCode -eq 0)
    }
    if ($ExitCode -ne 0) {
        throw "step failed: $Name exit=$ExitCode $Detail"
    }
}

if (-not $SkipConfigure) {
    & cmake --preset $ConfigurePreset
    Add-Step -Name 'configure' -ExitCode $LASTEXITCODE
}

if (-not $SkipBuild) {
    $targetArgs = @('--preset', $BuildPreset, '--parallel', '2', '--target') + $targets + @('--', '/nr:false')
    & cmake --build @targetArgs
    Add-Step -Name 'build' -ExitCode $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $BinDir -PathType Container)) {
    Add-Step -Name 'binDir' -ExitCode 1 -Detail "missing directory: $BinDir; pass -BinDir for custom output"
}

foreach ($exe in $testExes) {
    $path = Join-Path $BinDir $exe
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Step -Name $exe -ExitCode 1 -Detail "missing executable: $path"
    }
    & $path --gtest_color=yes
    Add-Step -Name $exe -ExitCode $LASTEXITCODE
}

$samplePath = Join-Path $BinDir 'tina_sample_3d.exe'
if (-not (Test-Path -LiteralPath $samplePath -PathType Leaf)) {
    Add-Step -Name 'tina_sample_3d' -ExitCode 1 -Detail "missing executable: $samplePath"
}

function Invoke-ProductSampleEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$StepName,
        [Parameter(Mandatory = $true)][ValidateSet('on', 'off')][string]$IblMode,
        [Parameter(Mandatory = $true)][int]$Frames,
        [switch]$ThemeDemo,
        [switch]$CaptureSceneRgb
    )

    $arguments = @("--frames=$Frames", '--frame-delay-ms=0', '--ui-theme=dark', "--ibl=$IblMode")
    if ($ThemeDemo) {
        $arguments += '--ui-theme-demo'
    }
    $sceneRgbPath = ''
    $sceneRgbBytes = $null
    if ($CaptureSceneRgb) {
        $sceneRgbPath = [System.IO.Path]::GetTempFileName()
        $arguments += "--scene-rgb-output=$sceneRgbPath"
    }
    try {
        $stdout = & $samplePath @arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            Add-Step -Name $StepName -ExitCode $exitCode -Detail $stdout.Trim()
        }

        $jsonLine = $stdout -split '\r?\n' |
            Where-Object { $_ -match '^\s*\{"status":"(ok|error)"' } |
            Select-Object -Last 1
        if ([string]::IsNullOrWhiteSpace($jsonLine)) {
            Add-Step -Name $StepName -ExitCode 1 -Detail "sample emitted no structured JSON; output=$($stdout.Trim())"
        }
        try {
            $evidence = $jsonLine | ConvertFrom-Json
        } catch {
            Add-Step -Name $StepName -ExitCode 1 -Detail "invalid JSON: $jsonLine"
        }
        if ($CaptureSceneRgb) {
            if (-not (Test-Path -LiteralPath $sceneRgbPath -PathType Leaf)) {
                Add-Step -Name $StepName -ExitCode 1 -Detail 'sample did not write the requested scene RGB capture'
            }
            $sceneRgbBytes = [System.IO.File]::ReadAllBytes($sceneRgbPath)
        }
        return [pscustomobject]@{
            Evidence      = $evidence
            Stdout        = $stdout.Trim()
            SceneRgbBytes = $sceneRgbBytes
        }
    } finally {
        if (-not [string]::IsNullOrEmpty($sceneRgbPath)) {
            Remove-Item -LiteralPath $sceneRgbPath -Force -ErrorAction SilentlyContinue
        }
    }
}

$sampleRun = Invoke-ProductSampleEvidence -StepName 'tina_sample_3d' -IblMode 'on' `
    -Frames $SampleFrames -ThemeDemo
$evidence = $sampleRun.Evidence
$sampleOut = $sampleRun.Stdout

$expectedResolverHits = [long]$SampleFrames * 2
$expectedFields = [ordered]@{
    status                              = 'ok'
    sample                              = 'tina_sample_3d'
    evidenceSchema                      = 13
    frames                              = $SampleFrames
    gltfCooked                          = $true
    cookedStaticMesh                    = $true
    cookedMaterial                      = $true
    cookedPrefab                        = $true
    prefabInstantiated                  = $true
    sceneExtract                        = $true
    multiMesh                           = $true
    materialTextureBound                = $true
    texturesUploaded                    = 3
    meshesUploaded                      = 2
    tangentMeshesUploaded               = 2
    cookedEnvironmentMap                = $true
    environmentMapsUploaded             = 1
    imageBasedLightingMode              = 'on'
    imageBasedLightingConfigured        = $true
    imageBasedLightingBindings          = 1
    imageBasedLightingClears            = 1
    environmentMapRetirementsAccepted   = 1
    environmentMapDiffuseFaceSize       = 2
    environmentMapSpecularFaceSize      = 4
    environmentMapSpecularMipCount      = 3
    environmentMapBrdfWidth             = 4
    environmentMapBrdfHeight            = 4
    materialsLoaded                     = 2
    prefabNodes                         = 2
    meshAssetHandlesPublished           = 2
    materialAssetHandlesPublished       = 2
    meshBindingsRegistered              = 2
    materialBindingsRegistered          = 2
    meshBindingsReleased                = 2
    materialBindingsReleased            = 2
    meshRetirementsAccepted             = 2
    textureRetirementsAccepted          = 3
    meshRetirementRecords               = 2
    textureRetirementRecords            = 3
    meshRetirementReleased              = 2
    textureRetirementReleased           = 3
    retirementRecordsLive               = 0
    meshAssetHandlesInvalidated          = 2
    materialAssetHandlesInvalidated      = 2
    textureAssetHandlesInvalidated       = 3
    meshFrameResourceResolverHits       = $expectedResolverHits
    materialFrameResourceResolverHits   = $expectedResolverHits
    assetStoreActiveCount               = 1
    prefabAssetResident                 = $true
    prefabInstances                     = 2
    meshSlotCount                       = 2
    externalGltf                        = $false
    completePbrFixture                  = $true
    materialFactorsBound                = $true
    materialMrTextureBound              = $true
    materialNormalTextureBound          = $true
    lightingConfigured                  = $true
    directionalLightCount               = 3
    cascadedDirectionalShadowCount      = 1
    submittedCascadedDirectionalShadowCount = 1
    cascadedDirectionalShadowCascadeCount = 4
    submittedCascadedDirectionalShadowCascadeCount = 4
    authoredSpotLightShadowCount        = 1
    submittedSpotLightShadowCount       = 1
    authoredPointLight3DCount           = 3
    pointLight3DCount                    = 2
    culledPointLight3DCount              = 1
    authoredSpotLight3DCount            = 3
    spotLight3DCount                    = 2
    culledSpotLight3DCount              = 1
    sceneLightingFrames                 = $SampleFrames
    submittedLightingFrames             = $SampleFrames
    submittedDirectionalLightCount      = 3
    lightingCountsStable                = $true
    logicalPixelWidth                   = 1280
    logicalPixelHeight                  = 720
    framebufferPixelWidth               = 1280
    framebufferPixelHeight              = 720
    cameraAspectChanges                 = 0
    cameraAspectMatchesSurface          = $true
    bindingRegistryReleased             = $true
    instanceBatchesPerFrame             = 2
    catalogCooked                       = 1
    stateExits                          = 1
    uiRootsCreated                      = 1
    uiRootsReleased                     = 1
    uiPanelsCreated                     = 7
    uiLabelsCreated                     = 13
    uiButtonsCreated                    = 1
    uiCheckboxesCreated                 = 1
    uiSlidersCreated                    = 1
    uiProgressBarsCreated               = 1
    uiListViewsCreated                  = 1
    uiTreeViewsCreated                  = 1
    uiThemeDemoRequested                = $true
    uiThemeSwitches                     = 2
    uiAutomatedThemeSteps               = 2
    uiAutomatedCollectionSteps          = 2
    uiTreeExpansionChanges              = 2
    uiListSelectionKey                  = 2003
    uiTreeSelectionKey                  = 4
    uiThemeButtonActivations            = 0
    uiCheckboxActivations               = 0
    uiSliderChanges                     = 0
    uiThemeInitialLight                 = $false
    uiThemeFinalLight                   = $false
    uiInheritedChromeVerified           = $true
    uiControlsInitialStateVerified      = $true
    uiResponsiveLayoutVerified          = $true
    uiAutoRotateFinal                   = $true
    uiRotationSpeedFinal                = 1
    uiProgressFinal                     = 100
    applicationShutdowns                = 1
    engineHostDestroyed                 = $true
    renderResourceLedgerBalanced        = $true
    pixelCaptureAttempted               = $true
    pixelCaptureOk                      = $true
    pixelCaptureWidth                   = 1280
    pixelCaptureHeight                  = 720
    pixelCaptureBytes                   = 3686400
    sceneRgbPixelCount                  = 191880
    sceneRgbOutputRequested             = $false
    sceneRgbOutputWritten               = $false
    pixelGoldenChecked                  = $false
    pixelGoldenMatched                  = $true
}

$evidenceErrors = [System.Collections.Generic.List[string]]::new()
foreach ($name in $expectedFields.Keys) {
    $property = $evidence.PSObject.Properties[$name]
    if ($null -eq $property) {
        $evidenceErrors.Add("missing $name")
        continue
    }
    $expected = $expectedFields[$name]
    if ($property.Value -ne $expected) {
        $evidenceErrors.Add("$name expected=$expected actual=$($property.Value)")
    }
}
if ($null -eq $evidence.PSObject.Properties['uiProgressUpdates'] -or
    [long]$evidence.uiProgressUpdates -lt $SampleFrames) {
    $evidenceErrors.Add("uiProgressUpdates expected>=$SampleFrames actual=$($evidence.uiProgressUpdates)")
}
if ($null -eq $evidence.PSObject.Properties['windowMetricsEvents'] -or
    [long]$evidence.windowMetricsEvents -lt 1) {
    $evidenceErrors.Add("windowMetricsEvents expected>=1 actual=$($evidence.windowMetricsEvents)")
}
$expectedCameraAspect = 1280.0 / 720.0
if ($null -eq $evidence.PSObject.Properties['submittedCameraAspectRatio'] -or
    [Math]::Abs([double]$evidence.submittedCameraAspectRatio - $expectedCameraAspect) -gt 0.0001) {
    $evidenceErrors.Add("submittedCameraAspectRatio expected=$expectedCameraAspect actual=$($evidence.submittedCameraAspectRatio)")
}
if ($null -eq $evidence.PSObject.Properties['pixelFingerprint'] -or
    [string]$evidence.pixelFingerprint -notmatch '^[0-9a-f]{32}$') {
    $evidenceErrors.Add("pixelFingerprint must be 32 lowercase hexadecimal characters")
}
if ($null -eq $evidence.PSObject.Properties['sceneRgbFingerprint'] -or
    [string]$evidence.sceneRgbFingerprint -notmatch '^[0-9a-f]{32}$') {
    $evidenceErrors.Add("sceneRgbFingerprint must be 32 lowercase hexadecimal characters")
}
if ($null -eq $evidence.PSObject.Properties['sceneRgbChannelSums'] -or
    @($evidence.sceneRgbChannelSums).Count -ne 3) {
    $evidenceErrors.Add("sceneRgbChannelSums must contain exactly three values")
}
if ($evidenceErrors.Count -ne 0) {
    Add-Step -Name 'productEvidence' -ExitCode 1 -Detail (($evidenceErrors -join '; ') + "; output=$sampleOut")
}

Add-Step -Name 'productEvidence' -ExitCode 0 -Detail "schema=13 frames=$SampleFrames mesh-layout=p3n3t4uv2 ibl=cooked-rgba16f-rg16f resize=surface-aspect-responsive-ui lights=directional-point-spot-culled csm=4-cascades spot-shadow=1 theme=dark-light-dark collections=list-tree"
Add-Step -Name 'tina_sample_3d' -ExitCode 0 -Detail "frames=$SampleFrames pixelFingerprint=$($evidence.pixelFingerprint)"

$iblComparisonEvidence = [ordered]@{
    on  = @()
    off = @()
}
$iblComparisonSceneRgb = [ordered]@{
    on  = @()
    off = @()
}
foreach ($mode in @('on', 'off')) {
    for ($iteration = 1; $iteration -le 2; ++$iteration) {
        $stepName = "ibl-$mode-$iteration"
        $run = Invoke-ProductSampleEvidence -StepName $stepName -IblMode $mode `
            -Frames $IblComparisonFrames -CaptureSceneRgb
        $modeEvidence = $run.Evidence
        $expectedConfigured = $mode -eq 'on'
        $expectedTransitions = if ($expectedConfigured) { 1 } else { 0 }
        $modeErrors = [System.Collections.Generic.List[string]]::new()
        $modeExpectedFields = [ordered]@{
            status                            = 'ok'
            sample                            = 'tina_sample_3d'
            evidenceSchema                    = 13
            frames                            = $IblComparisonFrames
            cookedEnvironmentMap              = $true
            environmentMapsUploaded           = 1
            imageBasedLightingMode            = $mode
            imageBasedLightingConfigured      = $expectedConfigured
            imageBasedLightingBindings        = $expectedTransitions
            imageBasedLightingClears          = $expectedTransitions
            environmentMapRetirementsAccepted = 1
            cascadedDirectionalShadowCount      = 1
            submittedCascadedDirectionalShadowCount = 1
            cascadedDirectionalShadowCascadeCount = 4
            submittedCascadedDirectionalShadowCascadeCount = 4
            authoredSpotLightShadowCount      = 1
            submittedSpotLightShadowCount     = 1
            pixelCaptureOk                    = $true
            sceneRgbPixelCount                = 191880
            sceneRgbOutputRequested           = $true
            sceneRgbOutputWritten             = $true
            renderResourceLedgerBalanced      = $true
        }
        foreach ($name in $modeExpectedFields.Keys) {
            $property = $modeEvidence.PSObject.Properties[$name]
            if ($null -eq $property) {
                $modeErrors.Add("missing $name")
                continue
            }
            $expected = $modeExpectedFields[$name]
            if ($property.Value -ne $expected) {
                $modeErrors.Add("$name expected=$expected actual=$($property.Value)")
            }
        }
        if ($null -eq $modeEvidence.PSObject.Properties['pixelFingerprint'] -or
            [string]$modeEvidence.pixelFingerprint -notmatch '^[0-9a-f]{32}$') {
            $modeErrors.Add('pixelFingerprint must be 32 lowercase hexadecimal characters')
        }
        if ($null -eq $modeEvidence.PSObject.Properties['sceneRgbFingerprint'] -or
            [string]$modeEvidence.sceneRgbFingerprint -notmatch '^[0-9a-f]{32}$') {
            $modeErrors.Add('sceneRgbFingerprint must be 32 lowercase hexadecimal characters')
        }
        if ($null -eq $modeEvidence.PSObject.Properties['sceneRgbChannelSums'] -or
            @($modeEvidence.sceneRgbChannelSums).Count -ne 3) {
            $modeErrors.Add('sceneRgbChannelSums must contain exactly three values')
        }
        if ($modeErrors.Count -ne 0) {
            Add-Step -Name $stepName -ExitCode 1 `
                -Detail (($modeErrors -join '; ') + "; output=$($run.Stdout)")
        }
        $iblComparisonEvidence[$mode] += $modeEvidence
        $iblComparisonSceneRgb[$mode] += ,$run.SceneRgbBytes
        Add-Step -Name $stepName -ExitCode 0 `
            -Detail "frames=$IblComparisonFrames pixelFingerprint=$($modeEvidence.pixelFingerprint)"
    }
}

$iblOnFingerprints = @($iblComparisonEvidence.on | ForEach-Object { [string]$_.pixelFingerprint })
$iblOffFingerprints = @($iblComparisonEvidence.off | ForEach-Object { [string]$_.pixelFingerprint })
$iblOnSceneFingerprints = @($iblComparisonEvidence.on | ForEach-Object { [string]$_.sceneRgbFingerprint })
$iblOffSceneFingerprints = @($iblComparisonEvidence.off | ForEach-Object { [string]$_.sceneRgbFingerprint })
$iblPixelErrors = [System.Collections.Generic.List[string]]::new()
if ($iblOnFingerprints[0] -ne $iblOnFingerprints[1]) {
    $iblPixelErrors.Add("IBL on fingerprint unstable: $($iblOnFingerprints -join ',')")
}
if ($iblOffFingerprints[0] -ne $iblOffFingerprints[1]) {
    $iblPixelErrors.Add("IBL off fingerprint unstable: $($iblOffFingerprints -join ',')")
}
if ($iblOnSceneFingerprints[0] -ne $iblOnSceneFingerprints[1]) {
    $iblPixelErrors.Add("IBL on scene RGB fingerprint unstable: $($iblOnSceneFingerprints -join ',')")
}
if ($iblOffSceneFingerprints[0] -ne $iblOffSceneFingerprints[1]) {
    $iblPixelErrors.Add("IBL off scene RGB fingerprint unstable: $($iblOffSceneFingerprints -join ',')")
}
if ($iblOnSceneFingerprints[0] -eq $iblOffSceneFingerprints[0]) {
    $iblPixelErrors.Add("IBL on/off scene RGB fingerprints must differ: $($iblOnSceneFingerprints[0])")
}
$iblOnRgb = [byte[]]$iblComparisonSceneRgb.on[0]
$iblOffRgb = [byte[]]$iblComparisonSceneRgb.off[0]
$expectedSceneRgbBytes = [long]$iblComparisonEvidence.on[0].sceneRgbPixelCount * 3
if ($iblOnRgb.LongLength -ne $expectedSceneRgbBytes -or
    $iblOffRgb.LongLength -ne $expectedSceneRgbBytes) {
    $iblPixelErrors.Add("IBL scene RGB byte count mismatch: expected=$expectedSceneRgbBytes on=$($iblOnRgb.LongLength) off=$($iblOffRgb.LongLength)")
}
for ($channel = 0; $channel -lt 3; ++$channel) {
    $onFirst = [long]$iblComparisonEvidence.on[0].sceneRgbChannelSums[$channel]
    $onSecond = [long]$iblComparisonEvidence.on[1].sceneRgbChannelSums[$channel]
    $offFirst = [long]$iblComparisonEvidence.off[0].sceneRgbChannelSums[$channel]
    $offSecond = [long]$iblComparisonEvidence.off[1].sceneRgbChannelSums[$channel]
    if ($onFirst -ne $onSecond) {
        $iblPixelErrors.Add("IBL on scene RGB channel $channel sum unstable: $onFirst,$onSecond")
    }
    if ($offFirst -ne $offSecond) {
        $iblPixelErrors.Add("IBL off scene RGB channel $channel sum unstable: $offFirst,$offSecond")
    }
}
$iblRgbL1Delta = [long]0
if ($iblPixelErrors.Count -eq 0) {
    for ($index = 0; $index -lt $iblOnRgb.Length; ++$index) {
        $iblRgbL1Delta += [Math]::Abs([int]$iblOnRgb[$index] - [int]$iblOffRgb[$index])
    }
}
$minimumIblRgbL1Delta = [long]$iblComparisonEvidence.on[0].sceneRgbPixelCount
if ($iblRgbL1Delta -lt $minimumIblRgbL1Delta) {
    $iblPixelErrors.Add("IBL scene RGB L1 delta too small: expected>=$minimumIblRgbL1Delta actual=$iblRgbL1Delta")
}
if ($iblPixelErrors.Count -ne 0) {
    Add-Step -Name 'iblPixelComparison' -ExitCode 1 -Detail ($iblPixelErrors -join '; ')
}
Add-Step -Name 'iblPixelComparison' -ExitCode 0 `
    -Detail "frames=$IblComparisonFrames sceneOn=$($iblOnSceneFingerprints[0]) sceneOff=$($iblOffSceneFingerprints[0]) rgbL1Delta=$iblRgbL1Delta minimum=$minimumIblRgbL1Delta stable=true different=true"

$report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
$report.sampleEvidence = $evidence
$report.sampleStdout = $sampleOut
$report.iblPixelComparison = [ordered]@{
    frames          = $IblComparisonFrames
    onFingerprints = $iblOnFingerprints
    offFingerprints = $iblOffFingerprints
    onSceneRgbFingerprints = $iblOnSceneFingerprints
    offSceneRgbFingerprints = $iblOffSceneFingerprints
    rgbL1Delta = $iblRgbL1Delta
    minimumRgbL1Delta = $minimumIblRgbL1Delta
    stable          = $true
    different       = $true
    onEvidence      = $iblComparisonEvidence.on
    offEvidence     = $iblComparisonEvidence.off
}
$report.ok = $true

if ($OutJson) {
    $dir = Split-Path -Parent $OutJson
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    ($report | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $OutJson -Encoding utf8
    Write-Output "wrote $OutJson"
}

Write-Output "product-3d gate ok schema=13 frames=$SampleFrames mesh-layout=p3n3t4uv2 ibl=on-off-pixel-differential csm=4-cascades spot-shadow=1 theme=dark-light-dark collections=list-tree"
exit 0
