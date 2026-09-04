<#
.SYNOPSIS
  Build and run the full Windows product-2d gate (TEST-002 topology).

.DESCRIPTION
  Configure/build windows-msvc-vnext-bgfx-product-2d, then run module GoogleTests and
  tina_sample_2d 300-frame smoke. Expects productGate=bgfx-physics-freetype-audio.

  Does not use CTest. Does not clean-first wipe. Exit non-zero on first failure.

.PARAMETER BinDir
  Directory containing the selected build preset's executables. Relative paths are
  resolved from SourceRoot. Defaults to the standard product-2d Debug output directory;
  pass this explicitly when a custom ConfigurePreset or BuildPreset uses another output.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$BuildPreset = 'windows-vnext-bgfx-product-2d-debug',
    [string]$ConfigurePreset = 'windows-msvc-vnext-bgfx-product-2d',
    [int]$SampleFrames = 300,
    [int]$ShadowVisualFrames = 300,
    [int]$NormalMapVisualFrames = 300,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [string]$OutJson = '',
    [string]$BinDir = ''
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

if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = Join-Path $SourceRoot "out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug"
} elseif (-not [System.IO.Path]::IsPathRooted($BinDir)) {
    $BinDir = Join-Path $SourceRoot $BinDir
}
$BinDir = [System.IO.Path]::GetFullPath($BinDir)

$expectedGate = 'bgfx-physics-freetype-audio'
$targets = @(
    'tina_sample_2d',
    'tina_sample_2d_authored_scene',
    'tina_sample_2d_custom_shader',
    'tina_sample_2d_shader_materials',
    'tina_sample_2d_shader_lighting',
    'tina_navigation2d_tests',
    'tina_scene_tests',
    'tina_render_scene_tests',
    'tina_render_bgfx_tests',
    'tina_ui_tests',
    'tina_runtime_ui_tests',
    'tina_ui_render_integration_tests',
    'tina_ui_freetype_tests',
    'tina_ui_uia_tests',
    'tina_physics2d_tests',
    'tina_audio_tests',
    'tina_audio_miniaudio_tests',
    'tina_asset_format_tests',
    'tina_asset_tests'
)
$testExes = @(
    'tina_navigation2d_tests.exe',
    'tina_scene_tests.exe',
    'tina_render_scene_tests.exe',
    'tina_render_bgfx_tests.exe',
    'tina_ui_tests.exe',
    'tina_runtime_ui_tests.exe',
    'tina_ui_render_integration_tests.exe',
    'tina_ui_freetype_tests.exe',
    'tina_ui_uia_tests.exe',
    'tina_physics2d_tests.exe',
    'tina_audio_tests.exe',
    'tina_audio_miniaudio_tests.exe',
    'tina_asset_format_tests.exe',
    'tina_asset_tests.exe'
)

$report = [ordered]@{
    schema           = 1
    startedAtUtc     = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot       = $SourceRoot
    configurePreset  = $ConfigurePreset
    buildPreset      = $BuildPreset
    binDir           = $BinDir
    expectedProductGate = $expectedGate
    head             = (git rev-parse HEAD 2>$null)
    steps            = @()
    ok               = $false
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
    Add-Step -Name 'binDir' -ExitCode 1 -Detail "missing directory: $BinDir; pass -BinDir for custom preset output"
}

foreach ($exe in $testExes) {
    $path = Join-Path $BinDir $exe
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Step -Name $exe -ExitCode 1 -Detail "missing executable: $path"
    }
    & $path --gtest_color=yes
    Add-Step -Name $exe -ExitCode $LASTEXITCODE
}

