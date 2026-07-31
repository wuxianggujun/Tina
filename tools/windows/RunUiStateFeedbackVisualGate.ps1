#Requires -Version 5.1
<#
.SYNOPSIS
  Run the UI-STATE-FEEDBACK Dark/Light differential visual gate.

.DESCRIPTION
  Launches the FreeType/bgfx UI showcase once per theme and drives real Win32 pointer
  input through GLFW and the Runtime UI route. Each state is captured twice with an
  identical client-frame fingerprint before ROI differences are evaluated.

  The gate is deliberately same-host/backend differential evidence. It verifies that
  normal, hover, pressed/drag, and focused product states are visibly distinct without
  claiming a cross-GPU exact golden.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$Exe = '',
    [ValidateSet('windows-vnext-bgfx-ui-freetype-debug')]
    [string]$BuildPreset = 'windows-vnext-bgfx-ui-freetype-debug',
    [string]$OutDir = 'artifacts/screenshots/ui-state-feedback',
    [int]$WarmupMs = 900,
    [int]$InputSettleMs = 180,
    [int]$CaptureIntervalMs = 70,
    [int]$CaptureAttempts = 4,
    [int]$ProcessExitTimeoutMs = 10000,
    [switch]$SkipBuild,
    [string]$OutJson = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if ($WarmupMs -lt 0 -or $InputSettleMs -lt 0 -or $CaptureIntervalMs -lt 1) {
    throw 'WarmupMs/InputSettleMs must be non-negative and CaptureIntervalMs must be positive'
}
if ($CaptureAttempts -lt 2) {
    throw 'CaptureAttempts must be at least two for repeatability evidence'
}
if ($ProcessExitTimeoutMs -lt 1000) {
    throw 'ProcessExitTimeoutMs must be at least 1000'
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

$expectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $SourceRoot `
    'out\build\windows-msvc-vnext-bgfx-ui-freetype'))
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $expectedBuildRoot 'bin\Debug\tina_sample_ui_showcase.exe'
} elseif (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $SourceRoot $Exe
}
$Exe = [IO.Path]::GetFullPath($Exe)
$expectedBuildPrefix = $expectedBuildRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $Exe.StartsWith($expectedBuildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "UI-STATE-FEEDBACK gate only accepts the fixed MSVC UI/FreeType/bgfx tree: $expectedBuildRoot"
}

if (-not $SkipBuild) {
    & cmake --build --preset $BuildPreset --target tina_sample_ui_showcase --parallel 2 -- /nr:false
    if ($LASTEXITCODE -ne 0) {
        throw "tina_sample_ui_showcase build failed exit=$LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "missing executable: $Exe"
}
$cachePath = Join-Path $expectedBuildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "missing MSVC CMake cache: $cachePath"
}
$generatorLine = Get-Content -LiteralPath $cachePath -Encoding utf8 |
    Where-Object { $_ -match '^CMAKE_GENERATOR:' } |
    Select-Object -First 1
if ($generatorLine -notmatch '=Visual Studio ') {
    throw "UI-STATE-FEEDBACK gate requires a Visual Studio/MSVC cache: $generatorLine"
}
$exeInfo = Get-Item -LiteralPath $Exe
$exeSha256 = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()
$buildPerformed = -not [bool]$SkipBuild

if (-not ('TinaUiStateFeedbackWin32' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class TinaUiStateFeedbackWin32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr value);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref Point point);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern bool PostMessage(
        IntPtr hWnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr dpiContext);

    public const uint PrintClientOnly = 1;
    public const uint PrintRenderFullContent = 2;
    public const uint MouseLeftDown = 0x0002;
    public const uint MouseLeftUp = 0x0004;
    public const uint WindowClose = 0x0010;
    public const int ShowRestore = 9;

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct Point { public int X, Y; }
}
'@
}

$designWidth = 1280
$designHeight = 980
$neutralPoint = @(150, 700)
$inputPoints = [ordered]@{
    slider = @(1103, 216)
    checkbox = @(303, 340)
    textEdit = @(500, 529)
    radio = @(480, 620)
    listSelected = @(600, 882)
    treeSelected = @(900, 831)
}
$roiDefinitions = [ordered]@{
    slider = @(780, 198, 1234, 236)
    checkbox = @(288, 326, 318, 355)
    textEdit = @(288, 505, 709, 553)
    radio = @(458, 601, 500, 644)
    listSelected = @(496, 870, 708, 896)
    treeSelected = @(780, 818, 1030, 844)
    disabledButton = @(289, 262, 486, 306)
    disabledButtonChrome = @(440, 270, 480, 298)
    enabledButtonChrome = @(662, 270, 702, 298)
}

function Find-SampleWindow {
    param([Parameter(Mandatory = $true)][int]$ProcessId)

    $found = @{ Hwnd = [IntPtr]::Zero; ProcessId = $ProcessId }
    $callback = [TinaUiStateFeedbackWin32+EnumWindowsProc]{
        param([IntPtr]$Hwnd, [IntPtr]$Value)
        [uint32]$owner = 0
        [void][TinaUiStateFeedbackWin32]::GetWindowThreadProcessId($Hwnd, [ref]$owner)
        if ($owner -ne [uint32]$found.ProcessId) { return $true }
        if (-not [TinaUiStateFeedbackWin32]::IsWindowVisible($Hwnd)) { return $true }
        $rect = New-Object TinaUiStateFeedbackWin32+Rect
        if (-not [TinaUiStateFeedbackWin32]::GetClientRect($Hwnd, [ref]$rect)) { return $true }
        if (($rect.Right - $rect.Left) -lt 320 -or ($rect.Bottom - $rect.Top) -lt 180) { return $true }
        $found.Hwnd = $Hwnd
        return $false
    }
    [void][TinaUiStateFeedbackWin32]::EnumWindows($callback, [IntPtr]::Zero)
    return $found.Hwnd
}

function Get-ClientExtent {
    param([Parameter(Mandatory = $true)][IntPtr]$Hwnd)

    $rect = New-Object TinaUiStateFeedbackWin32+Rect
    if (-not [TinaUiStateFeedbackWin32]::GetClientRect($Hwnd, [ref]$rect)) {
        throw 'GetClientRect failed'
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 320 -or $height -lt 180) {
        throw "invalid showcase client extent ${width}x${height}"
    }
    return [pscustomobject]@{ width = [int]$width; height = [int]$height }
}

function Convert-DesignPointToScreen {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)][int]$DesignX,
        [Parameter(Mandatory = $true)][int]$DesignY
    )

    if ($DesignX -lt 0 -or $DesignX -ge $designWidth -or $DesignY -lt 0 -or $DesignY -ge $designHeight) {
        throw "design input point outside ${designWidth}x${designHeight}: ${DesignX},${DesignY}"
    }
    $point = New-Object TinaUiStateFeedbackWin32+Point
    $point.X = [int][Math]::Round($DesignX * ([double]$Extent.width / $designWidth))
    $point.Y = [int][Math]::Round($DesignY * ([double]$Extent.height / $designHeight))
    if (-not [TinaUiStateFeedbackWin32]::ClientToScreen($Hwnd, [ref]$point)) {
        throw 'ClientToScreen failed'
    }
    return $point
}

function Move-DesignPointer {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)][int]$DesignX,
        [Parameter(Mandatory = $true)][int]$DesignY
    )

    $screen = Convert-DesignPointToScreen -Hwnd $Hwnd -Extent $Extent -DesignX $DesignX -DesignY $DesignY
    if (-not [TinaUiStateFeedbackWin32]::SetCursorPos($screen.X, $screen.Y)) {
        throw 'SetCursorPos failed'
    }
    $target = [TinaUiStateFeedbackWin32]::WindowFromPoint($screen)
    if ($target -ne $Hwnd) {
        throw "pointer target mismatch expected=$Hwnd actual=$target"
    }
}

function Assert-InputTarget {
    param([Parameter(Mandatory = $true)][IntPtr]$Hwnd)

    if ([TinaUiStateFeedbackWin32]::GetForegroundWindow() -ne $Hwnd) {
        [void][TinaUiStateFeedbackWin32]::SetForegroundWindow($Hwnd)
        Start-Sleep -Milliseconds 30
    }
    if ([TinaUiStateFeedbackWin32]::GetForegroundWindow() -ne $Hwnd) {
        throw "input target is not foreground: $Hwnd"
    }
    $cursor = New-Object TinaUiStateFeedbackWin32+Point
    if (-not [TinaUiStateFeedbackWin32]::GetCursorPos([ref]$cursor)) {
        throw 'GetCursorPos failed before pointer input'
    }
    $target = [TinaUiStateFeedbackWin32]::WindowFromPoint($cursor)
    if ($target -ne $Hwnd) {
        throw "pointer input would target another window expected=$Hwnd actual=$target"
    }
}

function Send-PrimaryDown {
    [TinaUiStateFeedbackWin32]::mouse_event(
        [TinaUiStateFeedbackWin32]::MouseLeftDown, 0, 0, 0, [UIntPtr]::Zero)
}

function Send-PrimaryUp {
    [TinaUiStateFeedbackWin32]::mouse_event(
        [TinaUiStateFeedbackWin32]::MouseLeftUp, 0, 0, 0, [UIntPtr]::Zero)
}

function Invoke-ClickAndHover {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)]$ClickPoint,
        [Parameter(Mandatory = $true)]$HoverPoint
    )

    Move-DesignPointer -Hwnd $Hwnd -Extent $Extent -DesignX $ClickPoint[0] -DesignY $ClickPoint[1]
    Start-Sleep -Milliseconds 80
    $primaryHeld = $false
    try {
        Assert-InputTarget -Hwnd $Hwnd
        $primaryHeld = $true
        Send-PrimaryDown
        Start-Sleep -Milliseconds 45
        Send-PrimaryUp
        $primaryHeld = $false
    } finally {
        if ($primaryHeld) {
            Send-PrimaryUp
        }
    }
    Start-Sleep -Milliseconds 80
    Move-DesignPointer -Hwnd $Hwnd -Extent $Extent -DesignX $HoverPoint[0] -DesignY $HoverPoint[1]
    Start-Sleep -Milliseconds $InputSettleMs
}

function Start-PrimaryHold {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)]$Point,
        [Parameter(Mandatory = $true)][ref]$Held
    )

    Move-DesignPointer -Hwnd $Hwnd -Extent $Extent -DesignX $Point[0] -DesignY $Point[1]
    Start-Sleep -Milliseconds 80
    Assert-InputTarget -Hwnd $Hwnd
    $Held.Value = $true
    Send-PrimaryDown
    Start-Sleep -Milliseconds $InputSettleMs
}

function Test-UsefulBitmap {
    param([Parameter(Mandatory = $true)][Drawing.Bitmap]$Image)

    $histogram = @{}
    $samples = 0
    $dark = 0
    $stepX = [Math]::Max(1, [int]($Image.Width / 40))
    $stepY = [Math]::Max(1, [int]($Image.Height / 40))
    for ($y = 0; $y -lt $Image.Height; $y += $stepY) {
        for ($x = 0; $x -lt $Image.Width; $x += $stepX) {
            $color = $Image.GetPixel($x, $y)
            $key = '{0:X2}{1:X2}{2:X2}' -f $color.R, $color.G, $color.B
            if ($histogram.ContainsKey($key)) { $histogram[$key]++ } else { $histogram[$key] = 1 }
            if ($color.R -lt 8 -and $color.G -lt 8 -and $color.B -lt 8) { $dark++ }
            $samples++
        }
    }
    $dominant = $histogram.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1
    $dominantRatio = $dominant.Value / [double]$samples
    $darkRatio = $dark / [double]$samples
    return $histogram.Count -ge 3 -and $dominantRatio -lt 0.995 -and $darkRatio -lt 0.98
}

function Capture-Client {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $extent = Get-ClientExtent -Hwnd $Hwnd
    $bitmap = New-Object Drawing.Bitmap $extent.width, $extent.height, `
        ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $method = 'none'
    try {
        $graphics.Clear([Drawing.Color]::Magenta)
        $hdc = $graphics.GetHdc()
        try {
            $flags = [TinaUiStateFeedbackWin32]::PrintClientOnly -bor `
                [TinaUiStateFeedbackWin32]::PrintRenderFullContent
            if ([TinaUiStateFeedbackWin32]::PrintWindow($Hwnd, $hdc, $flags) -or
                [TinaUiStateFeedbackWin32]::PrintWindow(
                    $Hwnd, $hdc, [TinaUiStateFeedbackWin32]::PrintClientOnly)) {
                $method = 'PrintWindow'
            }
        } finally {
            $graphics.ReleaseHdc($hdc)
        }
        if ($method -eq 'PrintWindow' -and -not (Test-UsefulBitmap -Image $bitmap)) {
            $method = 'none'
        }
        if ($method -eq 'none') {
            if ([TinaUiStateFeedbackWin32]::GetForegroundWindow() -ne $Hwnd) {
                throw 'CopyFromScreen fallback requires the showcase foreground window'
            }
            $origin = New-Object TinaUiStateFeedbackWin32+Point
            if (-not [TinaUiStateFeedbackWin32]::ClientToScreen($Hwnd, [ref]$origin)) {
                throw 'ClientToScreen failed for CopyFromScreen fallback'
            }
            $center = New-Object TinaUiStateFeedbackWin32+Point
            $center.X = $origin.X + [int]($extent.width / 2)
            $center.Y = $origin.Y + [int]($extent.height / 2)
            if ([TinaUiStateFeedbackWin32]::WindowFromPoint($center) -ne $Hwnd) {
                throw 'CopyFromScreen fallback client center is occluded'
            }
            $graphics.CopyFromScreen(
                $origin.X, $origin.Y, 0, 0,
                (New-Object Drawing.Size $extent.width, $extent.height))
            $method = 'CopyFromScreenAfterInvalidPrintWindow'
        }
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    return $method
}

function Test-UsefulCapture {
    param([Parameter(Mandatory = $true)][string]$Path)

    $image = [Drawing.Bitmap]::FromFile($Path)
    try {
        return Test-UsefulBitmap -Image $image
    } finally {
        $image.Dispose()
    }
}

function Get-RoiFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$DesignRect
    )

    $image = [Drawing.Bitmap]::FromFile($Path)
    try {
        $left = [Math]::Max(0, [int][Math]::Floor($DesignRect[0] * $image.Width / [double]$designWidth))
        $top = [Math]::Max(0, [int][Math]::Floor($DesignRect[1] * $image.Height / [double]$designHeight))
        $right = [Math]::Min(
            $image.Width, [int][Math]::Ceiling($DesignRect[2] * $image.Width / [double]$designWidth))
        $bottom = [Math]::Min(
            $image.Height, [int][Math]::Ceiling($DesignRect[3] * $image.Height / [double]$designHeight))
        $width = $right - $left
        $height = $bottom - $top
        if ($width -lt 1 -or $height -lt 1) {
            throw "empty ROI: $Name"
        }

        $bytes = New-Object byte[] ($width * $height * 3)
        $histogram = @{}
        [int64]$sumR = 0
        [int64]$sumG = 0
        [int64]$sumB = 0
        $offset = 0
        for ($y = $top; $y -lt $bottom; ++$y) {
            for ($x = $left; $x -lt $right; ++$x) {
                $color = $image.GetPixel($x, $y)
                $bytes[$offset++] = $color.R
                $bytes[$offset++] = $color.G
                $bytes[$offset++] = $color.B
                $sumR += $color.R
                $sumG += $color.G
                $sumB += $color.B
                $key = '{0:X2}{1:X2}{2:X2}' -f $color.R, $color.G, $color.B
                if ($histogram.ContainsKey($key)) { $histogram[$key]++ } else { $histogram[$key] = 1 }
            }
        }
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $hashBytes = $sha256.ComputeHash($bytes)
        } finally {
            $sha256.Dispose()
        }
        $hash = -join ($hashBytes | ForEach-Object { $_.ToString('x2') })
        $pixelCount = [double]($width * $height)
        return [pscustomobject]@{
            name = $Name
            left = $left
            top = $top
            width = $width
            height = $height
            sha256 = $hash
            avgRgb = @(
                [Math]::Round($sumR / $pixelCount, 2),
                [Math]::Round($sumG / $pixelCount, 2),
                [Math]::Round($sumB / $pixelCount, 2))
            uniqueColors = [int]$histogram.Count
        }
    } finally {
        $image.Dispose()
    }
}

function Capture-StableState {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)][string]$ThemeDirectory,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $lastUsefulHash = $null
    $consecutiveUseful = 0
    $attempts = New-Object System.Collections.Generic.List[object]
    for ($attempt = 1; $attempt -le $CaptureAttempts; ++$attempt) {
        $path = Join-Path $ThemeDirectory ('{0}-{1:D2}.png' -f $Name, $attempt)
        $method = Capture-Client -Hwnd $Hwnd -Path $path
        $useful = Test-UsefulCapture -Path $path
        $fullHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        [void]$attempts.Add([pscustomobject]@{
            path = $path
            method = $method
            useful = [bool]$useful
            sha256 = $fullHash
        })
        if ($useful) {
            if ($fullHash -eq $lastUsefulHash) {
                ++$consecutiveUseful
            } else {
                $lastUsefulHash = $fullHash
                $consecutiveUseful = 1
            }
            if ($consecutiveUseful -ge 2) {
                $roiResults = [ordered]@{}
                foreach ($roiName in $roiDefinitions.Keys) {
                    $roiResults[$roiName] = Get-RoiFingerprint `
                        -Path $path -Name $roiName -DesignRect $roiDefinitions[$roiName]
                }
                $extent = Get-ClientExtent -Hwnd $Hwnd
                return [pscustomobject]@{
                    name = $Name
                    path = $path
                    width = $extent.width
                    height = $extent.height
                    sha256 = $fullHash
                    repeatCount = $consecutiveUseful
                    attempts = @($attempts | ForEach-Object { $_ })
                    rois = [pscustomobject]$roiResults
                }
            }
        } else {
            $lastUsefulHash = $null
            $consecutiveUseful = 0
        }
        if ($attempt -lt $CaptureAttempts) {
            Start-Sleep -Milliseconds $CaptureIntervalMs
        }
    }
    throw "$Name did not produce two identical useful client captures in $CaptureAttempts attempts"
}

