<#
.SYNOPSIS
  Build and run the complete Windows product-3d gate (TEST-003 topology).

.DESCRIPTION
  Configure/build the bgfx + FreeType graph, run the affected Core, Scene,
  Asset, Render, and retained UI GoogleTest executables directly, then run the
  tina_sample_3d 300-frame product smoke with automated Dark -> Light -> Dark
  switching plus ListView/TreeView collection interaction. The final JSON is
  validated as evidence schema 16, including Animator3D, GPU skinning, and transparency. Short
  IBL on/on and off/off runs additionally prove machine-local pixel stability
  within each mode and a visible A/B change. One transparency-off run, one
  paused-skin run, and one point-shadow-off run reuse the IBL-on baseline for
  bounded transparency, animation, and shadow pixel A/B evidence.

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
    transparencyComparisonFrames = $IblComparisonFrames
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
        [Parameter(Mandatory = $true)][ValidateSet('on', 'off')][string]$PointShadowMode,
        [Parameter(Mandatory = $true)][int]$Frames,
        [ValidateSet('on', 'off')][string]$SkinAnimationMode = 'on',
        [ValidateSet('on', 'off')][string]$TransparencyMode = 'on',
        [switch]$ThemeDemo,
        [switch]$CaptureSceneRgb
    )

    $arguments = @(
        "--frames=$Frames",
        '--frame-delay-ms=0',
        '--ui-theme=dark',
        "--ibl=$IblMode",
        "--point-shadow=$PointShadowMode",
        "--skin-animation=$SkinAnimationMode",
        "--transparency=$TransparencyMode"
    )
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

$expectedLogicalPixelWidth = [long]1280
$expectedLogicalPixelHeight = [long]720

function Test-PixelCaptureContract {
    param(
        [Parameter(Mandatory = $true)][psobject]$Evidence,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Errors,
        [Parameter(Mandatory = $true)][string]$Label,
        [long]$ExpectedFramebufferWidth = 0,
        [long]$ExpectedFramebufferHeight = 0,
        [long]$ExpectedSceneRgbPixelCount = 0
    )

    $requiredFields = @(
        'logicalPixelWidth',
        'logicalPixelHeight',
        'framebufferPixelWidth',
        'framebufferPixelHeight',
        'pixelCaptureWidth',
        'pixelCaptureHeight',
        'pixelCaptureBytes',
        'sceneRgbPixelCount'
    )
    $missingField = $false
    foreach ($name in $requiredFields) {
        if ($null -eq $Evidence.PSObject.Properties[$name]) {
            $Errors.Add("$Label missing $name")
            $missingField = $true
        }
    }
    if ($missingField) {
        return $null
    }

    $logicalWidth = [long]$Evidence.logicalPixelWidth
    $logicalHeight = [long]$Evidence.logicalPixelHeight
    $framebufferWidth = [long]$Evidence.framebufferPixelWidth
    $framebufferHeight = [long]$Evidence.framebufferPixelHeight
    $captureWidth = [long]$Evidence.pixelCaptureWidth
    $captureHeight = [long]$Evidence.pixelCaptureHeight
    $captureBytes = [long]$Evidence.pixelCaptureBytes
    $sceneRgbPixelCount = [long]$Evidence.sceneRgbPixelCount

    if ($logicalWidth -ne $script:expectedLogicalPixelWidth -or
        $logicalHeight -ne $script:expectedLogicalPixelHeight) {
        $Errors.Add(
            "$Label logical extent expected=$($script:expectedLogicalPixelWidth)x$($script:expectedLogicalPixelHeight) actual=${logicalWidth}x${logicalHeight}")
    }
    if ($framebufferWidth -lt 1 -or $framebufferHeight -lt 1) {
        $Errors.Add("$Label framebuffer extent must be positive: ${framebufferWidth}x${framebufferHeight}")
        return $null
    }
    if ($captureWidth -ne $framebufferWidth -or $captureHeight -ne $framebufferHeight) {
        $Errors.Add(
            "$Label capture extent must match framebuffer: framebuffer=${framebufferWidth}x${framebufferHeight} capture=${captureWidth}x${captureHeight}")
    }

    $expectedCaptureBytes = [long]$captureWidth * [long]$captureHeight * [long]4
    if ($captureBytes -ne $expectedCaptureBytes) {
        $Errors.Add("$Label capture byte count expected=$expectedCaptureBytes actual=$captureBytes")
    }

    $sceneLeft = [long][Math]::Floor([double]$captureWidth / 4.0)
    $sceneRight = [long][Math]::Floor(([double]$captureWidth * 2.0) / 3.0)
    $sceneTop = [long][Math]::Floor([double]$captureHeight / 4.0)
    $sceneBottom = [long][Math]::Floor(([double]$captureHeight * 3.0) / 4.0)
    $calculatedSceneRgbPixelCount =
        [long]($sceneRight - $sceneLeft) * [long]($sceneBottom - $sceneTop)
    if ($sceneRgbPixelCount -ne $calculatedSceneRgbPixelCount) {
        $Errors.Add(
            "$Label scene RGB ROI expected=$calculatedSceneRgbPixelCount actual=$sceneRgbPixelCount")
    }
    if ($ExpectedFramebufferWidth -gt 0 -and $framebufferWidth -ne $ExpectedFramebufferWidth) {
        $Errors.Add(
            "$Label framebuffer width expected=$ExpectedFramebufferWidth actual=$framebufferWidth")
    }
    if ($ExpectedFramebufferHeight -gt 0 -and $framebufferHeight -ne $ExpectedFramebufferHeight) {
        $Errors.Add(
            "$Label framebuffer height expected=$ExpectedFramebufferHeight actual=$framebufferHeight")
    }
    if ($ExpectedSceneRgbPixelCount -gt 0 -and
        $sceneRgbPixelCount -ne $ExpectedSceneRgbPixelCount) {
        $Errors.Add(
            "$Label scene RGB ROI expected=$ExpectedSceneRgbPixelCount actual=$sceneRgbPixelCount")
    }

    return [pscustomobject]@{
        FramebufferWidth      = $framebufferWidth
        FramebufferHeight     = $framebufferHeight
        SceneRgbPixelCount    = $sceneRgbPixelCount
        ExpectedSceneRgbBytes = [long]$sceneRgbPixelCount * [long]3
    }
}

