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
    [int]$WindowReadyTimeoutMs = 45000,
    [int]$ProcessExitTimeoutMs = 10000,
    [switch]$SkipBuild,
    [string]$OutJson = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

if ($WarmupMs -lt 0 -or $InputSettleMs -lt 0 -or $CaptureIntervalMs -lt 1) {
    throw 'WarmupMs/InputSettleMs must be non-negative and CaptureIntervalMs must be positive'
}
if ($CaptureAttempts -lt 2) {
    throw 'CaptureAttempts must be at least two for repeatability evidence'
}
if ($WindowReadyTimeoutMs -lt 1000 -or $ProcessExitTimeoutMs -lt 1000) {
    throw 'WindowReadyTimeoutMs and ProcessExitTimeoutMs must be at least 1000'
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
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref Point point);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool SetWindowPos(
        IntPtr hWnd, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
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
    public const uint MouseWheel = 0x0800;
    public const uint WindowClose = 0x0010;
    public const int ShowRestore = 9;
    public const uint WindowPosNoSize = 0x0001;
    public const uint WindowPosNoMove = 0x0002;
    public const uint WindowPosShowWindow = 0x0040;
    public static readonly IntPtr TopMost = new IntPtr(-1);

    public static void SendMouseWheel(int delta) {
        mouse_event(MouseWheel, 0, 0, unchecked((uint)delta), UIntPtr.Zero);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct Point { public int X, Y; }
}
'@
}

$designWidth = 1280
$designHeight = 800
$neutralPoint = @(150, 700)
$componentScrollPoint = @(900, 400)
$pointerInterferenceTolerancePx = 2
$script:expectedPointerScreen = $null

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

    if (-not [TinaUiStateFeedbackWin32]::IsWindow($Hwnd)) {
        throw "showcase HWND is no longer valid: $Hwnd"
    }
    if ([TinaUiStateFeedbackWin32]::IsIconic($Hwnd)) {
        [void][TinaUiStateFeedbackWin32]::ShowWindow(
            $Hwnd, [TinaUiStateFeedbackWin32]::ShowRestore)
        Promote-InputWindow -Hwnd $Hwnd
    }
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

function New-UiaControlGeometry {
    param(
        [Parameter(Mandatory = $true)]$Current,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)]$ClientOrigin,
        [Parameter(Mandatory = $true)]$Spec
    )

    $controlTypeId = [int]$Spec.controlTypeId
    $name = [string]$Spec.name
    $identity = if ([string]::IsNullOrWhiteSpace($name)) {
        "type=$controlTypeId"
    } else {
        "type=$controlTypeId name='$name'"
    }
    $bounds = $Current.BoundingRectangle
    if ($bounds.Width -le 0.0 -or $bounds.Height -le 0.0 -or
        [double]::IsNaN($bounds.Left) -or [double]::IsNaN($bounds.Top) -or
        [double]::IsInfinity($bounds.Left) -or [double]::IsInfinity($bounds.Top)) {
        throw "UIA control has invalid bounds: $identity bounds=$bounds"
    }
    $scaleX = $designWidth / [double]$Extent.width
    $scaleY = $designHeight / [double]$Extent.height
    $left = ($bounds.Left - $ClientOrigin.X) * $scaleX
    $top = ($bounds.Top - $ClientOrigin.Y) * $scaleY
    $right = ($bounds.Right - $ClientOrigin.X) * $scaleX
    $bottom = ($bounds.Bottom - $ClientOrigin.Y) * $scaleY
    $inputX = $left + (($right - $left) * [double]$Spec.inputFractionX)
    $inputY = $top + (($bottom - $top) * [double]$Spec.inputFractionY)
    $roiPadding = [double]$Spec.roiPadding
    $roi = @(
        [Math]::Max(0.0, $left - $roiPadding),
        [Math]::Max(0.0, $top - $roiPadding),
        [Math]::Min([double]$designWidth, $right + $roiPadding),
        [Math]::Min([double]$designHeight, $bottom + $roiPadding))
    return [pscustomobject]@{
        automationId = [string]$Current.AutomationId
        controlTypeId = $controlTypeId
        name = [string]$Current.Name
        isOffscreen = [bool]$Current.IsOffscreen
        bounds = @($left, $top, $right, $bottom)
        inputPoint = @([int][Math]::Round($inputX), [int][Math]::Round($inputY))
        roi = $roi
    }
}

