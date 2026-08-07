#Requires -Version 5.1
<#
.SYNOPSIS
  Generic Windows host launcher for Tina Linux Docker gates.

.PARAMETER Gate
  gcc13-null | gcc13-platform | gcc13-editor-zenity | gcc13-editor-kdialog | clang22-null | clang22-sanitize | sdk-consumer | sdk-platform-glfw-consumer |
  sdk-desktop-bootstrap-consumer | sdk-audio-miniaudio-consumer
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('gcc13-null', 'gcc13-platform', 'gcc13-editor-zenity', 'gcc13-editor-kdialog',
        'clang22-null', 'clang22-sanitize', 'sdk-consumer',
        'sdk-platform-glfw-consumer', 'sdk-desktop-bootstrap-consumer', 'sdk-audio-miniaudio-consumer')]
    [string]$Gate,
    [string]$SourceRoot = '',
    [switch]$SkipImageBuild,
    [string]$OutJson = '',
    [switch]$PruneAfter
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

$map = @{
    'gcc13-null' = @{
        Image = 'tina-linux-gcc13:test-001'
        Dockerfile = 'docker/linux-gcc13/Dockerfile'
        Script = 'tools/linux/run-gcc13-null-gate.sh'
        Container = 'tina-test-001-gcc13-null'
        GateId = 'TEST-001-linux-gcc13-null'
    }
    'gcc13-platform' = @{
        Image = 'tina-linux-gcc13-platform:test-001'
        Dockerfile = 'docker/linux-gcc13-platform/Dockerfile'
        Script = 'tools/linux/run-gcc13-platform-gate.sh'
        Container = 'tina-test-001-gcc13-platform'
        GateId = 'TEST-001-linux-gcc13-platform'
    }
    'gcc13-editor-zenity' = @{
        Image = 'tina-linux-gcc13-editor-zenity:2d-editor'
        Dockerfile = 'docker/linux-gcc13-platform/Dockerfile'
        Script = 'tools/linux/run-gcc13-editor-gate.sh'
        Container = 'tina-2d-editor-gcc13-zenity'
        GateId = '2D-EDITOR-linux-gcc13-zenity'
        EditorBuildDirectory = '/work/tina/out/build/docker-linux-gcc13-vnext-bgfx-editor'
        EditorDialogHelper = 'zenity'
    }
    'gcc13-editor-kdialog' = @{
        Image = 'tina-linux-gcc13-editor-kdialog:2d-editor'
        Dockerfile = 'docker/linux-gcc13-platform/Dockerfile'
        Script = 'tools/linux/run-gcc13-editor-gate.sh'
        Container = 'tina-2d-editor-gcc13-kdialog'
        GateId = '2D-EDITOR-linux-gcc13-kdialog'
        EditorBuildDirectory = '/work/tina/out/build/docker-linux-gcc13-vnext-bgfx-editor'
        EditorDialogHelper = 'kdialog'
        ReuseEditorBuild = $true
        RemoveEditorBuildAfter = $true
    }
    'clang22-null' = @{
        Image = 'tina-linux-clang22:test-001'
        Dockerfile = 'docker/linux-clang22/Dockerfile'
        Script = 'tools/linux/run-clang22-null-gate.sh'
        Container = 'tina-test-001-clang22-null'
        GateId = 'TEST-001-linux-clang22-null'
    }
    'clang22-sanitize' = @{
        Image = 'tina-linux-clang22:test-001'
        Dockerfile = 'docker/linux-clang22/Dockerfile'
        Script = 'tools/linux/run-clang22-sanitize-gate.sh'
        Container = 'tina-test-001-clang22-sanitize'
        GateId = 'TEST-001-linux-clang22-sanitize'
    }
    'sdk-consumer' = @{
        Image = 'tina-linux-gcc13:test-001'
        Dockerfile = 'docker/linux-gcc13/Dockerfile'
        Script = 'tools/linux/run-sdk-consumer-gate.sh'
        Container = 'tina-sdk-001-gcc13-consumer'
        GateId = 'SDK-001-linux-gcc13-consumer'
        BuildDirectory = '/work/tina/out/build/docker-linux-gcc13-vnext-sdk-consumer'
    }
    'sdk-platform-glfw-consumer' = @{
        Image = 'tina-linux-gcc13-platform:test-001'
        Dockerfile = 'docker/linux-gcc13-platform/Dockerfile'
        Script = 'tools/linux/run-sdk-platform-glfw-consumer-gate.sh'
        Container = 'tina-sdk-001-gcc13-platform-glfw-consumer'
        GateId = 'SDK-001-linux-gcc13-platform-glfw-consumer'
        BuildDirectory = '/work/tina/out/build/docker-linux-gcc13-vnext-platform-sdk-platform-glfw-consumer'
    }
    'sdk-desktop-bootstrap-consumer' = @{
        Image = 'tina-linux-gcc13-platform:test-001'
        Dockerfile = 'docker/linux-gcc13-platform/Dockerfile'
        Script = 'tools/linux/run-sdk-desktop-bootstrap-consumer-gate.sh'
        Container = 'tina-sdk-001-gcc13-desktop-bootstrap-consumer'
        GateId = 'SDK-001-linux-gcc13-desktop-bootstrap-consumer'
        BuildDirectory = '/work/tina/out/build/docker-linux-gcc13-vnext-bgfx-sdk-desktop-bootstrap-consumer'
    }
    'sdk-audio-miniaudio-consumer' = @{
        Image = 'tina-linux-gcc13:test-001'
        Dockerfile = 'docker/linux-gcc13/Dockerfile'
        Script = 'tools/linux/run-sdk-audio-miniaudio-consumer-gate.sh'
        Container = 'tina-sdk-001-gcc13-audio-miniaudio-consumer'
        GateId = 'SDK-001-linux-gcc13-audio-miniaudio-consumer'
        BuildDirectory = '/work/tina/out/build/docker-linux-gcc13-vnext-audio-miniaudio-sdk-consumer'
    }
}

