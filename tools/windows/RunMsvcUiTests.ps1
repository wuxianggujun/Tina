<#
.SYNOPSIS
  Incrementally build and directly run Tina UI tests with MSVC.

.DESCRIPTION
  Uses CMake presets, never clean-first, disables MSBuild node reuse, and runs
  GoogleTest executables directly. Configure runs only when the selected build
  tree has no CMakeCache.txt or when ForceConfigure is specified.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$ConfigurePreset = 'windows-msvc-vnext-bgfx',
    [string]$BuildPreset = 'windows-vnext-bgfx-debug',
    [string]$BuildTree = 'windows-msvc-vnext-bgfx',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string[]]$Targets = @('tina_ui_tests'),
    [string[]]$TestExecutables = @('tina_ui_tests.exe'),
    [string]$GTestFilter = '',
    [ValidateRange(1, 64)]
    [int]$Jobs = 2,
    [switch]$ForceConfigure,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [string]$BinDir = ''
)
#Requires -Version 5.1

$ErrorActionPreference = 'Stop'
$buildProcessNames = @('cmake', 'MSBuild', 'cl', 'link')

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Step,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    Write-Host "=== $Step ==="
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE"
    }
}

function Wait-ForNewBuildProcessesToExit {
    param(
        [int[]]$BaselineProcessIds,
        [int]$TimeoutSeconds = 10
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $remaining = @(
            Get-Process -Name $buildProcessNames -ErrorAction SilentlyContinue |
                Where-Object { $BaselineProcessIds -notcontains $_.Id }
        )
        if ($remaining.Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)

    $details = ($remaining | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '
    throw "new compiler processes did not exit after the build: $details"
}

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $SourceRoot = (Get-Location).Path
    }
}
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path

if ($Targets.Count -eq 0) {
    throw 'Targets must contain at least one CMake target.'
}
if (-not $SkipTests -and $TestExecutables.Count -eq 0) {
    throw 'TestExecutables must contain at least one executable unless SkipTests is set.'
}

if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = Join-Path $SourceRoot "out\build\$BuildTree\bin\$Configuration"
} elseif (-not [System.IO.Path]::IsPathRooted($BinDir)) {
    $BinDir = Join-Path $SourceRoot $BinDir
}
$BinDir = [System.IO.Path]::GetFullPath($BinDir)
$cachePath = Join-Path $SourceRoot "out\build\$BuildTree\CMakeCache.txt"

$previousLocation = Get-Location
try {
    Set-Location -LiteralPath $SourceRoot

    if ($ForceConfigure -or -not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        Invoke-Checked -Step "configure $ConfigurePreset" -Command {
            & cmake --preset $ConfigurePreset
        }
    }

    if (-not $SkipBuild) {
        [int[]]$baselineProcessIds = @(
            Get-Process -Name $buildProcessNames -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty Id
        )
        $buildArgs = @('--build', '--preset', $BuildPreset, '--parallel', "$Jobs", '--target') +
            $Targets + @('--', '/nr:false')
        try {
            Invoke-Checked -Step "build $($Targets -join ', ')" -Command {
                & cmake @buildArgs
            }
        } finally {
            Wait-ForNewBuildProcessesToExit -BaselineProcessIds $baselineProcessIds
        }
    }

    if (-not $SkipTests) {
        foreach ($testExecutable in $TestExecutables) {
            $testPath = Join-Path $BinDir $testExecutable
            if (-not (Test-Path -LiteralPath $testPath -PathType Leaf)) {
                throw "missing test executable: $testPath"
            }

            $testArgs = @('--gtest_brief=1', '--gtest_color=yes')
            if (-not [string]::IsNullOrWhiteSpace($GTestFilter)) {
                $testArgs += "--gtest_filter=$GTestFilter"
            }
            Invoke-Checked -Step "test $testExecutable" -Command {
                & $testPath @testArgs
            }
        }
    }
} finally {
    Set-Location -LiteralPath $previousLocation
}

Write-Host 'MSVC UI test gate passed.'