function Test-TransparencyEvidenceContract {
    param(
        [Parameter(Mandatory = $true)][psobject]$Evidence,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Errors,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][ValidateSet('on', 'off')]
        [string]$ExpectedMode,
        [Parameter(Mandatory = $true)][long]$ExpectedFrames
    )

    $enabled = $ExpectedMode -eq 'on'
    $expectedWitnessCount = if ($enabled) { 2 } else { 0 }
    $expectedSubmittedFrames = if ($enabled) { $ExpectedFrames } else { 0 }
    $expectedFields = [ordered]@{
        transparencyMode                          = $ExpectedMode
        blendMaterialCount                        = 1
        authoredTransparentStaticWitnessCount     = $expectedWitnessCount
        transparentWitnessMaterialBound           = $true
        submittedTransparent3DFrames               = $expectedSubmittedFrames
        submittedTransparentStaticMesh3DCount      = $expectedWitnessCount
        submittedTransparentSkinnedMesh3DCount     = 0
        submittedTransparent3DDrawCount            = $expectedWitnessCount
        transparent3DSortOrderStable               = $true
    }
    foreach ($name in $expectedFields.Keys) {
        $property = $Evidence.PSObject.Properties[$name]
        if ($null -eq $property) {
            $Errors.Add("$Label missing $name")
            continue
        }
        $expected = $expectedFields[$name]
        if ($property.Value -ne $expected) {
            $Errors.Add("$Label $name expected=$expected actual=$($property.Value)")
        }
    }

    $checksumProperty = $Evidence.PSObject.Properties['submittedTransparent3DSortOrderChecksum']
    if ($null -eq $checksumProperty) {
        $Errors.Add("$Label missing submittedTransparent3DSortOrderChecksum")
    } else {
        $checksum = [decimal]$checksumProperty.Value
        if ($enabled -and $checksum -eq 0) {
            $Errors.Add("$Label submittedTransparent3DSortOrderChecksum must be non-zero")
        }
        if (-not $enabled -and $checksum -ne 0) {
            $Errors.Add(
                "$Label submittedTransparent3DSortOrderChecksum expected=0 actual=$($checksumProperty.Value)")
        }
    }
}