$samplePath = Join-Path $BinDir 'tina_sample_2d.exe'
if (-not (Test-Path -LiteralPath $samplePath)) {
    Add-Step -Name 'tina_sample_2d' -ExitCode 1 -Detail "missing executable: $samplePath"
}
$sampleOut = & $samplePath "--frames=$SampleFrames" '--frame-delay-ms=0' '--ui-theme-demo' '--ui-tree-demo' 2>&1 | Out-String
$sampleExit = $LASTEXITCODE
if ($sampleExit -ne 0) {
    Add-Step -Name 'tina_sample_2d' -ExitCode $sampleExit -Detail $sampleOut.Trim()
}
$gatePattern = 'productGate":"' + [regex]::Escape($expectedGate) + '"'
if ($sampleOut -notmatch $gatePattern) {
    Add-Step -Name 'productGate' -ExitCode 1 -Detail "expected $expectedGate; output=$($sampleOut.Trim())"
}
$requiredProductEvidence = @(
    'evidenceSchema\":29',
    'catalogRecipeAssets\":17',
    'animEventFootsteps\":[1-9][0-9]*',
    'animEventHits\":[1-9][0-9]*',
    'animEventOverflow\":0',
    'animEventUnknownTags\":0',
    'navigationReady\":true',
    'navigationFromCookedAsset\":true',
    'navigationCookedBitExact\":true',
    'navigationSolidTileCells\":11',
    'navigationBlockerRectangles\":0',
    'navigationBlockedCells\":11',
    'navigationWeightedCells\":1',
    'navigationMaximumTraversalCost\":5',
    'navigationBasePathCells\":5',
    'navigationBasePathCost\":40',
    'navigationDynamicPathCells\":5',
    'navigationDynamicPathCost\":40',
    'navigationStrictDiagonalPathCells\":5',
    'navigationStrictDiagonalPathCost\":40',
    'navigationCornerCutPathCells\":5',
    'navigationCornerCutPathCost\":40',
    'navigationWeightedPathCells\":7',
    'navigationWeightedPathCost\":60',
    'navigationWeightedPathAvoidedCostCell\":true',
    'navigationIncrementalExpandedNodes\":1',
    'navigationGridRevision\":10',
    'navigationDynamicBlockerMutations\":2',
    'navigationPhysicsSynchronizations\":[2-9][0-9]*',
    'navigationPhysicsBlockerAdds\":[1-9][0-9]*',
    'navigationPhysicsBlockerUpdates\":[1-9][0-9]*',
    'navigationPhysicsPublishedBlockers\":1',
    'navigationPhysicsRegisteredBodies\":1',
    'navigationPhysicsReady\":true',
    'navigationCancelled\":true',
    'physicsConvexPolygonReady\":true',
    'physicsRevoluteJointReady\":true',
    'physicsPrismaticJointReady\":true',
    'physicsChainReady\":true',
    'physicsStaticBodies\":1',
    'physicsStaticSolidCells\":11',
    'physicsStaticBoxShapes\":2',
    'sprite2DLightingConfigured\":true',
    'authoredPointLight2DCount\":3',
    'pointLight2DCount\":2',
    'culledPointLight2DCount\":1',
    'shadowOccluder2DCount\":2',
    'softShadowPointLight2DCount\":2',
    'normalMappedSpriteCount\":1',
    'sceneLightingFrames\":[1-9][0-9]*',
    'renderFrameAccountingValid\":true',
    'uiThemeDemoRequested\":true',
    'uiThemeSwitches\":2',
    'uiThemeButtonActivations\":0',
    'uiThemeFinalLight\":false',
    'uiTreeDemoRequested\":true',
    'uiTreeViewsCreated\":1',
    'uiTreeLogicalItems\":13',
    'uiTreeMaterializedCapacity\":12',
    'uiTreeSelectionChanges\":2',
    'uiTreeFinalSelectedKey\":402',
    'uiTreeFinalSelectedIndex\":12',
    'uiTreeFinalSelectionVerified\":true',
    'uiTreeScrolled\":true',
    'uiTreeThemeVerified\":true',
    'uiFlowLayersRegistered\":1',
    'uiFlowScreensRegistered\":2',
    'uiFlowScreenPushes\":2',
    'uiFlowScreenPops\":1',
    'uiFlowActionsRegistered\":4',
    'uiFlowActionsCleared\":4',
    'uiFlowBackActionInvocations\":0',
    'uiFlowConfirmActionInvocations\":0',
    'uiFlowMenuActionInvocations\":0',
    'pauseOpenActionInvocations\":0',
    'pauseInputDeviceHintUpdates\":1',
    'pauseInputDeviceRevision\":0',
    'pauseInputHintKeyboardMouse\":true',
    'pauseInputHintGamepad\":false',
    'pauseAutoResumeRequests\":1',
    'pauseResumeRequestedByAction\":false',
    'pauseUIScreenActivated\":true',
    'baseUIScreenRestored\":true',
    'accessibilityHasTree\":true',
    'accessibilityHasTreeItem\":true',
    'accessibilityTreeSelectionVerified\":true',
    'texturesUploaded\":3',
    'spriteBindingTextures\":3',
    'spriteTextureLeasesAcquired\":3',
    'spriteTextureRetirementsAccepted\":3',
    'spriteBindingRegistryReleased\":true',
    'spriteTextureHandlesInvalidated\":3',
    'spriteTextureRetirementRecords\":3',
    'spriteTextureRetirementReleased\":3',
    'spriteTextureRetirementLive\":0',
    'spriteBindingResolverHits\":[1-9][0-9]*',
    'tileMapSpriteBindingResolverHits\":[1-9][0-9]*',
    'particleSpriteBindingResolverHits\":[1-9][0-9]*',
    'trailSpriteBindingResolverHits\":[1-9][0-9]*',
    'particleCapacity\":12',
    'particleRandomSeed\":1414090305',
    'particleEmitted\":10',
    'trailCapacity\":8',
    'trailSegmentsCreated\":3',
    'trailBreaks\":1',
    'fxInitialFingerprint\":\"[0-9a-f]{32}\"',
    'audioVoiceParamsConfigured\":true',
    'audioFadeStarted\":true',
    'audioFadeCancelled\":true',
    'audioFadeStopped\":true',
    'audioOneShotRetired\":true',
    'audioStreamQueued\":true',
    'audioStreamSubmitted\":true',
    'audioStreamEofSignaled\":true',
    'audioStreamStartedObserved\":true',
    'audioStreamMixed\":true',
    'audioStreamDrained\":true',
    'audioStreamStopped\":true',
    'audioStreamRetired\":true',
    'audioStreamSubmittedFrames\":[1-9][0-9]*',
    'audioStreamConsumedFrames\":[1-9][0-9]*',
    'audioStreamUnderrunFrames\":0'
)
if ($SampleFrames -eq 300) {
    $requiredProductEvidence += @(
        'particleExpired\":0',
        'particleActive\":10',
        'particleExtracted\":10',
        'trailActive\":3',
        'trailExtracted\":3'
    )
}
foreach ($pattern in $requiredProductEvidence) {
    if ($sampleOut -notmatch $pattern) {
        Add-Step -Name 'productEvidence' -ExitCode 1 -Detail "missing $pattern; output=$($sampleOut.Trim())"
    }
}
$renderExtractionsMatch =
    [regex]::Match($sampleOut, 'renderExtractions\":(?<value>[1-9][0-9]*)')