function Get-UiaGeometrySet {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)][object[]]$Specs
    )

    Assert-PointerOwnership -Hwnd $Hwnd -Context 'before UIA snapshot'
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($Hwnd)
    if ($null -eq $root) {
        throw "UIA did not publish a root for $Hwnd"
    }
    $elements = $root.FindAll(
        [System.Windows.Automation.TreeScope]::Subtree,
        [System.Windows.Automation.Condition]::TrueCondition)
    $matches = [ordered]@{}
    foreach ($spec in $Specs) {
        $matches[[string]$spec.key] = New-Object System.Collections.Generic.List[object]
    }
    for ($index = 0; $index -lt $elements.Count; ++$index) {
        $current = $elements.Item($index).Current
        if ($current.FrameworkId -ne 'Tina') {
            continue
        }
        foreach ($spec in $Specs) {
            if ($current.ControlType.Id -ne [int]$spec.controlTypeId) {
                continue
            }
            if (-not [string]::IsNullOrWhiteSpace([string]$spec.name) -and
                $current.Name -ne [string]$spec.name) {
                continue
            }
            [void]$matches[[string]$spec.key].Add($current)
        }
    }
    Assert-PointerOwnership -Hwnd $Hwnd -Context 'after UIA snapshot'

    $clientOrigin = New-Object TinaUiStateFeedbackWin32+Point
    if (-not [TinaUiStateFeedbackWin32]::ClientToScreen($Hwnd, [ref]$clientOrigin)) {
        throw 'ClientToScreen failed while converting UIA bounds'
    }
    $geometries = [ordered]@{}
    foreach ($spec in $Specs) {
        $key = [string]$spec.key
        $name = [string]$spec.name
        $identity = if ([string]::IsNullOrWhiteSpace($name)) {
            "type=$([int]$spec.controlTypeId)"
        } else {
            "type=$([int]$spec.controlTypeId) name='$name'"
        }
        if ($matches[$key].Count -eq 0) {
            throw "UIA control not found: $identity"
        }
        if ($matches[$key].Count -ne 1) {
            throw "UIA control is ambiguous: $identity matches=$($matches[$key].Count)"
        }
        $geometries[$key] = New-UiaControlGeometry `
            -Current $matches[$key][0] -Extent $Extent -ClientOrigin $clientOrigin -Spec $spec
    }
    return [pscustomobject]$geometries
}

function Test-UiaGeometryVisible {
    param([Parameter(Mandatory = $true)]$Geometry)

    $visibleTop = 52.0
    $visibleBottom = $designHeight - 36.0
    return -not $Geometry.isOffscreen -and
        $Geometry.bounds[0] -ge 0.0 -and $Geometry.bounds[2] -le $designWidth -and
        $Geometry.bounds[1] -ge $visibleTop -and $Geometry.bounds[3] -le $visibleBottom
}

function Format-UiaGeometrySet {
    param([Parameter(Mandatory = $true)]$GeometrySet)

    return (($GeometrySet.PSObject.Properties | ForEach-Object {
        "$($_.Name)=id=$($_.Value.automationId),bounds=$($_.Value.bounds -join ','),offscreen=$($_.Value.isOffscreen)"
    }) -join '; ')
}

function Assert-UiaGeometrySetVisible {
    param(
        [Parameter(Mandatory = $true)]$GeometrySet,
        [Parameter(Mandatory = $true)][string]$Region
    )

    $invalid = New-Object System.Collections.Generic.List[string]
    foreach ($property in $GeometrySet.PSObject.Properties) {
        if (-not (Test-UiaGeometryVisible -Geometry $property.Value)) {
            [void]$invalid.Add(
                "$($property.Name)=bounds=$($property.Value.bounds -join ',') offscreen=$($property.Value.isOffscreen)")
        }
    }
    if ($invalid.Count -ne 0) {
        throw "$Region UIA controls are not fully visible: $($invalid -join '; ')"
    }
}

function Wait-UiaGeometrySetVisibleWithWheel {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)][object[]]$Specs,
        [Parameter(Mandatory = $true)][string]$Region,
        [int]$MaximumWheelSteps = 48
    )

    $lastDetail = '<not resolved>'
    $wheelSteps = 0
    $lastWheelDirection = 0
    while ($wheelSteps -le $MaximumWheelSteps) {
        try {
            $geometries = Get-UiaGeometrySet -Hwnd $Hwnd -Extent $Extent -Specs $Specs
            $details = New-Object System.Collections.Generic.List[string]
            $allVisible = $true
            $requiresScrollDown = $false
            $requiresScrollUp = $false
            foreach ($property in $geometries.PSObject.Properties) {
                $geometry = $property.Value
                [void]$details.Add(
                    "$($property.Name)=bounds=$($geometry.bounds -join ',') offscreen=$($geometry.isOffscreen)")
                if (-not (Test-UiaGeometryVisible -Geometry $geometry)) {
                    $allVisible = $false
                    if ($geometry.bounds[3] -gt ($designHeight - 36.0)) {
                        $requiresScrollDown = $true
                    } elseif ($geometry.bounds[1] -lt 52.0) {
                        $requiresScrollUp = $true
                    } elseif ($geometry.isOffscreen) {
                        $requiresScrollDown = $true
                    }
                }
            }
            $lastDetail = $details -join '; '
            if ($allVisible) {
                Write-Host "uiaGeometry region=$Region $(Format-UiaGeometrySet -GeometrySet $geometries)"
                return $geometries
            }
            if ($requiresScrollDown -and $requiresScrollUp) {
                $lastDetail = "$Region controls cannot fit in the Component Canvas viewport: $lastDetail"
                break
            }
            $wheelDelta = if ($requiresScrollUp) { 120 } else { -120 }
        } catch {
            if ($_.Exception.Message -match '^external (pointer|desktop) interference') {
                throw
            }
            $lastDetail = $_.Exception.Message
            $wheelDelta = -120
        }
        $remainingSteps = $MaximumWheelSteps - $wheelSteps
        if ($remainingSteps -le 0) {
            break
        }
        $wheelDirection = [Math]::Sign($wheelDelta)
        $batchLimit = if ($lastWheelDirection -ne 0 -and
            $wheelDirection -ne $lastWheelDirection) { 1 } else { 3 }
        $batchSize = [Math]::Min($batchLimit, $remainingSteps)
        Move-DesignPointer -Hwnd $Hwnd -Extent $Extent `
            -DesignX $componentScrollPoint[0] -DesignY $componentScrollPoint[1]
        for ($batchIndex = 0; $batchIndex -lt $batchSize; ++$batchIndex) {
            Assert-InputTarget -Hwnd $Hwnd -Context "$Region wheel batch"
            [TinaUiStateFeedbackWin32]::SendMouseWheel($wheelDelta)
            if ($batchIndex + 1 -lt $batchSize) {
                Start-Sleep -Milliseconds 20
            }
        }
        $wheelSteps += $batchSize
        $lastWheelDirection = $wheelDirection
        Start-Sleep -Milliseconds ([Math]::Max(90, $InputSettleMs))
    }
    throw "$Region controls did not become visible after $MaximumWheelSteps wheel steps: $lastDetail"
}

