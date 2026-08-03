#Requires -Version 5.1
<#
.SYNOPSIS
  Runs the Tina SDK cross-distribution artifact transfer gate from Windows.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [switch]$SkipProducerImageBuild,
    [switch]$SkipConsumerImageBuild,
    [string]$OutJson = '',
    [switch]$PruneAfter
)

$ErrorActionPreference = 'Stop'

$ProducerImage = 'tina-linux-gcc13:test-001'
$ConsumerImage = 'tina-sdk-consumer-debian13:sdk-001'
$ArtifactVolume = 'tina-sdk-001-cross-distro-artifact'
$ProducerContainer = 'tina-sdk-001-cross-distro-producer'
$ConsumerContainer = 'tina-sdk-001-cross-distro-consumer'
$ProducerDockerfile = 'docker/linux-gcc13/Dockerfile'
$ConsumerDockerfile = 'docker/linux-sdk-consumer-debian13/Dockerfile'
$ProducerScript = 'tools/linux/run-sdk-cross-distro-producer-gate.sh'
$ConsumerScript = '/usr/local/bin/tina-sdk-cross-distro-consumer-gate'
$GateId = 'SDK-001-linux-cross-distro-consumer'

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}
Set-Location -LiteralPath $SourceRoot

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Block
    )

    Write-Host "=== $Name ==="
    & $Block
    if ($LASTEXITCODE -ne 0) {
        $script:gateExit = $LASTEXITCODE
        throw "step failed: $Name exit=$LASTEXITCODE"
    }
}

function Remove-DockerResources {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    docker rm -f $ProducerContainer 2>$null | Out-Null
    docker rm -f $ConsumerContainer 2>$null | Out-Null
    docker volume rm -f $ArtifactVolume 2>$null | Out-Null
    $ErrorActionPreference = $previousErrorActionPreference
}

function Invoke-DockerPrune {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    Write-Host '=== docker container prune (stopped only) ==='
    docker container prune -f 2>$null | Out-Null
    Write-Host '=== docker image prune dangling ==='
    docker image prune -f 2>$null | Out-Null
    $ErrorActionPreference = $previousErrorActionPreference
}

function Convert-ToDockerMountPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ($Path -match '^[A-Za-z]:\\') {
        $drive = $Path.Substring(0, 1).ToLowerInvariant()
        $rest = $Path.Substring(2).Replace('\', '/')
        return "/$drive$rest"
    }
    return $Path
}

$report = [ordered]@{
    schema          = 1
    gate            = $GateId
    startedAtUtc    = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot      = $SourceRoot
    producerImage   = $ProducerImage
    consumerImage   = $ConsumerImage
    producer        = $ProducerContainer
    consumer        = $ConsumerContainer
    artifactVolume  = $ArtifactVolume
    head            = (git rev-parse HEAD 2>$null)
    ok              = $false
}

$sourceMount = Convert-ToDockerMountPath -Path $SourceRoot
$gateExit = 1
$failureMessage = ''

try {
    Remove-DockerResources

    if (-not $SkipProducerImageBuild) {
        Invoke-Checked "docker build $ProducerDockerfile" {
            docker build -f $ProducerDockerfile -t $ProducerImage $SourceRoot
        }
    }

    if (-not $SkipConsumerImageBuild) {
        Invoke-Checked "docker build $ConsumerDockerfile" {
            docker build -f $ConsumerDockerfile -t $ConsumerImage $SourceRoot
        }
    }

    Invoke-Checked "docker volume create $ArtifactVolume" {
        docker volume create $ArtifactVolume | Out-Null
    }

    $producerArgs = @(
        'run', '--name', $ProducerContainer,
        '--cpus=2', '--memory=8g',
        '-e', 'VCPKG_ROOT=/opt/vcpkg',
        '-e', 'VCPKG_FORCE_SYSTEM_BINARIES=1',
        '-e', 'VCPKG_DISABLE_METRICS=1',
        '-v', "${sourceMount}:/work/tina",
        '-v', "${ArtifactVolume}:/output",
        '-w', '/work/tina',
        $ProducerImage,
        'bash', '-lc', "chmod +x '$ProducerScript' && '$ProducerScript'"
    )
    Invoke-Checked "docker run $ProducerContainer" {
        & docker @producerArgs
    }

    # Always mount the tip consumer script + verification cmake from the source
    # tree so gate logic tracks the repo without requiring an image rebuild for
    # every script tweak. Producer source is still NOT mounted into consumer.
    $consumerArgs = @(
        'run', '--name', $ConsumerContainer,
        '--cpus=2', '--memory=8g',
        '-e', 'VCPKG_ROOT=/opt/vcpkg',
        '-e', 'VCPKG_FORCE_SYSTEM_BINARIES=1',
        '-e', 'VCPKG_DISABLE_METRICS=1',
        '-v', "${ArtifactVolume}:/input:ro",
        '-v', "${sourceMount}/tools/linux/run-sdk-cross-distro-consumer-gate.sh:/usr/local/bin/tina-sdk-cross-distro-consumer-gate:ro",
        '-v', "${sourceMount}/cmake/VerifyInstalledTinaSdkHeaders.cmake:/opt/tina-sdk-gate/cmake/VerifyInstalledTinaSdkHeaders.cmake:ro",
        '-v', "${sourceMount}/cmake/VerifyRelocatedTinaSdkPackage.cmake:/opt/tina-sdk-gate/cmake/VerifyRelocatedTinaSdkPackage.cmake:ro",
        $ConsumerImage,
        'bash', $ConsumerScript
    )
    Invoke-Checked "docker run $ConsumerContainer" {
        & docker @consumerArgs
    }

    $gateExit = 0
} catch {
    $failureMessage = $_.Exception.Message
    if ($gateExit -eq 0) {
        $gateExit = 1
    }
} finally {
    Write-Host '=== remove cross-distribution gate resources ==='
    Remove-DockerResources

    if ($PruneAfter) {
        Invoke-DockerPrune
    }

    $report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    $report.exitCode = $gateExit
    $report.ok = ($gateExit -eq 0)
    if (-not [string]::IsNullOrWhiteSpace($failureMessage)) {
        $report['error'] = $failureMessage
    }

    if ($OutJson) {
        $outputDirectory = Split-Path -Parent $OutJson
        if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
            New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
        }
        ($report | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $OutJson -Encoding utf8
        Write-Host "wrote $OutJson"
    }
}

if ($gateExit -ne 0) {
    Write-Error "$GateId failed exit=$gateExit`: $failureMessage" -ErrorAction Continue
    exit $gateExit
}

Write-Host "$GateId OK"
exit 0
