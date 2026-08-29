<#
.SYNOPSIS
  Build and run the Windows network gate (NET-001 topology).

.DESCRIPTION
  Two build trees, because the transports are unconditional while TLS is an
  optional feature:

    windows-msvc-vnext             -> tina_network_tests, tina_sample_network
    windows-msvc-vnext-network-tls -> tina_network_tls_tests

  Runs the unit suites, then the sample twice to prove the evidence is
  deterministic, then asserts every evidence field by value.

  Does not use CTest. Does not clean-first wipe. Exit non-zero on first failure.

.PARAMETER SampleFrames
  Frames for tina_sample_network. Several fields scale with it, so the expected
  values below are derived rather than hardcoded.

.PARAMETER SkipTls
  Skip the TLS tree. Use only when mbedTLS cannot be provisioned; the gate then
  proves nothing about TLS, so it is not the default.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$ConfigurePreset = 'windows-msvc-vnext',
    [string]$BuildPreset = 'windows-vnext-debug',
    [string]$TlsConfigurePreset = 'windows-msvc-vnext-network-tls',
    [string]$TlsBuildPreset = 'windows-vnext-network-tls-debug',
    [int]$SampleFrames = 300,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [switch]$SkipTls,
    [string]$OutJson = '',
    [string]$BinDir = '',
    [string]$TlsBinDir = ''
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

function Resolve-BinDir {
    param([string]$Value, [string]$Preset)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        $Value = Join-Path $SourceRoot "out\build\$Preset\bin\Debug"
    } elseif (-not [System.IO.Path]::IsPathRooted($Value)) {
        $Value = Join-Path $SourceRoot $Value
    }
    return [System.IO.Path]::GetFullPath($Value)
}

$BinDir = Resolve-BinDir -Value $BinDir -Preset $ConfigurePreset
$TlsBinDir = Resolve-BinDir -Value $TlsBinDir -Preset $TlsConfigurePreset

$report = [ordered]@{
    gate          = 'network'
    schema        = 1
    startedAtUtc  = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot    = $SourceRoot
    sampleFrames  = $SampleFrames
    tlsIncluded   = (-not $SkipTls)
    steps         = @()
    ok            = $false
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

function Invoke-Exe {
    param([string]$Dir, [string]$Name, [string[]]$ExeArgs = @())
    $path = Join-Path $Dir $Name
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Step -Name $Name -ExitCode 1 -Detail "missing executable: $path"
    }
    $out = & $path @ExeArgs 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $out }
}

# --- default tree: transports, protocols, DNS, and the sample -----------------

$targets = @('tina_network_tests', 'tina_sample_network')
if (-not $SkipConfigure) {
    & cmake --preset $ConfigurePreset
    Add-Step -Name 'configure' -ExitCode $LASTEXITCODE
}
if (-not $SkipBuild) {
    $buildArgs = @('--build', '--preset', $BuildPreset, '--parallel', '2', '--target') +
        $targets + @('--', '/nr:false')
    & cmake @buildArgs
    Add-Step -Name 'build' -ExitCode $LASTEXITCODE
}
if (-not (Test-Path -LiteralPath $BinDir)) {
    Add-Step -Name 'binDir' -ExitCode 1 `
        -Detail "missing directory: $BinDir; pass -BinDir for a custom preset output"
}

$networkTests = Invoke-Exe -Dir $BinDir -Name 'tina_network_tests.exe' -ExeArgs @('--gtest_color=no')
if ($networkTests.ExitCode -ne 0) {
    Add-Step -Name 'tina_network_tests' -ExitCode $networkTests.ExitCode `
        -Detail $networkTests.Output.Trim()
}
# One skip is expected: this host's loopback absorbs a multi-megabyte send, so
# genuine backpressure is unreachable. Reporting the count keeps a second,
# unrelated skip from hiding behind it.
#
# The summary block repeats each skipped name, so the count comes from the
# "N test(s), listed below" line rather than from counting [ SKIPPED ] markers --
# doing that reports three for one skip.
$networkSkips = 0
if ($networkTests.Output -match '\[\s+SKIPPED\s+\]\s+(\d+)\s+tests?,') {
    $networkSkips = [int]$Matches[1]
}
Add-Step -Name 'tina_network_tests' -ExitCode 0 -Detail "skips=$networkSkips"

