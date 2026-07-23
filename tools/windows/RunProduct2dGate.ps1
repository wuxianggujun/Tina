#Requires -Version 5.1
<#
.SYNOPSIS
  Build and run the full Windows product-2d gate (TEST-002 topology).

.DESCRIPTION
  Configure/build windows-msvc-vnext-bgfx-product-2d, then run module GoogleTests and
  tina_sample_2d 300-frame smoke. Expects productGate=bgfx-physics-freetype-audio.

  Does not use CTest. Does not clean-first wipe. Exit non-zero on first failure.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$BuildPreset = 'windows-vnext-bgfx-product-2d-debug',
    [string]$ConfigurePreset = 'windows-msvc-vnext-bgfx-product-2d',
    [int]$SampleFrames = 300,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [string]$OutJson = ''
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

$binDir = Join-Path $SourceRoot "out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug"
$expectedGate = 'bgfx-physics-freetype-audio'
$targets = @(
    'tina_sample_2d',
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

foreach ($exe in $testExes) {
    $path = Join-Path $binDir $exe
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Step -Name $exe -ExitCode 1 -Detail 'missing executable'
    }
    & $path --gtest_color=yes
    Add-Step -Name $exe -ExitCode $LASTEXITCODE
}

$samplePath = Join-Path $binDir 'tina_sample_2d.exe'
if (-not (Test-Path -LiteralPath $samplePath)) {
    Add-Step -Name 'tina_sample_2d' -ExitCode 1 -Detail 'missing executable'
}
$sampleOut = & $samplePath "--frames=$SampleFrames" '--frame-delay-ms=0' 2>&1 | Out-String
$sampleExit = $LASTEXITCODE
if ($sampleExit -ne 0) {
    Add-Step -Name 'tina_sample_2d' -ExitCode $sampleExit -Detail $sampleOut.Trim()
}
$gatePattern = 'productGate":"' + [regex]::Escape($expectedGate) + '"'
if ($sampleOut -notmatch $gatePattern) {
    Add-Step -Name 'productGate' -ExitCode 1 -Detail "expected $expectedGate; output=$($sampleOut.Trim())"
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