function Read-ShowcaseSuccess {
    param(
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$Theme
    )

    $lines = @(Get-Content -LiteralPath $StdoutPath -Encoding utf8 -ErrorAction SilentlyContinue)
    $jsonLine = $lines |
        Where-Object { $_ -match '^\{"status":"ok","sample":"tina_sample_ui_showcase"' } |
        Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($jsonLine)) {
        throw "$Theme showcase did not emit success JSON: $($lines -join ' | ')"
    }
    $evidence = $jsonLine | ConvertFrom-Json
    $errors = New-Object System.Collections.Generic.List[string]
    if ([string]$evidence.initialTheme -ne $Theme) {
        [void]$errors.Add("initialTheme expected=$Theme actual=$($evidence.initialTheme)")
    }
    if ([string]$evidence.finalTheme -ne $Theme) {
        [void]$errors.Add("finalTheme expected=$Theme actual=$($evidence.finalTheme)")
    }
    if ([bool]$evidence.autoDemo) { [void]$errors.Add('autoDemo must be false') }
    if ([int64]$evidence.themeSwitches -ne 0) { [void]$errors.Add('themeSwitches must be zero') }
    if ([int64]$evidence.controls -ne 20) {
        [void]$errors.Add("controls expected=20 actual=$($evidence.controls)")
    }
    if ([string]$evidence.exit -ne 'PrimaryWindowRequestedClose') {
        [void]$errors.Add("exit expected=PrimaryWindowRequestedClose actual=$($evidence.exit)")
    }
    if ($errors.Count -ne 0) {
        throw "$Theme showcase evidence invalid: $($errors -join '; ')"
    }
    return $evidence
}