$sampleRun = Invoke-ProductSampleEvidence -StepName 'tina_sample_3d' -IblMode 'on' `
    -PointShadowMode 'on' -SkinAnimationMode 'on' -Frames $SampleFrames -ThemeDemo
$evidence = $sampleRun.Evidence
$sampleOut = $sampleRun.Stdout

$expectedStaticMeshResolverHits = [long]$SampleFrames * 4
$expectedSkinnedMeshResolverHits = [long]$SampleFrames
$expectedMaterialResolverHits = [long]$SampleFrames * 5
$expectedSkinnedPoseFingerprintChanges = [long]$SampleFrames - 1
$expectedFields = [ordered]@{
    status                              = 'ok'
    sample                              = 'tina_sample_3d'
    evidenceSchema                      = 16
    frames                              = $SampleFrames
    gltfCooked                          = $true
    cookedStaticMesh                    = $true
    cookedSkinnedMesh                   = $true
    cookedAnimationClip3D               = $true
    cookedMaterial                      = $true
    cookedPrefab                        = $true
    prefabInstantiated                  = $true
    sceneExtract                        = $true
    multiMesh                           = $true
    materialTextureBound                = $true
    texturesUploaded                    = 3
    meshesUploaded                      = 3
    tangentMeshesUploaded               = 2
    skinnedMeshesUploaded               = 1
    uploadedSkinnedJointCount           = 2
    animatorJointCount                  = 2
    animatorUpdates                     = $SampleFrames
    animatorPoseChanges                 = $SampleFrames
    submittedSkinnedMesh3DFrames        = $SampleFrames
    submittedSkinnedMesh3DCount         = 1
    visibleSkinnedMesh3DCount           = 1
    submittedSkinnedPaletteJointCount   = 2
    submittedSkinnedPoseFingerprintChanges = $expectedSkinnedPoseFingerprintChanges
    cookedEnvironmentMap                = $true
    environmentMapsUploaded             = 1
    imageBasedLightingMode              = 'on'
    pointLightShadowMode                = 'on'
    skinAnimationMode                   = 'on'
    imageBasedLightingConfigured        = $true
    imageBasedLightingBindings          = 1
    imageBasedLightingClears            = 1
    environmentMapRetirementsAccepted   = 1
    environmentMapDiffuseFaceSize       = 2
    environmentMapSpecularFaceSize      = 4
    environmentMapSpecularMipCount      = 3
    environmentMapBrdfWidth             = 4
    environmentMapBrdfHeight            = 4
    materialsLoaded                     = 4
    prefabNodes                         = 2
    meshAssetHandlesPublished           = 3
    materialAssetHandlesPublished       = 4
    meshBindingsRegistered              = 3
    materialBindingsRegistered          = 4
    meshBindingsReleased                = 3
    materialBindingsReleased            = 4
    meshRetirementsAccepted             = 3
    textureRetirementsAccepted          = 3
    meshRetirementRecords               = 3
    textureRetirementRecords            = 3
    meshRetirementReleased              = 3
    textureRetirementReleased           = 3
    retirementRecordsLive               = 0
    meshAssetHandlesInvalidated          = 3
    materialAssetHandlesInvalidated      = 4
    textureAssetHandlesInvalidated       = 3
    animationClipAssetHandlesPublished  = 1
    animationClipAssetHandlesInvalidated = 1
    skinnedPrefabAssetHandlesPublished  = 1
    skinnedPrefabAssetHandlesInvalidated = 1
    meshFrameResourceResolverHits       = $expectedStaticMeshResolverHits
    skinnedMeshFrameResourceResolverHits = $expectedSkinnedMeshResolverHits
    skinnedPoseProviderHits             = $expectedSkinnedMeshResolverHits
    materialFrameResourceResolverHits   = $expectedMaterialResolverHits
    assetStoreActiveCount               = 1
    prefabAssetResident                 = $true
    prefabInstances                     = 2
    skinnedPrefabInstances              = 3
    meshSlotCount                       = 3
    staticMeshSlotCount                 = 2
    skinnedMeshSlotCount                = 1
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
    authoredPointLightShadowCount       = 1
    submittedPointLightShadowCount      = 1
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
$mainCaptureContract = Test-PixelCaptureContract -Evidence $evidence -Errors $evidenceErrors `
    -Label 'tina_sample_3d'