$cfg = $map[$Gate]
$ImageName = $cfg.Image
$ContainerName = $cfg.Container
$Dockerfile = $cfg.Dockerfile
$Script = $cfg.Script
$GateId = $cfg.GateId

$report = [ordered]@{
    schema       = 1
    gate         = $GateId
    startedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot   = $SourceRoot
    image        = $ImageName
    container    = $ContainerName
    head         = (git rev-parse HEAD 2>$null)
    ok           = $false
}

function Invoke-Checked {
    param([string]$Name, [scriptblock]$Block)
    Write-Host "=== $Name ==="
    & $Block
    if ($LASTEXITCODE -ne 0) {
        throw "step failed: $Name exit=$LASTEXITCODE"
    }
}

if (-not $SkipImageBuild) {
    Invoke-Checked "docker build $Dockerfile" {
        $imageBuildArguments = @('-f', $Dockerfile, '-t', $ImageName)
        if ($cfg.ContainsKey('EditorDialogHelper')) {
            $imageBuildArguments += @('--build-arg', "TINA_EDITOR_DIALOG_HELPER=$($cfg.EditorDialogHelper)")
        }
        $imageBuildArguments += $SourceRoot
        docker build @imageBuildArguments
    }
}

$mount = $SourceRoot
if ($mount -match '^[A-Za-z]:\\') {
    $drive = $mount.Substring(0, 1).ToLowerInvariant()
    $rest = $mount.Substring(2).Replace('\', '/')
    $mount = "/$drive$rest"
}

$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
docker rm -f $ContainerName 2>$null | Out-Null
$ErrorActionPreference = $prevEap

$dockerArgs = @(
    'run', '--name', $ContainerName, '--rm',
    '--cpus=2', '--memory=8g',
    '-e', 'VCPKG_ROOT=/opt/vcpkg',
    '-e', 'VCPKG_FORCE_SYSTEM_BINARIES=1',
    '-e', 'VCPKG_DISABLE_METRICS=1'
)
if($cfg.ContainsKey('BuildDirectory')) {
    $dockerArgs += @('-e', "TINA_SDK_BUILD_DIRECTORY=$($cfg.BuildDirectory)")
}
if($cfg.ContainsKey('EditorDialogHelper')) {
    $dockerArgs += @(
        '-e', "TINA_EDITOR_DIALOG_HELPER=$($cfg.EditorDialogHelper)",
        '-e', "TINA_EDITOR_BUILD_DIRECTORY=$($cfg.EditorBuildDirectory)"
    )
}
if($cfg.ContainsKey('ReuseEditorBuild') -and $cfg.ReuseEditorBuild) {
    $dockerArgs += @('-e', 'TINA_EDITOR_REUSE_BUILD=1')
}
if($cfg.ContainsKey('RemoveEditorBuildAfter') -and $cfg.RemoveEditorBuildAfter) {
    $dockerArgs += @('-e', 'TINA_EDITOR_REMOVE_BUILD_AFTER=1')
}
$dockerArgs += @(
    '-v', "${mount}:/work/tina",
    '-w', '/work/tina',
    $ImageName,
    'bash', '-lc', "chmod +x $Script && $Script"
)

Write-Host "=== docker run $GateId ==="
& docker @dockerArgs
$gateExit = $LASTEXITCODE

$containerId = docker container ls -a --filter "name=^/${ContainerName}$" --format '{{.ID}}' 2>$null
$containerRemoved = [string]::IsNullOrWhiteSpace(($containerId -join ''))
$resourceState = [ordered]@{
    containerRemoved = $containerRemoved
    processState = if ($gateExit -eq 0) { 'gate-verified-stopped' } else { 'container-exited' }
}
if($cfg.ContainsKey('EditorBuildDirectory')) {
    $containerSourcePrefix = '/work/tina/'
    if (-not $cfg.EditorBuildDirectory.StartsWith($containerSourcePrefix, [System.StringComparison]::Ordinal)) {
        throw "editor build directory must stay under $containerSourcePrefix"
    }
    $relativeBuildTree = $cfg.EditorBuildDirectory.Substring($containerSourcePrefix.Length).Replace(
        '/', [System.IO.Path]::DirectorySeparatorChar)
    $hostEditorBuildTree = Join-Path $SourceRoot $relativeBuildTree
    $editorBuildTreeExists = Test-Path -LiteralPath $hostEditorBuildTree
    $resourceState.editorBuildTree = $hostEditorBuildTree
    $resourceState.editorBuildTreeState = if ($editorBuildTreeExists) { 'retained' } else { 'removed' }

    if ($gateExit -eq 0 -and $cfg.ContainsKey('RemoveEditorBuildAfter') -and
        $cfg.RemoveEditorBuildAfter -and $editorBuildTreeExists) {
        Write-Error "editor gate succeeded but its dedicated build tree was not removed: $hostEditorBuildTree"
        $gateExit = 1
    }
}
if ($gateExit -eq 0 -and -not $containerRemoved) {
    Write-Error "gate container was not removed: $ContainerName"
    $gateExit = 1
}

$report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
$report.exitCode = $gateExit
$report.ok = ($gateExit -eq 0)
$report.resources = $resourceState

if ($OutJson) {
    $dir = Split-Path -Parent $OutJson
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    ($report | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $OutJson -Encoding utf8
    Write-Host "wrote $OutJson"
}

if ($PruneAfter) {
    Write-Host '=== docker container prune (stopped only) ==='
    docker container prune -f 2>$null | Out-Null
    Write-Host '=== docker image prune dangling ==='
    docker image prune -f 2>$null | Out-Null
}

if ($gateExit -ne 0) {
    Write-Error "$GateId failed exit=$gateExit"
    exit $gateExit
}
Write-Host "$GateId OK"
exit 0