function Capture-ThemeMatrix {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('dark', 'light')][string]$Theme,
        [Parameter(Mandatory = $true)][string]$RunRoot
    )

    $themeDirectory = Join-Path $RunRoot $Theme
    New-Item -ItemType Directory -Path $themeDirectory -Force | Out-Null
    $stdoutPath = Join-Path $themeDirectory 'stdout.txt'
    $stderrPath = Join-Path $themeDirectory 'stderr.txt'
    $process = Start-Process -FilePath $Exe `
        -ArgumentList "--frame-delay-ms=4 --theme=$Theme" `
        -WorkingDirectory (Split-Path -Parent $Exe) `
        -PassThru -WindowStyle Normal `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

    $states = [ordered]@{}
    $primaryHeld = $false
    $closedNormally = $false
    $hwnd = [IntPtr]::Zero
    try {
        $windowDeadline = (Get-Date).AddSeconds(15)
        while ((Get-Date) -lt $windowDeadline -and -not $process.HasExited) {
            try { $process.Refresh() } catch {}
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
                $hwnd = $process.MainWindowHandle
                break
            }
            $hwnd = Find-SampleWindow -ProcessId $process.Id
            if ($hwnd -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 80
        }
        if ($hwnd -eq [IntPtr]::Zero) {
            throw "$Theme showcase did not publish a visible window"
        }

        [void][TinaUiStateFeedbackWin32]::ShowWindow(
            $hwnd, [TinaUiStateFeedbackWin32]::ShowRestore)
        if (-not [TinaUiStateFeedbackWin32]::SetForegroundWindow($hwnd)) {
            throw "$Theme showcase could not become the foreground window"
        }
        $extent = Get-ClientExtent -Hwnd $hwnd
        $aspect = $extent.width / [double]$extent.height
        $expectedAspect = $designWidth / [double]$designHeight
        if ([Math]::Abs($aspect - $expectedAspect) -gt 0.02) {
            throw "$Theme showcase client aspect mismatch actual=$aspect expected=$expectedAspect"
        }

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $neutralPoint[0] -DesignY $neutralPoint[1]
        Start-Sleep -Milliseconds $WarmupMs
        $states['normal'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'normal'

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $inputPoints.slider -HoverPoint $inputPoints.checkbox
        $states['slider-focus-checkbox-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'slider-focus-checkbox-hover'

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent -Point $inputPoints.slider `
            -Held ([ref]$primaryHeld)
        try {
            $states['slider-drag'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'slider-drag'
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $inputPoints.textEdit -HoverPoint $inputPoints.radio
        $states['text-focus-radio-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'text-focus-radio-hover'

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $inputPoints.textEdit[0] -DesignY $inputPoints.textEdit[1]
        Start-Sleep -Milliseconds $InputSettleMs
        $states['text-focus-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'text-focus-hover'

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent -Point $inputPoints.textEdit `
            -Held ([ref]$primaryHeld)
        try {
            $states['text-press'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'text-press'
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $inputPoints.listSelected -HoverPoint $inputPoints.treeSelected
        $states['list-focus-tree-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'list-focus-tree-hover'

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $inputPoints.listSelected[0] -DesignY $inputPoints.listSelected[1]
        Start-Sleep -Milliseconds $InputSettleMs
        $states['list-focus-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'list-focus-hover'

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent -Point $inputPoints.listSelected `
            -Held ([ref]$primaryHeld)
        try {
            $states['list-press'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'list-press'
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $inputPoints.treeSelected -HoverPoint $inputPoints.treeSelected
        $states['tree-focus-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'tree-focus-hover'

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent -Point $inputPoints.treeSelected `
            -Held ([ref]$primaryHeld)
        try {
            $states['tree-press'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'tree-press'
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $neutralPoint[0] -DesignY $neutralPoint[1]
        if (-not [TinaUiStateFeedbackWin32]::PostMessage(
                $hwnd, [TinaUiStateFeedbackWin32]::WindowClose, [UIntPtr]::Zero, [IntPtr]::Zero)) {
            throw "$Theme showcase WM_CLOSE failed"
        }
        $exitDeadline = (Get-Date).AddMilliseconds($ProcessExitTimeoutMs)
        while (-not $process.HasExited -and (Get-Date) -lt $exitDeadline) {
            Start-Sleep -Milliseconds 50
            try { $process.Refresh() } catch {}
        }
        if (-not $process.HasExited) {
            throw "$Theme showcase did not exit after WM_CLOSE"
        }
        $process.WaitForExit()
        $process.Refresh()
        $exitCode = [int]$process.ExitCode
        if ($exitCode -ne 0) {
            $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
            throw "$Theme showcase failed exit=$exitCode stderr=$stderr"
        }
        $evidence = Read-ShowcaseSuccess -StdoutPath $stdoutPath -Theme $Theme
        $closedNormally = $true
        return [pscustomobject]@{
            theme = $Theme
            clientWidth = $extent.width
            clientHeight = $extent.height
            processExitCode = $exitCode
            processEvidence = $evidence
            states = [pscustomobject]$states
        }
    } finally {
        try {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
            }
        } catch {
            Write-Warning "$Theme showcase pointer cleanup failed: $($_.Exception.Message)"
        } finally {
            try {
                if (-not $process.HasExited) {
                    if ($hwnd -ne [IntPtr]::Zero) {
                        [void][TinaUiStateFeedbackWin32]::PostMessage(
                            $hwnd, [TinaUiStateFeedbackWin32]::WindowClose, [UIntPtr]::Zero, [IntPtr]::Zero)
                        Start-Sleep -Milliseconds 250
                        try { $process.Refresh() } catch {}
                    }
                    if (-not $process.HasExited) {
                        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                    }
                }
            } finally {
                try { $process.WaitForExit() } catch {}
            }
        }
        if (-not $closedNormally -and $process.HasExited) {
            Write-Verbose "$Theme showcase cleanup completed"
        }
    }
}

function Get-State {
    param($ThemeResult, [string]$Name)
    return $ThemeResult.states.PSObject.Properties[$Name].Value
}

function Get-Roi {
    param($ThemeResult, [string]$StateName, [string]$RoiName)
    $state = Get-State -ThemeResult $ThemeResult -Name $StateName
    return $state.rois.PSObject.Properties[$RoiName].Value
}

$runRootBase = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $SourceRoot $OutDir }
New-Item -ItemType Directory -Path $runRootBase -Force | Out-Null
$runRoot = Join-Path $runRootBase ('{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $PID)
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$previousDpiContext =
    [TinaUiStateFeedbackWin32]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$originalCursor = New-Object TinaUiStateFeedbackWin32+Point
$cursorCaptured = [TinaUiStateFeedbackWin32]::GetCursorPos([ref]$originalCursor)
try {
    $dark = Capture-ThemeMatrix -Theme 'dark' -RunRoot $runRoot
    $light = Capture-ThemeMatrix -Theme 'light' -RunRoot $runRoot
} finally {
    try {
        if ($cursorCaptured) {
            [void][TinaUiStateFeedbackWin32]::SetCursorPos($originalCursor.X, $originalCursor.Y)
        }
    } finally {
        if ($previousDpiContext -ne [IntPtr]::Zero) {
            [void][TinaUiStateFeedbackWin32]::SetThreadDpiAwarenessContext($previousDpiContext)
        }
    }
}

$checks = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]

function Measure-RoiDifference {
    param(
        [Parameter(Mandatory = $true)][string]$FirstPath,
        [Parameter(Mandatory = $true)]$FirstDesignRect,
        [Parameter(Mandatory = $true)][string]$SecondPath,
        [Parameter(Mandatory = $true)]$SecondDesignRect
    )

    $firstImage = [Drawing.Bitmap]::FromFile($FirstPath)
    $secondImage = [Drawing.Bitmap]::FromFile($SecondPath)
    try {
        if ($firstImage.Width -ne $secondImage.Width -or $firstImage.Height -ne $secondImage.Height) {
            throw 'ROI comparison images have different dimensions'
        }
        $firstLeft = [Math]::Max(
            0, [int][Math]::Floor($FirstDesignRect[0] * $firstImage.Width / [double]$designWidth))
        $firstTop = [Math]::Max(
            0, [int][Math]::Floor($FirstDesignRect[1] * $firstImage.Height / [double]$designHeight))
        $firstRight = [Math]::Min(
            $firstImage.Width,
            [int][Math]::Ceiling($FirstDesignRect[2] * $firstImage.Width / [double]$designWidth))
        $firstBottom = [Math]::Min(
            $firstImage.Height,
            [int][Math]::Ceiling($FirstDesignRect[3] * $firstImage.Height / [double]$designHeight))
        $secondLeft = [Math]::Max(
            0, [int][Math]::Floor($SecondDesignRect[0] * $secondImage.Width / [double]$designWidth))
        $secondTop = [Math]::Max(
            0, [int][Math]::Floor($SecondDesignRect[1] * $secondImage.Height / [double]$designHeight))
        $secondRight = [Math]::Min(
            $secondImage.Width,
            [int][Math]::Ceiling($SecondDesignRect[2] * $secondImage.Width / [double]$designWidth))
        $secondBottom = [Math]::Min(
            $secondImage.Height,
            [int][Math]::Ceiling($SecondDesignRect[3] * $secondImage.Height / [double]$designHeight))
        $width = $firstRight - $firstLeft
        $height = $firstBottom - $firstTop
        if ($width -lt 1 -or $height -lt 1 -or
            $width -ne ($secondRight - $secondLeft) -or
            $height -ne ($secondBottom - $secondTop)) {
            throw 'ROI comparison rectangles have incompatible pixel extents'
        }

        [int64]$changedPixels = 0
        [int64]$totalAbsoluteChannelDelta = 0
        $maxChannelDelta = 0
        for ($y = 0; $y -lt $height; ++$y) {
            for ($x = 0; $x -lt $width; ++$x) {
                $firstColor = $firstImage.GetPixel($firstLeft + $x, $firstTop + $y)
                $secondColor = $secondImage.GetPixel($secondLeft + $x, $secondTop + $y)
                $deltaR = [Math]::Abs([int]$firstColor.R - [int]$secondColor.R)
                $deltaG = [Math]::Abs([int]$firstColor.G - [int]$secondColor.G)
                $deltaB = [Math]::Abs([int]$firstColor.B - [int]$secondColor.B)
                $pixelDelta = $deltaR + $deltaG + $deltaB
                if ($pixelDelta -ne 0) { ++$changedPixels }
                $totalAbsoluteChannelDelta += $pixelDelta
                $maxChannelDelta = [Math]::Max(
                    $maxChannelDelta, [Math]::Max($deltaR, [Math]::Max($deltaG, $deltaB)))
            }
        }
        [int64]$pixelCount = $width * $height
        $meanChangedChannelDelta = if ($changedPixels -eq 0) {
            0.0
        } else {
            $totalAbsoluteChannelDelta / [double]($changedPixels * 3)
        }
        return [pscustomobject]@{
            pixelCount = $pixelCount
            changedPixels = $changedPixels
            changedRatio = [Math]::Round($changedPixels / [double]$pixelCount, 6)
            totalAbsoluteChannelDelta = $totalAbsoluteChannelDelta
            meanChangedChannelDelta = [Math]::Round($meanChangedChannelDelta, 3)
            maxChannelDelta = $maxChannelDelta
        }
    } finally {
        $secondImage.Dispose()
        $firstImage.Dispose()
    }
}

function Add-RoiDifferenceCheck {
    param(
        [Parameter(Mandatory = $true)]$ThemeResult,
        [Parameter(Mandatory = $true)][string]$FirstState,
        [Parameter(Mandatory = $true)][string]$SecondState,
        [Parameter(Mandatory = $true)][string]$FirstRoiName,
        [string]$SecondRoiName = '',
        [int]$MinChangedPixels = 4,
        [double]$MinChangedRatio = 0.0005,
        [int]$MinMaxChannelDelta = 8,
        [double]$MinMeanChangedChannelDelta = 2.0
    )

    if ([string]::IsNullOrWhiteSpace($SecondRoiName)) { $SecondRoiName = $FirstRoiName }
    $firstStateResult = Get-State -ThemeResult $ThemeResult -Name $FirstState
    $secondStateResult = Get-State -ThemeResult $ThemeResult -Name $SecondState
    $first = Get-Roi -ThemeResult $ThemeResult -StateName $FirstState -RoiName $FirstRoiName
    $second = Get-Roi -ThemeResult $ThemeResult -StateName $SecondState -RoiName $SecondRoiName
    $difference = Measure-RoiDifference `
        -FirstPath $firstStateResult.path -FirstDesignRect $roiDefinitions[$FirstRoiName] `
        -SecondPath $secondStateResult.path -SecondDesignRect $roiDefinitions[$SecondRoiName]
    $requiredChangedPixels = [Math]::Max(
        $MinChangedPixels, [int][Math]::Ceiling($difference.pixelCount * $MinChangedRatio))
    $different =
        $difference.changedPixels -ge $requiredChangedPixels -and
        $difference.maxChannelDelta -ge $MinMaxChannelDelta -and
        $difference.meanChangedChannelDelta -ge $MinMeanChangedChannelDelta
    [void]$checks.Add([pscustomobject]@{
        theme = $ThemeResult.theme
        firstState = $FirstState
        secondState = $SecondState
        firstRoi = $FirstRoiName
        secondRoi = $SecondRoiName
        firstSha256 = $first.sha256
        secondSha256 = $second.sha256
        requiredChangedPixels = $requiredChangedPixels
        minMaxChannelDelta = $MinMaxChannelDelta
        minMeanChangedChannelDelta = $MinMeanChangedChannelDelta
        difference = $difference
        different = [bool]$different
    })
    if (-not $different) {
        [void]$failures.Add(
            "$($ThemeResult.theme) $FirstRoiName/$SecondRoiName did not visibly change: " +
            "$FirstState -> $SecondState changed=$($difference.changedPixels)/$requiredChangedPixels " +
            "maxDelta=$($difference.maxChannelDelta)/$MinMaxChannelDelta " +
            "meanChangedDelta=$($difference.meanChangedChannelDelta)/$MinMeanChangedChannelDelta")
    }
}

foreach ($themeResult in @($dark, $light)) {
    Add-RoiDifferenceCheck $themeResult 'normal' 'slider-focus-checkbox-hover' 'slider' `
        -MinChangedPixels 12
    Add-RoiDifferenceCheck $themeResult 'slider-focus-checkbox-hover' 'slider-drag' 'slider' `
        -MinChangedPixels 12
    Add-RoiDifferenceCheck $themeResult 'normal' 'slider-focus-checkbox-hover' 'checkbox'
    Add-RoiDifferenceCheck $themeResult 'normal' 'text-focus-radio-hover' 'textEdit'
    Add-RoiDifferenceCheck $themeResult 'text-focus-hover' 'text-press' 'textEdit' `
        -MinChangedPixels 16
    Add-RoiDifferenceCheck $themeResult 'normal' 'text-focus-radio-hover' 'radio'
    Add-RoiDifferenceCheck $themeResult 'normal' 'list-focus-tree-hover' 'listSelected' `
        -MinChangedPixels 16 -MinChangedRatio 0.001
    Add-RoiDifferenceCheck $themeResult 'list-focus-hover' 'list-press' 'listSelected' `
        -MinChangedPixels 16 -MinChangedRatio 0.001
    Add-RoiDifferenceCheck $themeResult 'normal' 'list-focus-tree-hover' 'treeSelected' `
        -MinChangedPixels 16 -MinChangedRatio 0.001
    Add-RoiDifferenceCheck $themeResult 'tree-focus-hover' 'tree-press' 'treeSelected' `
        -MinChangedPixels 16 -MinChangedRatio 0.001
    Add-RoiDifferenceCheck $themeResult 'normal' 'normal' 'disabledButtonChrome' `
        -SecondRoiName 'enabledButtonChrome' -MinChangedPixels 64 -MinChangedRatio 0.02
}

if ($dark.clientWidth -ne $light.clientWidth -or $dark.clientHeight -ne $light.clientHeight) {
    [void]$failures.Add(
        "Dark/Light client dimensions differ: $($dark.clientWidth)x$($dark.clientHeight) vs " +
        "$($light.clientWidth)x$($light.clientHeight)")
}
$darkNormal = Get-State -ThemeResult $dark -Name 'normal'
$lightNormal = Get-State -ThemeResult $light -Name 'normal'
if ($darkNormal.sha256 -eq $lightNormal.sha256) {
    [void]$failures.Add('Dark and Light normal client captures are identical')
}
$darkDisabled = Get-Roi -ThemeResult $dark -StateName 'normal' -RoiName 'disabledButton'
$lightDisabled = Get-Roi -ThemeResult $light -StateName 'normal' -RoiName 'disabledButton'
if ($darkDisabled.sha256 -eq $lightDisabled.sha256) {
    [void]$failures.Add('Dark and Light disabled button ROIs are identical')
}

$report = [ordered]@{
    schema = 1
    gate = 'ui-state-feedback-differential'
    head = (git rev-parse HEAD 2>$null)
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    sourceRoot = $SourceRoot
    exe = $Exe
    exeSha256 = $exeSha256
    exeLastWriteUtc = $exeInfo.LastWriteTimeUtc.ToString('o')
    buildPreset = $BuildPreset
    buildPerformed = $buildPerformed
    cmakeCache = $cachePath
    cmakeGenerator = ($generatorLine -split '=', 2)[1]
    designExtent = @($designWidth, $designHeight)
    runRoot = $runRoot
    dark = $dark
    light = $light
    checks = @($checks | ForEach-Object { $_ })
    darkLightNormalDifferent = $darkNormal.sha256 -ne $lightNormal.sha256
    darkLightDisabledDifferent = $darkDisabled.sha256 -ne $lightDisabled.sha256
    limitations = @(
        'Same-host/backend differential evidence; not a cross-GPU exact golden',
        'Pointer input is injected through Win32 and consumed by the product GLFW/Runtime/UI route',
        'State ROI checks require non-trivial changed-pixel count and channel-delta thresholds',
        'Unit and Runtime UI tests remain authoritative for stale-state cleanup and atomic commit failure paths'
    )
    failures = @($failures | ForEach-Object { [string]$_ })
    ok = $failures.Count -eq 0
}

$reportPath = Join-Path $runRoot 'report.json'
($report | ConvertTo-Json -Depth 14) | Set-Content -LiteralPath $reportPath -Encoding utf8
if (-not [string]::IsNullOrWhiteSpace($OutJson)) {
    $outJsonPath = if ([IO.Path]::IsPathRooted($OutJson)) { $OutJson } else { Join-Path $SourceRoot $OutJson }
    $outJsonParent = Split-Path -Parent $outJsonPath
    if ($outJsonParent -and -not (Test-Path -LiteralPath $outJsonParent)) {
        New-Item -ItemType Directory -Path $outJsonParent -Force | Out-Null
    }
    ($report | ConvertTo-Json -Depth 14) | Set-Content -LiteralPath $outJsonPath -Encoding utf8
}

Write-Output ([pscustomobject]@{
    schema = $report.schema
    gate = $report.gate
    runRoot = $runRoot
    checks = $checks.Count
    failures = @($failures | ForEach-Object { [string]$_ })
    darkNormal = $darkNormal.sha256
    lightNormal = $lightNormal.sha256
    ok = $report.ok
} | ConvertTo-Json -Depth 5 -Compress)

if ($failures.Count -ne 0) {
    throw "UI-STATE-FEEDBACK visual gate failed: $($failures -join '; ')"
}
Write-Host "UI-STATE-FEEDBACK visual gate ok checks=$($checks.Count) report=$reportPath"
exit 0