Test-TransparencyEvidenceContract -Evidence $evidence -Errors $evidenceErrors `
    -Label 'tina_sample_3d' -ExpectedMode 'on' -ExpectedFrames $SampleFrames
if ($null -eq $evidence.PSObject.Properties['uiProgressUpdates'] -or
    [long]$evidence.uiProgressUpdates -lt $SampleFrames) {
    $evidenceErrors.Add("uiProgressUpdates expected>=$SampleFrames actual=$($evidence.uiProgressUpdates)")
}
if ($null -eq $evidence.PSObject.Properties['windowMetricsEvents'] -or
    [long]$evidence.windowMetricsEvents -lt 1) {
    $evidenceErrors.Add("windowMetricsEvents expected>=1 actual=$($evidence.windowMetricsEvents)")
}
$expectedCameraAspect = [double]$expectedLogicalPixelWidth / [double]$expectedLogicalPixelHeight
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
foreach ($name in @('firstSubmittedSkinnedPoseFingerprint', 'submittedSkinnedPoseFingerprint')) {
    $property = $evidence.PSObject.Properties[$name]
    if ($null -eq $property -or [decimal]$property.Value -eq 0) {
        $evidenceErrors.Add("$name must be non-zero")
    }
}
if ($evidenceErrors.Count -ne 0) {
    Add-Step -Name 'productEvidence' -ExitCode 1 -Detail (($evidenceErrors -join '; ') + "; output=$sampleOut")
}

Add-Step -Name 'productEvidence' -ExitCode 0 -Detail "schema=16 frames=$SampleFrames mesh-layout=static-p3n3t4uv2+skinned-j4w4 animator=cpu-pose gpu-skinning=palette transparency=sorted-alpha-blend ibl=cooked-rgba16f-rg16f resize=surface-aspect-responsive-ui lights=directional-point-spot-culled csm=4-cascades spot-shadow=1 point-shadow=1 theme=dark-light-dark collections=list-tree"
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
            -PointShadowMode 'on' -SkinAnimationMode 'on' `
            -Frames $IblComparisonFrames -CaptureSceneRgb
        $modeEvidence = $run.Evidence
        $expectedConfigured = $mode -eq 'on'
        $expectedTransitions = if ($expectedConfigured) { 1 } else { 0 }
        $modeErrors = [System.Collections.Generic.List[string]]::new()
        $modeExpectedFields = [ordered]@{
            status                            = 'ok'
            sample                            = 'tina_sample_3d'
            evidenceSchema                    = 16
            frames                            = $IblComparisonFrames
            cookedEnvironmentMap              = $true
            environmentMapsUploaded           = 1
            imageBasedLightingMode            = $mode
            pointLightShadowMode              = 'on'
            skinAnimationMode                 = 'on'
            imageBasedLightingConfigured      = $expectedConfigured
            imageBasedLightingBindings        = $expectedTransitions
            imageBasedLightingClears          = $expectedTransitions
            environmentMapRetirementsAccepted = 1
            meshesUploaded                    = 3
            tangentMeshesUploaded             = 2
            skinnedMeshesUploaded             = 1
            animatorJointCount                = 2
            animatorUpdates                   = $IblComparisonFrames
            animatorPoseChanges               = $IblComparisonFrames
            submittedSkinnedMesh3DFrames      = $IblComparisonFrames
            submittedSkinnedMesh3DCount       = 1
            visibleSkinnedMesh3DCount         = 1
            submittedSkinnedPaletteJointCount = 2
            submittedSkinnedPoseFingerprintChanges = ([long]$IblComparisonFrames - 1)
            cascadedDirectionalShadowCount      = 1
            submittedCascadedDirectionalShadowCount = 1
            cascadedDirectionalShadowCascadeCount = 4
            submittedCascadedDirectionalShadowCascadeCount = 4
            authoredSpotLightShadowCount      = 1
            submittedSpotLightShadowCount     = 1
            authoredPointLightShadowCount     = 1
            submittedPointLightShadowCount    = 1
            pixelCaptureOk                    = $true
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
        $modeCaptureContract = Test-PixelCaptureContract -Evidence $modeEvidence `
            -Errors $modeErrors -Label $stepName `
            -ExpectedFramebufferWidth $mainCaptureContract.FramebufferWidth `
            -ExpectedFramebufferHeight $mainCaptureContract.FramebufferHeight `
            -ExpectedSceneRgbPixelCount $mainCaptureContract.SceneRgbPixelCount
        if ($null -ne $modeCaptureContract -and
            ($null -eq $run.SceneRgbBytes -or
             $run.SceneRgbBytes.LongLength -ne $modeCaptureContract.ExpectedSceneRgbBytes)) {
            $actualSceneRgbBytes = if ($null -eq $run.SceneRgbBytes) {
                0
            } else {
                $run.SceneRgbBytes.LongLength
            }
            $modeErrors.Add(
                "$stepName scene RGB file byte count expected=$($modeCaptureContract.ExpectedSceneRgbBytes) actual=$actualSceneRgbBytes")
        }
        Test-TransparencyEvidenceContract -Evidence $modeEvidence -Errors $modeErrors `
            -Label $stepName -ExpectedMode 'on' -ExpectedFrames $IblComparisonFrames
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

$transparencyOffRun = Invoke-ProductSampleEvidence -StepName 'transparency-off' `
    -IblMode 'on' -PointShadowMode 'on' -SkinAnimationMode 'on' -TransparencyMode 'off' `
    -Frames $IblComparisonFrames -CaptureSceneRgb
$transparencyOffEvidence = $transparencyOffRun.Evidence
$transparencyErrors = [System.Collections.Generic.List[string]]::new()
$transparencyOffExpectedFields = [ordered]@{
    status                                  = 'ok'
    sample                                  = 'tina_sample_3d'
    evidenceSchema                          = 16
    frames                                  = $IblComparisonFrames
    imageBasedLightingMode                  = 'on'
    pointLightShadowMode                    = 'on'
    skinAnimationMode                       = 'on'
    imageBasedLightingConfigured            = $true
    meshesUploaded                          = 3
    tangentMeshesUploaded                   = 2
    skinnedMeshesUploaded                   = 1
    materialsLoaded                         = 4
    materialAssetHandlesPublished           = 4
    materialBindingsRegistered              = 4
    materialBindingsReleased                = 4
    materialAssetHandlesInvalidated         = 4
    animatorJointCount                      = 2
    animatorUpdates                         = $IblComparisonFrames
    animatorPoseChanges                     = $IblComparisonFrames
    submittedSkinnedMesh3DFrames            = $IblComparisonFrames
    submittedSkinnedMesh3DCount             = 1
    visibleSkinnedMesh3DCount               = 1
    submittedSkinnedPaletteJointCount       = 2
    submittedSkinnedPoseFingerprintChanges  = ([long]$IblComparisonFrames - 1)
    meshFrameResourceResolverHits           = ([long]$IblComparisonFrames * 2)
    skinnedMeshFrameResourceResolverHits    = [long]$IblComparisonFrames
    skinnedPoseProviderHits                 = [long]$IblComparisonFrames
    materialFrameResourceResolverHits       = ([long]$IblComparisonFrames * 3)
    pixelCaptureOk                          = $true
    sceneRgbOutputRequested                 = $true
    sceneRgbOutputWritten                   = $true
    renderResourceLedgerBalanced            = $true
}
foreach ($name in $transparencyOffExpectedFields.Keys) {
    $property = $transparencyOffEvidence.PSObject.Properties[$name]
    if ($null -eq $property) {
        $transparencyErrors.Add("transparency-off missing $name")
        continue
    }
    $expected = $transparencyOffExpectedFields[$name]
    if ($property.Value -ne $expected) {
        $transparencyErrors.Add(
            "transparency-off $name expected=$expected actual=$($property.Value)")
    }
}
$transparencyOffCaptureContract = Test-PixelCaptureContract -Evidence $transparencyOffEvidence `
    -Errors $transparencyErrors -Label 'transparency-off' `
    -ExpectedFramebufferWidth $mainCaptureContract.FramebufferWidth `
    -ExpectedFramebufferHeight $mainCaptureContract.FramebufferHeight `
    -ExpectedSceneRgbPixelCount $mainCaptureContract.SceneRgbPixelCount
Test-TransparencyEvidenceContract -Evidence $transparencyOffEvidence -Errors $transparencyErrors `
    -Label 'transparency-off' -ExpectedMode 'off' -ExpectedFrames $IblComparisonFrames
if ($null -ne $transparencyOffCaptureContract -and
    ($null -eq $transparencyOffRun.SceneRgbBytes -or
     $transparencyOffRun.SceneRgbBytes.LongLength -ne
        $transparencyOffCaptureContract.ExpectedSceneRgbBytes)) {
    $actualSceneRgbBytes = if ($null -eq $transparencyOffRun.SceneRgbBytes) {
        0
    } else {
        $transparencyOffRun.SceneRgbBytes.LongLength
    }
    $transparencyErrors.Add(
        "transparency-off scene RGB file byte count expected=$($transparencyOffCaptureContract.ExpectedSceneRgbBytes) actual=$actualSceneRgbBytes")
}

$transparencyOnEvidence = $iblComparisonEvidence.on[0]
$transparencyOnRgb = [byte[]]$iblComparisonSceneRgb.on[0]
$transparencyOffRgb = [byte[]]$transparencyOffRun.SceneRgbBytes
$transparencyOnFingerprint = [string]$transparencyOnEvidence.sceneRgbFingerprint
$transparencyOffFingerprint = [string]$transparencyOffEvidence.sceneRgbFingerprint
if ($transparencyOnFingerprint -eq $transparencyOffFingerprint) {
    $transparencyErrors.Add(
        "Transparency on/off scene RGB fingerprints must differ: $transparencyOnFingerprint")
}
$expectedTransparencyRgbBytes = [long]$transparencyOnEvidence.sceneRgbPixelCount * 3
if ($transparencyOnRgb.LongLength -ne $expectedTransparencyRgbBytes -or
    $transparencyOffRgb.LongLength -ne $expectedTransparencyRgbBytes) {
    $transparencyErrors.Add(
        "Transparency scene RGB byte count mismatch: expected=$expectedTransparencyRgbBytes on=$($transparencyOnRgb.LongLength) off=$($transparencyOffRgb.LongLength)")
}
$transparencyRgbL1Delta = [long]0
if ($transparencyErrors.Count -eq 0) {
    for ($index = 0; $index -lt $transparencyOnRgb.Length; ++$index) {
        $transparencyRgbL1Delta +=
            [Math]::Abs([int]$transparencyOnRgb[$index] - [int]$transparencyOffRgb[$index])
    }
}
$minimumTransparencyRgbL1Delta = [long]$transparencyOnEvidence.sceneRgbPixelCount
if ($transparencyRgbL1Delta -lt $minimumTransparencyRgbL1Delta) {
    $transparencyErrors.Add(
        "Transparency scene RGB L1 delta too small: expected>=$minimumTransparencyRgbL1Delta actual=$transparencyRgbL1Delta")
}
if ($transparencyErrors.Count -ne 0) {
    Add-Step -Name 'transparencyPixelComparison' -ExitCode 1 `
        -Detail (($transparencyErrors -join '; ') + "; output=$($transparencyOffRun.Stdout)")
}
Add-Step -Name 'transparencyPixelComparison' -ExitCode 0 `
    -Detail "frames=$IblComparisonFrames sceneOn=$transparencyOnFingerprint sceneOff=$transparencyOffFingerprint rgbL1Delta=$transparencyRgbL1Delta minimum=$minimumTransparencyRgbL1Delta different=true"

$skinAnimationOffRun = Invoke-ProductSampleEvidence -StepName 'skin-animation-off' `
    -IblMode 'on' -PointShadowMode 'on' -SkinAnimationMode 'off' `
    -Frames $IblComparisonFrames -CaptureSceneRgb
$skinAnimationOffEvidence = $skinAnimationOffRun.Evidence
$skinAnimationErrors = [System.Collections.Generic.List[string]]::new()
$skinAnimationOffExpectedFields = [ordered]@{
    status                                  = 'ok'
    sample                                  = 'tina_sample_3d'
    evidenceSchema                          = 16
    frames                                  = $IblComparisonFrames
    imageBasedLightingMode                  = 'on'
    pointLightShadowMode                    = 'on'
    skinAnimationMode                       = 'off'
    imageBasedLightingConfigured            = $true
    meshesUploaded                          = 3
    tangentMeshesUploaded                   = 2
    skinnedMeshesUploaded                   = 1
    animatorJointCount                      = 2
    animatorUpdates                         = $IblComparisonFrames
    animatorPoseChanges                     = 0
    submittedSkinnedMesh3DFrames            = $IblComparisonFrames
    submittedSkinnedMesh3DCount             = 1
    visibleSkinnedMesh3DCount               = 1
    submittedSkinnedPaletteJointCount       = 2
    submittedSkinnedPoseFingerprintChanges  = 0
    pixelCaptureOk                          = $true
    sceneRgbOutputRequested                 = $true
    sceneRgbOutputWritten                   = $true
    renderResourceLedgerBalanced            = $true
}
foreach ($name in $skinAnimationOffExpectedFields.Keys) {
    $property = $skinAnimationOffEvidence.PSObject.Properties[$name]
    if ($null -eq $property) {
        $skinAnimationErrors.Add("skin-animation-off missing $name")
        continue
    }
    $expected = $skinAnimationOffExpectedFields[$name]
    if ($property.Value -ne $expected) {
        $skinAnimationErrors.Add("skin-animation-off $name expected=$expected actual=$($property.Value)")
    }
}
$null = Test-PixelCaptureContract -Evidence $skinAnimationOffEvidence `
    -Errors $skinAnimationErrors -Label 'skin-animation-off' `
    -ExpectedFramebufferWidth $mainCaptureContract.FramebufferWidth `
    -ExpectedFramebufferHeight $mainCaptureContract.FramebufferHeight `
    -ExpectedSceneRgbPixelCount $mainCaptureContract.SceneRgbPixelCount
Test-TransparencyEvidenceContract -Evidence $skinAnimationOffEvidence `
    -Errors $skinAnimationErrors -Label 'skin-animation-off' `
    -ExpectedMode 'on' -ExpectedFrames $IblComparisonFrames
foreach ($name in @('firstSubmittedSkinnedPoseFingerprint', 'submittedSkinnedPoseFingerprint')) {
    $property = $skinAnimationOffEvidence.PSObject.Properties[$name]
    if ($null -eq $property -or [decimal]$property.Value -eq 0) {
        $skinAnimationErrors.Add("skin-animation-off $name must be non-zero")
    }
}

$skinAnimationOnEvidence = $iblComparisonEvidence.on[0]
$skinAnimationOnRgb = [byte[]]$iblComparisonSceneRgb.on[0]
$skinAnimationOffRgb = [byte[]]$skinAnimationOffRun.SceneRgbBytes
$skinAnimationOnFingerprint = [string]$skinAnimationOnEvidence.sceneRgbFingerprint
$skinAnimationOffFingerprint = [string]$skinAnimationOffEvidence.sceneRgbFingerprint
if ($skinAnimationOnFingerprint -eq $skinAnimationOffFingerprint) {
    $skinAnimationErrors.Add(
        "Skin animation on/off scene RGB fingerprints must differ: $skinAnimationOnFingerprint")
}
$expectedSkinAnimationRgbBytes = [long]$skinAnimationOnEvidence.sceneRgbPixelCount * 3
if ($skinAnimationOnRgb.LongLength -ne $expectedSkinAnimationRgbBytes -or
    $skinAnimationOffRgb.LongLength -ne $expectedSkinAnimationRgbBytes) {
    $skinAnimationErrors.Add(
        "Skin animation scene RGB byte count mismatch: expected=$expectedSkinAnimationRgbBytes on=$($skinAnimationOnRgb.LongLength) off=$($skinAnimationOffRgb.LongLength)")
}
$skinAnimationRgbL1Delta = [long]0
if ($skinAnimationErrors.Count -eq 0) {
    for ($index = 0; $index -lt $skinAnimationOnRgb.Length; ++$index) {
        $skinAnimationRgbL1Delta +=
            [Math]::Abs([int]$skinAnimationOnRgb[$index] - [int]$skinAnimationOffRgb[$index])
    }
}
$minimumSkinAnimationRgbL1Delta = [long]$skinAnimationOnEvidence.sceneRgbPixelCount
if ($skinAnimationRgbL1Delta -lt $minimumSkinAnimationRgbL1Delta) {
    $skinAnimationErrors.Add(
        "Skin animation scene RGB L1 delta too small: expected>=$minimumSkinAnimationRgbL1Delta actual=$skinAnimationRgbL1Delta")
}
if ($skinAnimationErrors.Count -ne 0) {
    Add-Step -Name 'skinAnimationPixelComparison' -ExitCode 1 `
        -Detail (($skinAnimationErrors -join '; ') + "; output=$($skinAnimationOffRun.Stdout)")
}
Add-Step -Name 'skinAnimationPixelComparison' -ExitCode 0 `
    -Detail "frames=$IblComparisonFrames sceneOn=$skinAnimationOnFingerprint sceneOff=$skinAnimationOffFingerprint rgbL1Delta=$skinAnimationRgbL1Delta minimum=$minimumSkinAnimationRgbL1Delta different=true"

$pointShadowOffRun = Invoke-ProductSampleEvidence -StepName 'point-shadow-off' `
    -IblMode 'on' -PointShadowMode 'off' -SkinAnimationMode 'on' `
    -Frames $IblComparisonFrames -CaptureSceneRgb
$pointShadowOffEvidence = $pointShadowOffRun.Evidence
$pointShadowErrors = [System.Collections.Generic.List[string]]::new()
$pointShadowOffExpectedFields = [ordered]@{
    status                          = 'ok'
    sample                          = 'tina_sample_3d'
    evidenceSchema                  = 16
    frames                          = $IblComparisonFrames
    imageBasedLightingMode          = 'on'
    imageBasedLightingConfigured    = $true
    pointLightShadowMode            = 'off'
    skinAnimationMode               = 'on'
    authoredPointLightShadowCount   = 0
    submittedPointLightShadowCount  = 0
    authoredPointLight3DCount       = 3
    pointLight3DCount               = 2
    meshesUploaded                  = 3
    tangentMeshesUploaded           = 2
    skinnedMeshesUploaded           = 1
    animatorJointCount              = 2
    animatorUpdates                 = $IblComparisonFrames
    animatorPoseChanges             = $IblComparisonFrames
    submittedSkinnedMesh3DFrames    = $IblComparisonFrames
    submittedSkinnedMesh3DCount     = 1
    visibleSkinnedMesh3DCount       = 1
    submittedSkinnedPaletteJointCount = 2
    submittedSkinnedPoseFingerprintChanges = ([long]$IblComparisonFrames - 1)
    pixelCaptureOk                  = $true
    sceneRgbOutputRequested         = $true
    sceneRgbOutputWritten           = $true
    renderResourceLedgerBalanced    = $true
}
foreach ($name in $pointShadowOffExpectedFields.Keys) {
    $property = $pointShadowOffEvidence.PSObject.Properties[$name]
    if ($null -eq $property) {
        $pointShadowErrors.Add("point-shadow-off missing $name")
        continue
    }
    $expected = $pointShadowOffExpectedFields[$name]
    if ($property.Value -ne $expected) {
        $pointShadowErrors.Add("point-shadow-off $name expected=$expected actual=$($property.Value)")
    }
}
$null = Test-PixelCaptureContract -Evidence $pointShadowOffEvidence `
    -Errors $pointShadowErrors -Label 'point-shadow-off' `
    -ExpectedFramebufferWidth $mainCaptureContract.FramebufferWidth `
    -ExpectedFramebufferHeight $mainCaptureContract.FramebufferHeight `
    -ExpectedSceneRgbPixelCount $mainCaptureContract.SceneRgbPixelCount
Test-TransparencyEvidenceContract -Evidence $pointShadowOffEvidence `
    -Errors $pointShadowErrors -Label 'point-shadow-off' `
    -ExpectedMode 'on' -ExpectedFrames $IblComparisonFrames

$pointShadowOnEvidence = $iblComparisonEvidence.on[0]
$pointShadowOnRgb = [byte[]]$iblComparisonSceneRgb.on[0]
$pointShadowOffRgb = [byte[]]$pointShadowOffRun.SceneRgbBytes
$pointShadowOnFingerprint = [string]$pointShadowOnEvidence.sceneRgbFingerprint
$pointShadowOffFingerprint = [string]$pointShadowOffEvidence.sceneRgbFingerprint
if ($pointShadowOnFingerprint -eq $pointShadowOffFingerprint) {
    $pointShadowErrors.Add("Point shadow on/off scene RGB fingerprints must differ: $pointShadowOnFingerprint")
}
$expectedPointShadowRgbBytes = [long]$pointShadowOnEvidence.sceneRgbPixelCount * 3
if ($pointShadowOnRgb.LongLength -ne $expectedPointShadowRgbBytes -or
    $pointShadowOffRgb.LongLength -ne $expectedPointShadowRgbBytes) {
    $pointShadowErrors.Add("Point shadow scene RGB byte count mismatch: expected=$expectedPointShadowRgbBytes on=$($pointShadowOnRgb.LongLength) off=$($pointShadowOffRgb.LongLength)")
}
$pointShadowRgbL1Delta = [long]0
if ($pointShadowErrors.Count -eq 0) {
    for ($index = 0; $index -lt $pointShadowOnRgb.Length; ++$index) {
        $pointShadowRgbL1Delta +=
            [Math]::Abs([int]$pointShadowOnRgb[$index] - [int]$pointShadowOffRgb[$index])
    }
}
if ($pointShadowRgbL1Delta -lt 1) {
    $pointShadowErrors.Add("Point shadow scene RGB L1 delta must be positive: actual=$pointShadowRgbL1Delta")
}
if ($pointShadowErrors.Count -ne 0) {
    Add-Step -Name 'pointShadowPixelComparison' -ExitCode 1 `
        -Detail (($pointShadowErrors -join '; ') + "; output=$($pointShadowOffRun.Stdout)")
}
Add-Step -Name 'pointShadowPixelComparison' -ExitCode 0 `
    -Detail "frames=$IblComparisonFrames sceneOn=$pointShadowOnFingerprint sceneOff=$pointShadowOffFingerprint rgbL1Delta=$pointShadowRgbL1Delta different=true"

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
$report.transparencyPixelComparison = [ordered]@{
    frames                = $IblComparisonFrames
    onSceneRgbFingerprint = $transparencyOnFingerprint
    offSceneRgbFingerprint = $transparencyOffFingerprint
    rgbL1Delta            = $transparencyRgbL1Delta
    minimumRgbL1Delta     = $minimumTransparencyRgbL1Delta
    different             = $true
    onEvidence            = $transparencyOnEvidence
    offEvidence           = $transparencyOffEvidence
}
$report.pointShadowPixelComparison = [ordered]@{
    frames              = $IblComparisonFrames
    onSceneRgbFingerprint = $pointShadowOnFingerprint
    offSceneRgbFingerprint = $pointShadowOffFingerprint
    rgbL1Delta          = $pointShadowRgbL1Delta
    different           = $true
    onEvidence          = $pointShadowOnEvidence
    offEvidence         = $pointShadowOffEvidence
}
$report.skinAnimationPixelComparison = [ordered]@{
    frames                = $IblComparisonFrames
    onSceneRgbFingerprint = $skinAnimationOnFingerprint
    offSceneRgbFingerprint = $skinAnimationOffFingerprint
    rgbL1Delta            = $skinAnimationRgbL1Delta
    minimumRgbL1Delta     = $minimumSkinAnimationRgbL1Delta
    different             = $true
    onEvidence            = $skinAnimationOnEvidence
    offEvidence           = $skinAnimationOffEvidence
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

Write-Output "product-3d gate ok schema=16 frames=$SampleFrames mesh-layout=static-p3n3t4uv2+skinned-j4w4 animator=cpu-pose gpu-skinning=palette transparency=on-off-pixel-differential skin-animation=on-off-pixel-differential ibl=on-off-pixel-differential csm=4-cascades spot-shadow=1 point-shadow=on-off-pixel-differential theme=dark-light-dark collections=list-tree"
exit 0