# --- sample: run twice, then assert every field -------------------------------

# Determinism matters more than any single field: a run that varies means state is
# leaking between frames or a counter is reading uninitialised memory.
$firstRun = Invoke-Exe -Dir $BinDir -Name 'tina_sample_network.exe' -ExeArgs @("--frames=$SampleFrames")
if ($firstRun.ExitCode -ne 0) {
    Add-Step -Name 'tina_sample_network' -ExitCode $firstRun.ExitCode `
        -Detail $firstRun.Output.Trim()
}
$secondRun = Invoke-Exe -Dir $BinDir -Name 'tina_sample_network.exe' -ExeArgs @("--frames=$SampleFrames")
if ($secondRun.ExitCode -ne 0) {
    Add-Step -Name 'tina_sample_network:rerun' -ExitCode $secondRun.ExitCode `
        -Detail $secondRun.Output.Trim()
}

function Select-EvidenceLine {
    param([string]$Output)
    return $Output -split "`n" |
        Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_network"' } |
        Select-Object -Last 1
}

$firstLine = Select-EvidenceLine -Output $firstRun.Output
$secondLine = Select-EvidenceLine -Output $secondRun.Output
if (-not $firstLine) {
    Add-Step -Name 'tina_sample_network' -ExitCode 1 `
        -Detail "no ok evidence line; output=$($firstRun.Output.Trim())"
}
if ($firstLine.Trim() -ne $secondLine.Trim()) {
    Add-Step -Name 'tina_sample_network:deterministic' -ExitCode 1 `
        -Detail "runs differ`nfirst= $($firstLine.Trim())`nsecond=$($secondLine.Trim())"
}
Add-Step -Name 'tina_sample_network:deterministic' -ExitCode 0 -Detail 'two runs byte-identical'