function Promote-InputWindow {
    param([Parameter(Mandatory = $true)][IntPtr]$Hwnd)

    $flags = [TinaUiStateFeedbackWin32]::WindowPosNoMove -bor
        [TinaUiStateFeedbackWin32]::WindowPosNoSize -bor
        [TinaUiStateFeedbackWin32]::WindowPosShowWindow
    if (-not [TinaUiStateFeedbackWin32]::SetWindowPos(
            $Hwnd, [TinaUiStateFeedbackWin32]::TopMost, 0, 0, 0, 0, $flags)) {
        throw "SetWindowPos(HWND_TOPMOST) failed for $Hwnd"
    }
    [void][TinaUiStateFeedbackWin32]::BringWindowToTop($Hwnd)
    if (-not [TinaUiStateFeedbackWin32]::SetForegroundWindow($Hwnd)) {
        throw "input window could not become the foreground window: $Hwnd"
    }
    Start-Sleep -Milliseconds 30
}

function Assert-PointerOwnership {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [string]$Context = 'pointer operation'
    )

    if ($null -eq $script:expectedPointerScreen) {
        throw "pointer ownership was not initialized: context=$Context"
    }
    if ($script:expectedPointerScreen.hwnd -ne $Hwnd) {
        throw "pointer ownership belongs to another window: context=$Context " +
            "expected=$($script:expectedPointerScreen.hwnd) actual=$Hwnd"
    }
    $foreground = [TinaUiStateFeedbackWin32]::GetForegroundWindow()
    if ($foreground -ne $Hwnd -or [TinaUiStateFeedbackWin32]::IsIconic($Hwnd)) {
        throw "external desktop interference: context=$Context expectedForeground=$Hwnd " +
            "actualForeground=$foreground iconic=$([TinaUiStateFeedbackWin32]::IsIconic($Hwnd))"
    }
    $cursor = New-Object TinaUiStateFeedbackWin32+Point
    if (-not [TinaUiStateFeedbackWin32]::GetCursorPos([ref]$cursor)) {
        throw 'GetCursorPos failed while validating pointer ownership'
    }
    $deltaX = [Math]::Abs($cursor.X - [int]$script:expectedPointerScreen.x)
    $deltaY = [Math]::Abs($cursor.Y - [int]$script:expectedPointerScreen.y)
    if ($deltaX -gt $pointerInterferenceTolerancePx -or
        $deltaY -gt $pointerInterferenceTolerancePx) {
        throw "external pointer interference: context=$Context " +
            "expected=$($script:expectedPointerScreen.x),$($script:expectedPointerScreen.y) " +
            "actual=$($cursor.X),$($cursor.Y) delta=${deltaX},${deltaY}"
    }
    $target = [TinaUiStateFeedbackWin32]::WindowFromPoint($cursor)
    if ($target -ne $Hwnd) {
        throw "external pointer interference: context=$Context expectedTarget=$Hwnd actualTarget=$target " +
            "screen=$($cursor.X),$($cursor.Y)"
    }
}

