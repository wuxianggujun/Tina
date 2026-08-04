<#
.SYNOPSIS
  Build and run the complete Windows product-3d gate (TEST-003 topology).

.DESCRIPTION
  Configure/build the bgfx + FreeType graph, run the affected Core, Scene,
  Asset, Render, and retained UI GoogleTest executables directly, then run the
  tina_sample_3d 300-frame product smoke with automated Dark -> Light -> Dark
  switching plus ListView/TreeView collection interaction. The final JSON is
  validated as evidence schema 11. Short IBL on/on and off/off runs additionally
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
        [switch]$ThemeDemo
    )

    $arguments = @("--frames=$Frames", '--frame-delay-ms=0', '--ui-theme=dark', "--ibl=$IblMode")
    if ($ThemeDemo) {
        $arguments += '--ui-theme-demo'
    }
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
    return [pscustomobject]@{
        Evidence = $evidence
        Stdout   = $stdout.Trim()
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
    evidenceSchema                      = 11
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
    directionalShadowCasterCount        = 1
    submittedDirectionalShadowCasterCount = 1
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
if ($evidenceErrors.Count -ne 0) {
    Add-Step -Name 'productEvidence' -ExitCode 1 -Detail (($evidenceErrors -join '; ') + "; output=$sampleOut")
}

Add-Step -Name 'productEvidence' -ExitCode 0 -Detail "schema=11 frames=$SampleFrames mesh-layout=p3n3t4uv2 ibl=cooked-rgba16f-rg16f resize=surface-aspect-responsive-ui lights=directional-point-spot-culled theme=dark-light-dark collections=list-tree"
Add-Step -Name 'tina_sample_3d' -ExitCode 0 -Detail "frames=$SampleFrames pixelFingerprint=$($evidence.pixelFingerprint)"

$iblComparisonEvidence = [ordered]@{
    on  = @()
    off = @()
}
foreach ($mode in @('on', 'off')) {
    for ($iteration = 1; $iteration -le 2; ++$iteration) {
        $stepName = "ibl-$mode-$iteration"
        $run = Invoke-ProductSampleEvidence -StepName $stepName -IblMode $mode -Frames $IblComparisonFrames
        $modeEvidence = $run.Evidence
        $expectedConfigured = $mode -eq 'on'
        $expectedTransitions = if ($expectedConfigured) { 1 } else { 0 }
        $modeErrors = [System.Collections.Generic.List[string]]::new()
        $modeExpectedFields = [ordered]@{
            status                            = 'ok'
            sample                            = 'tina_sample_3d'
            evidenceSchema                    = 11
            frames                            = $IblComparisonFrames
            cookedEnvironmentMap              = $true
            environmentMapsUploaded           = 1
            imageBasedLightingMode            = $mode
            imageBasedLightingConfigured      = $expectedConfigured
            imageBasedLightingBindings        = $expectedTransitions
            imageBasedLightingClears          = $expectedTransitions
            environmentMapRetirementsAccepted = 1
            pixelCaptureOk                    = $true
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
        if ($modeErrors.Count -ne 0) {
            Add-Step -Name $stepName -ExitCode 1 `
                -Detail (($modeErrors -join '; ') + "; output=$($run.Stdout)")
        }
        $iblComparisonEvidence[$mode] += $modeEvidence
        Add-Step -Name $stepName -ExitCode 0 `
            -Detail "frames=$IblComparisonFrames pixelFingerprint=$($modeEvidence.pixelFingerprint)"
    }
}

$iblOnFingerprints = @($iblComparisonEvidence.on | ForEach-Object { [string]$_.pixelFingerprint })
$iblOffFingerprints = @($iblComparisonEvidence.off | ForEach-Object { [string]$_.pixelFingerprint })
$iblPixelErrors = [System.Collections.Generic.List[string]]::new()
if ($iblOnFingerprints[0] -ne $iblOnFingerprints[1]) {
    $iblPixelErrors.Add("IBL on fingerprint unstable: $($iblOnFingerprints -join ',')")
}
if ($iblOffFingerprints[0] -ne $iblOffFingerprints[1]) {
    $iblPixelErrors.Add("IBL off fingerprint unstable: $($iblOffFingerprints -join ',')")
}
if ($iblOnFingerprints[0] -eq $iblOffFingerprints[0]) {
    $iblPixelErrors.Add("IBL on/off fingerprints must differ: $($iblOnFingerprints[0])")
}
if ($iblPixelErrors.Count -ne 0) {
    Add-Step -Name 'iblPixelComparison' -ExitCode 1 -Detail ($iblPixelErrors -join '; ')
}
Add-Step -Name 'iblPixelComparison' -ExitCode 0 `
    -Detail "frames=$IblComparisonFrames on=$($iblOnFingerprints[0]) off=$($iblOffFingerprints[0]) stable=true different=true"

$report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
$report.sampleEvidence = $evidence
$report.sampleStdout = $sampleOut
$report.iblPixelComparison = [ordered]@{
    frames          = $IblComparisonFrames
    onFingerprints = $iblOnFingerprints
    offFingerprints = $iblOffFingerprints
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

Write-Output "product-3d gate ok schema=11 frames=$SampleFrames mesh-layout=p3n3t4uv2 ibl=on-off-pixel-differential theme=dark-light-dark collections=list-tree"
exit 0