$sceneLightingFramesMatch =
    [regex]::Match($sampleOut, 'sceneLightingFrames\":(?<value>[1-9][0-9]*)')
$submittedRenderFramesMatch =
    [regex]::Match($sampleOut, 'submittedRenderFrames\":(?<value>[1-9][0-9]*)')
$skippedSuspendedSurfaceFramesMatch =
    [regex]::Match($sampleOut, 'skippedSuspendedSurfaceFrames\":(?<value>[0-9]+)')
if (-not $renderExtractionsMatch.Success -or -not $sceneLightingFramesMatch.Success -or
    -not $submittedRenderFramesMatch.Success -or -not $skippedSuspendedSurfaceFramesMatch.Success -or
    [int64]$sceneLightingFramesMatch.Groups['value'].Value -ne
        [int64]$submittedRenderFramesMatch.Groups['value'].Value -or
    [int64]$renderExtractionsMatch.Groups['value'].Value -ne
        [int64]$submittedRenderFramesMatch.Groups['value'].Value +
        [int64]$skippedSuspendedSurfaceFramesMatch.Groups['value'].Value) {
    Add-Step -Name 'sceneLightingFrames' -ExitCode 1 `
        -Detail 'lighting submissions plus suspended-surface skips must exactly account for renderExtractions'
}
Add-Step -Name 'tina_sample_2d' -ExitCode 0 -Detail "productGate=$expectedGate frames=$SampleFrames"

# The authored-scene sample is the Gameplay2D consumer: it loads a .tworld and lets
# Scene2DRuntime own the per-frame order for all four resource kinds plus physics.
# Headless, so it adds no GPU dependency to this gate.
#
# Parsed as JSON with numeric comparisons rather than substring regexes: an unanchored
# pattern like 'physicsSteps\":300' also matches 3000, so a counter that grew a digit
# would pass silently.
$authoredScenePath = Join-Path $BinDir 'tina_sample_2d_authored_scene.exe'
if (-not (Test-Path -LiteralPath $authoredScenePath)) {
    Add-Step -Name 'tina_sample_2d_authored_scene' -ExitCode 1 `
        -Detail "missing executable: $authoredScenePath"
}
$authoredSceneOut = & $authoredScenePath "--frames=$SampleFrames" 2>&1 | Out-String
$authoredSceneExit = $LASTEXITCODE
if ($authoredSceneExit -ne 0) {
    Add-Step -Name 'tina_sample_2d_authored_scene' -ExitCode $authoredSceneExit `
        -Detail $authoredSceneOut.Trim()
}
$authoredSceneLine = $authoredSceneOut -split "`n" |
    Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_2d_authored_scene"' } |
    Select-Object -Last 1
if (-not $authoredSceneLine) {
    Add-Step -Name 'tina_sample_2d_authored_scene' -ExitCode 1 `
        -Detail "no ok evidence line; output=$($authoredSceneOut.Trim())"
}
$authoredScene = $authoredSceneLine | ConvertFrom-Json
$authoredSceneExpected = [ordered]@{
    evidenceSchema               = 1
    frames                       = [int64]$SampleFrames
    submittedFrames              = [int64]$SampleFrames
    presentedFrames              = [int64]$SampleFrames
    sceneLoadedFromFile          = $true
    authoredEntities             = 10
    resolvedNodesByAuthoredName  = $true
    tileMapNodes                 = 1
    tileLayers                   = 2
    fxNodes                      = 1
    navigationNodes              = 1
    audioNodes                   = 1
    unresolvedNodes              = 0
    residentTileChunks           = 2
    demandUpdates                = [int64]$SampleFrames
    commits                      = [int64]$SampleFrames
    extracts                     = [int64]$SampleFrames
    physicsSteps                 = [int64]$SampleFrames
    extractBeforeCommitRejected  = $true
    tileMapReachable             = $true
    collisionCellsFound          = 4
    navigationGridReachable      = $true
    visibleTileSprites           = 16
    lastFrameHadCamera           = $true
    audioVoiceStarted            = $true
    runtimeShutdownOk            = $true
    stateExits                   = 1
    applicationShutdowns         = 1
    renderShutdowns              = 1
}
foreach ($field in $authoredSceneExpected.Keys) {
    $actual = $authoredScene.$field
    $expected = $authoredSceneExpected[$field]
    if ($null -eq $actual) {
        Add-Step -Name "authoredScene:$field" -ExitCode 1 -Detail 'field missing from evidence'
    }
    if ($actual -ne $expected) {
        Add-Step -Name "authoredScene:$field" -ExitCode 1 `
            -Detail "expected $expected; actual $actual"
    }
}
# The authored transform must place the emitter, not the Fx payload's own origin.
if ([double]$authoredScene.fxOriginX -ne 2.0 -or [double]$authoredScene.fxOriginY -ne 1.0) {
    Add-Step -Name 'authoredScene:fxOrigin' -ExitCode 1 `
        -Detail "expected (2,1); actual ($($authoredScene.fxOriginX),$($authoredScene.fxOriginY))"
}
# Physics is authoritative and written back: the crate fell from its authored height
# and settled on the authored floor rather than falling forever.
$crateStartY = [double]$authoredScene.crateStartY
$crateEndY = [double]$authoredScene.crateEndY
if ($crateStartY -ne 6.0 -or $crateEndY -ge $crateStartY -or
    [math]::Abs($crateEndY - 1.0) -gt 0.1) {
    Add-Step -Name 'authoredScene:cratePhysics' -ExitCode 1 `
        -Detail "expected fall from 6.0 to about 1.0; actual start=$crateStartY end=$crateEndY"
}
Add-Step -Name 'tina_sample_2d_authored_scene' -ExitCode 0 `
    -Detail "evidenceSchema=1 frames=$SampleFrames crateEndY=$crateEndY"

# First consumer of the Sprite2D custom fragment path. Pixel evidence is two-sided: the
# custom-shader sprites must move between the two pinned u_pulse phases, and the engine
# control sprite must not -- plus its four quadrant colours must match the 2x2 checker,
# which is what proves sampling/UV rather than "the fragment ran and tinted white".
$customShaderPath = Join-Path $BinDir 'tina_sample_2d_custom_shader.exe'
if (-not (Test-Path -LiteralPath $customShaderPath)) {
    Add-Step -Name 'tina_sample_2d_custom_shader' -ExitCode 1 `
        -Detail "missing executable: $customShaderPath"
}
$customShaderOut = & $customShaderPath "--frames=$SampleFrames" 2>&1 | Out-String
$customShaderExit = $LASTEXITCODE
if ($customShaderExit -ne 0) {
    Add-Step -Name 'tina_sample_2d_custom_shader' -ExitCode $customShaderExit `
        -Detail $customShaderOut.Trim()
}
$customShaderLine = $customShaderOut -split "`n" |
    Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_2d_custom_shader"' } |
    Select-Object -Last 1
if (-not $customShaderLine) {
    Add-Step -Name 'tina_sample_2d_custom_shader' -ExitCode 1 `
        -Detail "no ok evidence line; output=$($customShaderOut.Trim())"
}
$customShader = $customShaderLine | ConvertFrom-Json
$customShaderExpected = [ordered]@{
    frames                  = [int64]$SampleFrames
    spritesPerFrame         = 4
    customShaderSprites     = 3
    shaderRetired           = $true
    textureRetired          = $true
    evidenceCollected       = $true
    engineSpriteDelta       = 0
    checkerSamplingMatched  = $true
}
foreach ($field in $customShaderExpected.Keys) {
    $actual = $customShader.$field
    $expected = $customShaderExpected[$field]
    if ($null -eq $actual) {
        Add-Step -Name "customShader:$field" -ExitCode 1 -Detail 'field missing from evidence'
    }
    if ($actual -ne $expected) {
        Add-Step -Name "customShader:$field" -ExitCode 1 `
            -Detail "expected $expected; actual $actual"
    }
}
if ([int64]$customShader.customSpriteDelta -lt 8) {
    Add-Step -Name 'customShader:customSpriteDelta' -ExitCode 1 `
        -Detail "expected customSpriteDelta >= 8; actual $($customShader.customSpriteDelta)"
}
if ([int64]$customShader.shaderBlobCount -lt 1) {
    Add-Step -Name 'customShader:shaderBlobCount' -ExitCode 1 `
        -Detail "expected shaderBlobCount >= 1; actual $($customShader.shaderBlobCount)"
}
Add-Step -Name 'tina_sample_2d_custom_shader' -ExitCode 0 `
    -Detail "frames=$SampleFrames customSpriteDelta=$($customShader.customSpriteDelta) checkerSamplingMatched=true"

# One program, three uniform bindings. The two zero-valued fields are the ones that make this
# a material proof rather than a brightness proof: two sprites sharing a binding must be
# byte-identical, and the same-uniform control must not spread. Thresholds mirror the sample's
# own constants (separation >= 12, sameMaterialDelta <= 6, flatSpread <= 24), which were each
# argued against a negative control; the gate asserts the stricter observed invariants only
# where the sample itself proves them exactly.
$materialsPath = Join-Path $BinDir 'tina_sample_2d_shader_materials.exe'
if (-not (Test-Path -LiteralPath $materialsPath)) {
    Add-Step -Name 'tina_sample_2d_shader_materials' -ExitCode 1 `
        -Detail "missing executable: $materialsPath"
}
$materialsOut = & $materialsPath "--frames=$SampleFrames" 2>&1 | Out-String
$materialsExit = $LASTEXITCODE
if ($materialsExit -ne 0) {
    Add-Step -Name 'tina_sample_2d_shader_materials' -ExitCode $materialsExit `
        -Detail $materialsOut.Trim()
}
$materialsLine = $materialsOut -split "`n" |
    Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_2d_shader_materials"' } |
    Select-Object -Last 1
if (-not $materialsLine) {
    Add-Step -Name 'tina_sample_2d_shader_materials' -ExitCode 1 `
        -Detail "no ok evidence line; output=$($materialsOut.Trim())"
}
$materials = $materialsLine | ConvertFrom-Json
$materialsExpected = [ordered]@{
    frames                  = [int64]$SampleFrames
    materials               = 3
    spritesPerFrame         = 6
    shaderRetired           = $true
    textureRetired          = $true
    evidenceCollected       = $true
    maximumSameMaterialDelta = 0
    flatMaterialSpread      = 0
    evidenceError           = ''
}
foreach ($field in $materialsExpected.Keys) {
    $actual = $materials.$field
    $expected = $materialsExpected[$field]
    if ($null -eq $actual) {
        Add-Step -Name "shaderMaterials:$field" -ExitCode 1 -Detail 'field missing from evidence'
    }
    if ($actual -ne $expected) {
        Add-Step -Name "shaderMaterials:$field" -ExitCode 1 `
            -Detail "expected $expected; actual $actual"
    }
}
if ([int64]$materials.minimumMaterialSeparation -lt 12) {
    Add-Step -Name 'shaderMaterials:minimumMaterialSeparation' -ExitCode 1 `
        -Detail "expected >= 12; actual $($materials.minimumMaterialSeparation)"
}
Add-Step -Name 'tina_sample_2d_shader_materials' -ExitCode 0 `
    -Detail ("frames=$SampleFrames minimumMaterialSeparation=$($materials.minimumMaterialSeparation) " +
             "sameMaterialDelta=0 flatSpread=0")

# A custom fragment that consumes the engine lighting/normal contract instead of replacing it.
# normalVsFlatSeparation is deliberately NOT asserted: the sample documents that killing the
# normal map still measures 3 there, so any threshold at that scale would pass with the feature
# dead. normalLeftVsRight is the criterion that survives the negative control (0 vs 10).
$lightingPath = Join-Path $BinDir 'tina_sample_2d_shader_lighting.exe'
if (-not (Test-Path -LiteralPath $lightingPath)) {
    Add-Step -Name 'tina_sample_2d_shader_lighting' -ExitCode 1 `
        -Detail "missing executable: $lightingPath"
}
$lightingOut = & $lightingPath "--frames=$SampleFrames" 2>&1 | Out-String
$lightingExit = $LASTEXITCODE
if ($lightingExit -ne 0) {
    Add-Step -Name 'tina_sample_2d_shader_lighting' -ExitCode $lightingExit `
        -Detail $lightingOut.Trim()
}
$lightingLine = $lightingOut -split "`n" |
    Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_2d_shader_lighting"' } |
    Select-Object -Last 1
if (-not $lightingLine) {
    Add-Step -Name 'tina_sample_2d_shader_lighting' -ExitCode 1 `
        -Detail "no ok evidence line; output=$($lightingOut.Trim())"
}
$lighting = $lightingLine | ConvertFrom-Json
$lightingExpected = [ordered]@{
    frames            = [int64]$SampleFrames
    spritesPerFrame   = 6
    shaderRetired     = $true
    albedoRetired     = $true
    normalRetired     = $true
    evidenceCollected = $true
    evidenceError     = ''
}
foreach ($field in $lightingExpected.Keys) {
    $actual = $lighting.$field
    $expected = $lightingExpected[$field]
    if ($null -eq $actual) {
        Add-Step -Name "shaderLighting:$field" -ExitCode 1 -Detail 'field missing from evidence'
    }
    if ($actual -ne $expected) {
        Add-Step -Name "shaderLighting:$field" -ExitCode 1 `
            -Detail "expected $expected; actual $actual"
    }
}
$lightingMinimums = [ordered]@{
    normalLeftVsRight   = 6
    shadowedVsLit       = 40
    engineControlSpread = 40
}
foreach ($field in $lightingMinimums.Keys) {
    $actual = [int64]$lighting.$field
    $minimum = $lightingMinimums[$field]
    if ($actual -lt $minimum) {
        Add-Step -Name "shaderLighting:$field" -ExitCode 1 `
            -Detail "expected >= $minimum; actual $actual"
    }
}
Add-Step -Name 'tina_sample_2d_shader_lighting' -ExitCode 0 `
    -Detail ("frames=$SampleFrames normalLeftVsRight=$($lighting.normalLeftVsRight) " +
             "shadowedVsLit=$($lighting.shadowedVsLit) engineControlSpread=$($lighting.engineControlSpread)")

$shadowVisualScript = Join-Path $SourceRoot 'tools\windows\RunProduct2dShadowVisualGate.ps1'
if (-not (Test-Path -LiteralPath $shadowVisualScript -PathType Leaf)) {
    Add-Step -Name 'shadowVisual' -ExitCode 1 -Detail "missing script: $shadowVisualScript"
}
$shadowVisualOut = & powershell -NoProfile -ExecutionPolicy Bypass -File $shadowVisualScript `
    -SourceRoot $SourceRoot `
    -BinDir $BinDir `
    -SampleFrames $ShadowVisualFrames 2>&1 | Out-String
$shadowVisualExit = $LASTEXITCODE
if ($shadowVisualExit -ne 0) {
    Add-Step -Name 'shadowVisual' -ExitCode $shadowVisualExit -Detail $shadowVisualOut.Trim()
}
Add-Step -Name 'shadowVisual' -ExitCode 0 -Detail $shadowVisualOut.Trim()

$normalMapVisualScript = Join-Path $SourceRoot 'tools\windows\RunProduct2dNormalMapVisualGate.ps1'
if (-not (Test-Path -LiteralPath $normalMapVisualScript -PathType Leaf)) {
    Add-Step -Name 'normalMapVisual' -ExitCode 1 -Detail "missing script: $normalMapVisualScript"
}
$normalMapVisualOut = & powershell -NoProfile -ExecutionPolicy Bypass -File $normalMapVisualScript `
    -SourceRoot $SourceRoot `
    -BinDir $BinDir `
    -SampleFrames $NormalMapVisualFrames 2>&1 | Out-String
$normalMapVisualExit = $LASTEXITCODE
if ($normalMapVisualExit -ne 0) {
    Add-Step -Name 'normalMapVisual' -ExitCode $normalMapVisualExit -Detail $normalMapVisualOut.Trim()
}
Add-Step -Name 'normalMapVisual' -ExitCode 0 -Detail $normalMapVisualOut.Trim()

$report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
$report.sampleStdout = $sampleOut.Trim()
$report.ok = $true

if ($OutJson) {
    $dir = Split-Path -Parent $OutJson
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    ($report | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $OutJson -Encoding utf8
    Write-Output "wrote $OutJson"
}

Write-Output "product-2d gate ok productGate=$expectedGate"
exit 0