function Move-DesignPointer {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [Parameter(Mandatory = $true)]$Extent,
        [Parameter(Mandatory = $true)][int]$DesignX,
        [Parameter(Mandatory = $true)][int]$DesignY
    )

    if ($null -ne $script:expectedPointerScreen) {
        Assert-PointerOwnership -Hwnd $Hwnd -Context 'before scripted pointer move'
    } else {
        if ([TinaUiStateFeedbackWin32]::IsIconic($Hwnd) -or
            [TinaUiStateFeedbackWin32]::GetForegroundWindow() -ne $Hwnd) {
            [void][TinaUiStateFeedbackWin32]::ShowWindow(
                $Hwnd, [TinaUiStateFeedbackWin32]::ShowRestore)
            Promote-InputWindow -Hwnd $Hwnd
        }
    }
    $screen = Convert-DesignPointToScreen -Hwnd $Hwnd -Extent $Extent -DesignX $DesignX -DesignY $DesignY
    if (-not [TinaUiStateFeedbackWin32]::SetCursorPos($screen.X, $screen.Y)) {
        throw 'SetCursorPos failed'
    }
    $script:expectedPointerScreen = [pscustomobject]@{
        hwnd = $Hwnd
        x = [int]$screen.X
        y = [int]$screen.Y
        designX = $DesignX
        designY = $DesignY
    }
    Assert-PointerOwnership -Hwnd $Hwnd -Context 'after scripted pointer move'
}

