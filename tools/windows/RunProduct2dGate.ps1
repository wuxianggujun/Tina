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
    'tina_scene_tests',
    'tina_ui_tests',
    'tina_runtime_ui_tests',
    'tina_ui_render_integration_tests',
    'tina_ui_freetype_tests',
    'tina_physics2d_tests',
    'tina_audio_tests',
    'tina_audio_miniaudio_tests',
    'tina_asset_tests'
)
$testExes = @(
    'tina_scene_tests.exe',
    'tina_ui_tests.exe',
    'tina_runtime_ui_tests.exe',
    'tina_ui_render_integration_tests.exe',
    'tina_ui_freetype_tests.exe',
    'tina_physics2d_tests.exe',
    'tina_audio_tests.exe',
    'tina_audio_miniaudio_tests.exe',
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
    $targetArgs = @('--preset', $BuildPreset, '--target') + $targets + @('--', '/m:2', '/v:m')
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
    'evidenceSchema\":14',
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
    'accessibilityHasTree\":true',
    'accessibilityHasTreeItem\":true',
    'accessibilityTreeSelectionVerified\":true',
    'spriteBindingTextures\":2',
    'spriteTextureLeasesAcquired\":2',
    'spriteTextureRetirementsAccepted\":2',
    'spriteBindingRegistryReleased\":true',
    'spriteTextureHandlesInvalidated\":2',
    'spriteTextureRetirementRecords\":2',
    'spriteTextureRetirementReleased\":2',
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
        'particleExpired\":4',
        'particleActive\":6',
        'particleExtracted\":6',
        'trailActive\":3',
        'trailExtracted\":3'
    )
}
foreach ($pattern in $requiredProductEvidence) {
    if ($sampleOut -notmatch $pattern) {
        Add-Step -Name 'productEvidence' -ExitCode 1 -Detail "missing $pattern; output=$($sampleOut.Trim())"
    }
}
Add-Step -Name 'tina_sample_2d' -ExitCode 0 -Detail "productGate=$expectedGate frames=$SampleFrames"

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
