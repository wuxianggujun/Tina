#Requires -Version 5.1
<#
.SYNOPSIS
  Host launcher for TEST-001 Linux GCC13 Null gate via Docker Desktop.

.DESCRIPTION
  Builds docker/linux-gcc13 image if needed, mounts the repo, runs
  tools/linux/run-gcc13-null-gate.sh. Writes a short evidence JSON under
  artifacts/gates/ when -OutJson is set.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$ImageName = 'tina-linux-gcc13:test-001',
    [string]$ContainerName = 'tina-test-001-gcc13-null',
    [switch]$SkipImageBuild,
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

$report = [ordered]@{
    schema        = 1
    gate          = 'TEST-001-linux-gcc13-null'
    startedAtUtc  = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot    = $SourceRoot
    image         = $ImageName
    container     = $ContainerName
    head          = (git rev-parse HEAD 2>$null)
    ok            = $false
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
    Invoke-Checked 'docker build linux-gcc13' {
        docker build -f docker/linux-gcc13/Dockerfile -t $ImageName $SourceRoot
    }
}

# Convert Windows path for Docker Desktop bind mount.
$mount = $SourceRoot
if ($mount -match '^[A-Za-z]:\\') {
    $drive = $mount.Substring(0, 1).ToLowerInvariant()
    $rest = $mount.Substring(2).Replace('\', '/')
    $mount = "/$drive$rest"
}

# Drop previous container with same name if any (ignore missing).
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
    'bash', '-lc', 'chmod +x tools/linux/run-gcc13-null-gate.sh && tools/linux/run-gcc13-null-gate.sh'
)

Write-Host "=== docker run TEST-001 GCC13 Null ==="
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

if ($gateExit -ne 0) {
    Write-Error "TEST-001 Linux GCC13 Null gate failed exit=$gateExit"
    exit $gateExit
}
Write-Host "TEST-001 Linux GCC13 Null gate OK"
exit 0