# Parsed as JSON with typed comparisons rather than substring regexes: an
# unanchored pattern like 'httpStatusCode\":200' also matches 2000, so a value that
# grew a digit would pass silently.
$evidence = $firstLine | ConvertFrom-Json
$expected = [ordered]@{
    evidenceSchema              = 1
    frames                      = [int64]$SampleFrames
    udpDatagramsSent            = [int64]1
    udpSenderEndpointMatched    = $true
    dnsResolvedNumericLiteral   = $true
    dnsRejectedUnresolvableName = $true
    # One pump per frame: the resolver is driven from the same loop as everything
    # else, so a mismatch means a frame skipped it.
    dnsPumpCount                = [int64]$SampleFrames
    # HTTP, plain TCP and WebSocket each take one, so the listener served three
    # peers rather than reusing one.
    tcpConnectionsAccepted      = [int64]3
    tcpClientToServerMatched    = $true
    tcpServerToClientMatched    = $true
    httpRequestCompleted        = $true
    httpStatusCode              = [int64]200
    httpBodyMatched             = $true
    webSocketHandshakeCompleted = $true
    webSocketEchoMatched        = $true
}
foreach ($field in $expected.Keys) {
    $actual = $evidence.$field
    if ($null -eq $actual) {
        Add-Step -Name "sample:$field" -ExitCode 1 -Detail 'field missing from evidence'
    }
    if ($actual -ne $expected[$field]) {
        Add-Step -Name "sample:$field" -ExitCode 1 `
            -Detail "expected $($expected[$field]); actual $actual"
    }
}

# Lower bounds rather than equalities: loopback may coalesce or split, so the
# contract is "at least one arrived", not an exact count.
if ([int64]$evidence.udpDatagramsReceived -lt 1) {
    Add-Step -Name 'sample:udpDatagramsReceived' -ExitCode 1 `
        -Detail "expected at least 1; actual $($evidence.udpDatagramsReceived)"
}
if ([int64]$evidence.webSocketFramesSent -lt 1) {
    Add-Step -Name 'sample:webSocketFramesSent' -ExitCode 1 `
        -Detail "expected at least 1; actual $($evidence.webSocketFramesSent)"
}
Add-Step -Name 'tina_sample_network' -ExitCode 0 `
    -Detail "evidenceSchema=1 frames=$SampleFrames accepted=$($evidence.tcpConnectionsAccepted)"

# --- installed public headers -------------------------------------------------

# The scanner rejects socket, Winsock and mbedTLS tokens, so this is what keeps a
# platform type from reaching include/tina.
$scan = & cmake -DTINA_SDK_INCLUDE_DIR=include -P (Join-Path $SourceRoot 'cmake\VerifyInstalledTinaSdkHeaders.cmake') 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    Add-Step -Name 'publicHeaderScan' -ExitCode $LASTEXITCODE -Detail $scan.Trim()
}
Add-Step -Name 'publicHeaderScan' -ExitCode 0 -Detail $scan.Trim()

# --- TLS tree ----------------------------------------------------------------

if (-not $SkipTls) {
    if (-not $SkipConfigure) {
        & cmake --preset $TlsConfigurePreset
        Add-Step -Name 'tlsConfigure' -ExitCode $LASTEXITCODE
    }
    if (-not $SkipBuild) {
        & cmake --build --preset $TlsBuildPreset --parallel 2 --target tina_network_tls_tests -- /nr:false
        Add-Step -Name 'tlsBuild' -ExitCode $LASTEXITCODE
    }
    if (-not (Test-Path -LiteralPath $TlsBinDir)) {
        Add-Step -Name 'tlsBinDir' -ExitCode 1 -Detail "missing directory: $TlsBinDir"
    }

    $tlsTests = Invoke-Exe -Dir $TlsBinDir -Name 'tina_network_tls_tests.exe' `
        -ExeArgs @('--gtest_color=no')
    if ($tlsTests.ExitCode -ne 0) {
        Add-Step -Name 'tina_network_tls_tests' -ExitCode $tlsTests.ExitCode `
            -Detail $tlsTests.Output.Trim()
    }

    # These four are the difference between "TLS compiles" and "TLS works". A
    # green suite that skipped them would prove nothing about the handshake, so
    # their presence is asserted rather than assumed.
    $required = @(
        'CompletesHandshakeWithTrustedCertificate',
        'RejectsCertificateWithMismatchedHostname',
        'HttpRequestRunsOverTlsStream',
        'WebSocketRunsOverTlsStream'
    )
    foreach ($name in $required) {
        if ($tlsTests.Output -notmatch "\[\s+OK\s+\]\s+TlsHandshakeTest\.$name") {
            Add-Step -Name "tls:$name" -ExitCode 1 -Detail 'did not run or did not pass'
        }
    }

    # The platform store is what a client reaching a public endpoint depends on.
    # A skip here is legitimate on a host without one, so it is recorded rather
    # than failed -- but it must be visible in the report.
    $storeRead = ($tlsTests.Output -match '\[\s+OK\s+\]\s+TlsTrustStoreTest\.ReadsPlatformAnchors')
    Add-Step -Name 'tls:platformTrustStore' -ExitCode 0 `
        -Detail "readPlatformAnchors=$storeRead"

    Add-Step -Name 'tina_network_tls_tests' -ExitCode 0 -Detail 'handshake evidence present'
}

$report.finishedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
$report.sampleEvidence = $firstLine.Trim()
$report.ok = $true

if ($OutJson) {
    $dir = Split-Path -Parent $OutJson
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    ($report | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $OutJson -Encoding utf8
    Write-Output "wrote $OutJson"
}

$tlsNote = if ($SkipTls) { 'tls=skipped' } else { 'tls=included' }
Write-Output "network gate ok frames=$SampleFrames $tlsNote"
exit 0