function Assert-InputTarget {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Hwnd,
        [string]$Context = 'before pointer input'
    )

    Assert-PointerOwnership -Hwnd $Hwnd -Context $Context
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
        Assert-InputTarget -Hwnd $Hwnd -Context 'before primary down'
        $primaryHeld = $true
        Send-PrimaryDown
        Start-Sleep -Milliseconds 45
        Assert-InputTarget -Hwnd $Hwnd -Context 'before primary up'
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
    Assert-InputTarget -Hwnd $Hwnd -Context 'before primary hold'
    $Held.Value = $true
    Send-PrimaryDown
    Start-Sleep -Milliseconds $InputSettleMs
    Assert-InputTarget -Hwnd $Hwnd -Context 'after primary hold settle'
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
            designRect = @($DesignRect | ForEach-Object { [double]$_ })
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
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$RoiDefinitions
    )

    $lastUsefulHash = $null
    $consecutiveUseful = 0
    $attempts = New-Object System.Collections.Generic.List[object]
    for ($attempt = 1; $attempt -le $CaptureAttempts; ++$attempt) {
        Assert-PointerOwnership -Hwnd $Hwnd -Context "before $Name capture $attempt"
        $path = Join-Path $ThemeDirectory ('{0}-{1:D2}.png' -f $Name, $attempt)
        $method = Capture-Client -Hwnd $Hwnd -Path $path
        Assert-PointerOwnership -Hwnd $Hwnd -Context "after $Name capture $attempt"
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
                foreach ($roiName in $RoiDefinitions.Keys) {
                    $roiResults[$roiName] = Get-RoiFingerprint `
                        -Path $path -Name $roiName -DesignRect $RoiDefinitions[$roiName]
                }
                Assert-PointerOwnership -Hwnd $Hwnd -Context "after $Name ROI fingerprints"
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
    if ([string]$evidence.initialDensity -ne 'comfortable' -or
        [string]$evidence.finalDensity -ne 'comfortable') {
        [void]$errors.Add(
            "density expected=comfortable/comfortable actual=$($evidence.initialDensity)/$($evidence.finalDensity)")
    }
    if ([bool]$evidence.autoDemo) { [void]$errors.Add('autoDemo must be false') }
    if ([int64]$evidence.themeSwitches -ne 0) { [void]$errors.Add('themeSwitches must be zero') }
    if ([int64]$evidence.densitySwitchRequests -ne 0 -or [int64]$evidence.densityRebuilds -ne 0) {
        [void]$errors.Add('density switches and rebuilds must be zero')
    }
    if ([int64]$evidence.controls -ne 24) {
        [void]$errors.Add("controls expected=24 actual=$($evidence.controls)")
    }
    if ([int64]$evidence.imageProducts -ne 5 -or
        [int64]$evidence.asymmetricCornerProducts -ne 3) {
        [void]$errors.Add(
            "image products expected=5/3 actual=$($evidence.imageProducts)/$($evidence.asymmetricCornerProducts)")
    }
    if ([int64]$evidence.componentProfiles -ne 3 -or [int64]$evidence.workbenchBands -ne 5) {
        [void]$errors.Add(
            "workbench structure expected=3/5 actual=$($evidence.componentProfiles)/$($evidence.workbenchBands)")
    }
    if (-not [bool]$evidence.desktopWorkbench -or [bool]$evidence.dialogOpen -or
        -not [bool]$evidence.stylesheetInstalled) {
        [void]$errors.Add('desktopWorkbench/stylesheetInstalled must be true and dialogOpen must be false')
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

    $script:expectedPointerScreen = $null
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
        $windowDeadline = (Get-Date).AddMilliseconds($WindowReadyTimeoutMs)
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
            if ($process.HasExited) {
                $process.WaitForExit()
                $process.Refresh()
                $stderr = if (Test-Path -LiteralPath $stderrPath) {
                    (Get-Content -LiteralPath $stderrPath -Encoding utf8 -Raw).Trim()
                } else { '' }
                $detail = if ([string]::IsNullOrWhiteSpace($stderr)) { '<empty>' } else { $stderr }
                throw "$Theme showcase exited before publishing a visible window exit=$($process.ExitCode) stderr=$detail"
            }
            throw "$Theme showcase did not publish a visible window"
        }

        [void][TinaUiStateFeedbackWin32]::ShowWindow(
            $hwnd, [TinaUiStateFeedbackWin32]::ShowRestore)
        Promote-InputWindow -Hwnd $hwnd
        Write-Host "inputWindowIsolation=HWND_TOPMOST hwnd=$hwnd"
        $extent = Get-ClientExtent -Hwnd $hwnd
        $aspect = $extent.width / [double]$extent.height
        $expectedAspect = $designWidth / [double]$designHeight
        if ([Math]::Abs($aspect - $expectedAspect) -gt 0.02) {
            throw "$Theme showcase client aspect mismatch actual=$aspect expected=$expectedAspect"
        }

        $initialSpecs = @(
            [pscustomobject]@{
                key = 'slider'; controlTypeId = 50015; name = ''
                inputFractionX = 0.72; inputFractionY = 0.5; roiPadding = 4.0
            },
            [pscustomobject]@{
                key = 'checkbox'; controlTypeId = 50002; name = ''
                inputFractionX = 0.5; inputFractionY = 0.5; roiPadding = 4.0
            },
            [pscustomobject]@{
                key = 'disabledButton'; controlTypeId = 50000; name = 'Disabled'
                inputFractionX = 0.5; inputFractionY = 0.5; roiPadding = 3.0
            },
            [pscustomobject]@{
                key = 'enabledButton'; controlTypeId = 50000; name = 'Reset state'
                inputFractionX = 0.5; inputFractionY = 0.5; roiPadding = 3.0
            }
        )
        $formSpecs = @(
            [pscustomobject]@{
                key = 'textEdit'; controlTypeId = 50004; name = 'Profile name'
                inputFractionX = 0.35; inputFractionY = 0.5; roiPadding = 4.0
            },
            [pscustomobject]@{
                key = 'radio'; controlTypeId = 50013; name = 'Balanced'
                inputFractionX = 0.5; inputFractionY = 0.5; roiPadding = 4.0
            }
        )
        $collectionSpecs = @(
            [pscustomobject]@{
                key = 'listSelected'; controlTypeId = 50007; name = 'Asset browser'
                inputFractionX = 0.6; inputFractionY = 0.5; roiPadding = 2.0
            },
            [pscustomobject]@{
                key = 'treeSelected'; controlTypeId = 50024; name = 'World'
                inputFractionX = 0.65; inputFractionY = 0.5; roiPadding = 2.0
            }
        )

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $neutralPoint[0] -DesignY $neutralPoint[1]
        Start-Sleep -Milliseconds $WarmupMs
        $initialGeometry = Get-UiaGeometrySet `
            -Hwnd $hwnd -Extent $extent -Specs $initialSpecs
        Assert-UiaGeometrySetVisible -GeometrySet $initialGeometry -Region 'initial'
        Write-Host "uiaGeometry region=initial $(Format-UiaGeometrySet -GeometrySet $initialGeometry)"
        $initialRois = [ordered]@{
            slider = $initialGeometry.slider.roi
            checkbox = $initialGeometry.checkbox.roi
            disabledButton = $initialGeometry.disabledButton.roi
            disabledButtonChrome = $initialGeometry.disabledButton.roi
            enabledButtonChrome = $initialGeometry.enabledButton.roi
        }
        $states['normal'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'normal' `
            -RoiDefinitions $initialRois

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $initialGeometry.slider.inputPoint `
            -HoverPoint $initialGeometry.checkbox.inputPoint
        $states['slider-focus-checkbox-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'slider-focus-checkbox-hover' `
            -RoiDefinitions $initialRois

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent `
            -Point $initialGeometry.slider.inputPoint `
            -Held ([ref]$primaryHeld)
        try {
            $states['slider-drag'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'slider-drag' `
                -RoiDefinitions $initialRois
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        $formGeometry = Wait-UiaGeometrySetVisibleWithWheel `
            -Hwnd $hwnd -Extent $extent -Specs $formSpecs -Region 'forms'
        $formRois = [ordered]@{
            textEdit = $formGeometry.textEdit.roi
            radio = $formGeometry.radio.roi
        }
        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $neutralPoint[0] -DesignY $neutralPoint[1]
        Start-Sleep -Milliseconds $InputSettleMs
        $states['forms-normal'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'forms-normal' `
            -RoiDefinitions $formRois

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $formGeometry.textEdit.inputPoint `
            -HoverPoint $formGeometry.radio.inputPoint
        $states['text-focus-radio-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'text-focus-radio-hover' `
            -RoiDefinitions $formRois

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $formGeometry.textEdit.inputPoint[0] `
            -DesignY $formGeometry.textEdit.inputPoint[1]
        Start-Sleep -Milliseconds $InputSettleMs
        $states['text-focus-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'text-focus-hover' `
            -RoiDefinitions $formRois

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent `
            -Point $formGeometry.textEdit.inputPoint `
            -Held ([ref]$primaryHeld)
        try {
            $states['text-press'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'text-press' `
                -RoiDefinitions $formRois
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        $collectionGeometry = Wait-UiaGeometrySetVisibleWithWheel `
            -Hwnd $hwnd -Extent $extent -Specs $collectionSpecs -Region 'collections'
        $collectionRois = [ordered]@{
            listSelected = $collectionGeometry.listSelected.roi
            treeSelected = $collectionGeometry.treeSelected.roi
        }
        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $neutralPoint[0] -DesignY $neutralPoint[1]
        Start-Sleep -Milliseconds $InputSettleMs
        $states['collections-normal'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'collections-normal' `
            -RoiDefinitions $collectionRois

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $collectionGeometry.listSelected.inputPoint `
            -HoverPoint $collectionGeometry.treeSelected.inputPoint
        $states['list-focus-tree-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'list-focus-tree-hover' `
            -RoiDefinitions $collectionRois

        Move-DesignPointer -Hwnd $hwnd -Extent $extent `
            -DesignX $collectionGeometry.listSelected.inputPoint[0] `
            -DesignY $collectionGeometry.listSelected.inputPoint[1]
        Start-Sleep -Milliseconds $InputSettleMs
        $states['list-focus-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'list-focus-hover' `
            -RoiDefinitions $collectionRois

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent `
            -Point $collectionGeometry.listSelected.inputPoint `
            -Held ([ref]$primaryHeld)
        try {
            $states['list-press'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'list-press' `
                -RoiDefinitions $collectionRois
        } finally {
            if ($primaryHeld) {
                Send-PrimaryUp
                $primaryHeld = $false
                Start-Sleep -Milliseconds $InputSettleMs
            }
        }

        Invoke-ClickAndHover -Hwnd $hwnd -Extent $extent `
            -ClickPoint $collectionGeometry.treeSelected.inputPoint `
            -HoverPoint $collectionGeometry.treeSelected.inputPoint
        $states['tree-focus-hover'] = Capture-StableState `
            -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'tree-focus-hover' `
            -RoiDefinitions $collectionRois

        Start-PrimaryHold -Hwnd $hwnd -Extent $extent `
            -Point $collectionGeometry.treeSelected.inputPoint `
            -Held ([ref]$primaryHeld)
        try {
            $states['tree-press'] = Capture-StableState `
                -Hwnd $hwnd -ThemeDirectory $themeDirectory -Name 'tree-press' `
                -RoiDefinitions $collectionRois
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
            geometry = [pscustomobject]@{
                initial = $initialGeometry
                forms = $formGeometry
                collections = $collectionGeometry
            }
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
        $script:expectedPointerScreen = $null
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
        -FirstPath $firstStateResult.path -FirstDesignRect $first.designRect `
        -SecondPath $secondStateResult.path -SecondDesignRect $second.designRect
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
    Add-RoiDifferenceCheck $themeResult 'forms-normal' 'text-focus-radio-hover' 'textEdit'
    Add-RoiDifferenceCheck $themeResult 'text-focus-hover' 'text-press' 'textEdit' `
        -MinChangedPixels 16
    Add-RoiDifferenceCheck $themeResult 'forms-normal' 'text-focus-radio-hover' 'radio'
    Add-RoiDifferenceCheck $themeResult 'collections-normal' 'list-focus-tree-hover' 'listSelected' `
        -MinChangedPixels 16 -MinChangedRatio 0.001
    Add-RoiDifferenceCheck $themeResult 'list-focus-hover' 'list-press' 'listSelected' `
        -MinChangedPixels 16 -MinChangedRatio 0.001
    Add-RoiDifferenceCheck $themeResult 'collections-normal' 'list-focus-tree-hover' 'treeSelected' `
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
