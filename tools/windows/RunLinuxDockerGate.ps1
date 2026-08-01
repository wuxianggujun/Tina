#Requires -Version 5.1
<#
.SYNOPSIS
  Generic Windows host launcher for Tina Linux Docker gates.

.PARAMETER Gate
  gcc13-null | gcc13-platform | clang22-null | clang22-sanitize | sdk-consumer
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('gcc13-null', 'gcc13-platform', 'clang22-null', 'clang22-sanitize', 'sdk-consumer')]
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
        docker build -f $Dockerfile -t $ImageName $SourceRoot
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
    '-e', 'VCPKG_ROOT=/opt/vcpkg',
    '-e', 'VCPKG_FORCE_SYSTEM_BINARIES=1',
    '-e', 'VCPKG_DISABLE_METRICS=1',
    '-v', "${mount}:/work/tina",
    '-w', '/work/tina',
    $ImageName,
    'bash', '-lc', "chmod +x $Script && $Script"
)

Write-Host "=== docker run $GateId ==="
& docker @dockerArgs
$gateExit = $LASTEXITCODE

$report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
$report.exitCode = $gateExit
$report.ok = ($gateExit -eq 0)

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
