<#
.SYNOPSIS
  Run the UI-002 Windows cross-process UI Automation product gate.

.DESCRIPTION
  Configures and builds the UIA-enabled bgfx + FreeType graph, runs the UIA
  GoogleTests directly, launches tina_sample_ui_showcase, and inspects its real
  HWND through the Windows UI Automation client API. The sample is closed with
  WM_CLOSE so EngineHost and UI ownership unwind normally.

  This gate proves external-client discovery, property publication, and real
  Invoke/Toggle/RangeValue/Value pattern actions. It does not claim Narrator
  interaction compliance.
#>
[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$ConfigurePreset = 'windows-msvc-vnext-bgfx-ui-freetype',
    [string]$BuildPreset = 'windows-vnext-bgfx-ui-freetype-debug',
    [string]$BinDir = '',
    [string]$OutJson = '',
    [int]$TimeoutMs = 30000,
    [switch]$SkipConfigure,
    [switch]$SkipBuild
)
#Requires -Version 5.1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

if (-not ('TinaUiaGateNative' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class TinaUiaGateNative {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr parameter);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int capacity);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
}
"@
}

function Resolve-SourceRoot {
    if (-not [string]::IsNullOrWhiteSpace($SourceRoot)) {
        return (Resolve-Path -LiteralPath $SourceRoot).Path
    }
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
    }
    return (Get-Location).Path
}

function Find-ProcessWindow([int]$ProcessId) {
    $match = @{ Hwnd = [IntPtr]::Zero; ProcessId = [uint32]$ProcessId }
    $callback = [TinaUiaGateNative+EnumWindowsProc]{
        param([IntPtr]$hwnd, [IntPtr]$parameter)
        [uint32]$owner = 0
        [void][TinaUiaGateNative]::GetWindowThreadProcessId($hwnd, [ref]$owner)
        if ($owner -ne $match.ProcessId -or -not [TinaUiaGateNative]::IsWindowVisible($hwnd)) {
            return $true
        }
        $rect = New-Object TinaUiaGateNative+Rect
        if (-not [TinaUiaGateNative]::GetClientRect($hwnd, [ref]$rect)) {
            return $true
        }
        if (($rect.Right - $rect.Left) -lt 32 -or ($rect.Bottom - $rect.Top) -lt 32) {
            return $true
        }
        $match.Hwnd = $hwnd
        return $false
    }
    [void][TinaUiaGateNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $match.Hwnd
}

function Read-WindowTitle([IntPtr]$Hwnd) {
    $buffer = New-Object System.Text.StringBuilder 512
    [void][TinaUiaGateNative]::GetWindowText($Hwnd, $buffer, $buffer.Capacity)
    return $buffer.ToString()
}

function Wait-ForWindow([System.Diagnostics.Process]$Process, [DateTime]$Deadline) {
    while ((Get-Date) -lt $Deadline -and -not $Process.HasExited) {
        try { $Process.Refresh() } catch {}
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }
        $hwnd = Find-ProcessWindow -ProcessId $Process.Id
        if ($hwnd -ne [IntPtr]::Zero) {
            return $hwnd
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

$SourceRoot = Resolve-SourceRoot
Set-Location -LiteralPath $SourceRoot

if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = Join-Path $SourceRoot 'out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug'
} elseif (-not [System.IO.Path]::IsPathRooted($BinDir)) {
    $BinDir = Join-Path $SourceRoot $BinDir
}
$BinDir = [System.IO.Path]::GetFullPath($BinDir)

if ([string]::IsNullOrWhiteSpace($OutJson)) {
    $artifactDir = Join-Path $SourceRoot 'artifacts\gates'
    [void](New-Item -ItemType Directory -Path $artifactDir -Force)
    $OutJson = Join-Path $artifactDir ('ui-002-uia-{0}.json' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
} elseif (-not [System.IO.Path]::IsPathRooted($OutJson)) {
    $OutJson = Join-Path $SourceRoot $OutJson
}
$OutJson = [System.IO.Path]::GetFullPath($OutJson)
[void](New-Item -ItemType Directory -Path (Split-Path -Parent $OutJson) -Force)

$steps = New-Object System.Collections.Generic.List[object]
function Add-Step([string]$Name, [int]$ExitCode, [string]$Detail = '') {
    [void]$steps.Add([ordered]@{
        name = $Name
        exitCode = $ExitCode
        detail = $Detail
        ok = ($ExitCode -eq 0)
    })
    if ($ExitCode -ne 0) {
        throw "step failed: $Name exit=$ExitCode $Detail"
    }
}

if (-not $SkipConfigure) {
    & cmake --preset $ConfigurePreset
    Add-Step -Name 'configure' -ExitCode $LASTEXITCODE
}
if (-not $SkipBuild) {
    & cmake --build --preset $BuildPreset --parallel 2 --target tina_ui_uia_tests tina_sample_ui_showcase -- /nr:false
    Add-Step -Name 'build' -ExitCode $LASTEXITCODE
}

$uiaTests = Join-Path $BinDir 'tina_ui_uia_tests.exe'
$showcase = Join-Path $BinDir 'tina_sample_ui_showcase.exe'
if (-not (Test-Path -LiteralPath $uiaTests -PathType Leaf)) {
    Add-Step -Name 'tina_ui_uia_tests.exe' -ExitCode 1 -Detail "missing executable: $uiaTests"
}
if (-not (Test-Path -LiteralPath $showcase -PathType Leaf)) {
    Add-Step -Name 'tina_sample_ui_showcase.exe' -ExitCode 1 -Detail "missing executable: $showcase"
}

& $uiaTests --gtest_color=yes
Add-Step -Name 'tina_ui_uia_tests.exe' -ExitCode $LASTEXITCODE

$runDir = Join-Path (Split-Path -Parent $OutJson) ('ui-002-uia-run-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
[void](New-Item -ItemType Directory -Path $runDir -Force)
$stdoutPath = Join-Path $runDir 'showcase-stdout.txt'
$stderrPath = Join-Path $runDir 'showcase-stderr.txt'
$arguments = '--frame-delay-ms=4 --theme=dark'
$process = Start-Process -FilePath $showcase -ArgumentList $arguments -WorkingDirectory $BinDir -PassThru `
    -WindowStyle Normal -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

$deadline = (Get-Date).AddMilliseconds($TimeoutMs)
$hwnd = [IntPtr]::Zero
$windowTitle = ''
$elements = $null
$tinaProviderCount = 0
$positiveBoundsCount = 0
$focusableCount = 0
$rootElementCount = 0
$frameworkElementCount = 0
$runtimeIdCount = 0
$uniqueRuntimeIdCount = 0
$uniqueAutomationIdCount = 0
$fragmentOrphanCount = 0
$fragmentIntegrityVerified = $false
$namedElements = New-Object System.Collections.Generic.List[string]
$controlTypeCounts = @{}
$textEditValue = ''
$sliderValue = $null
$checkboxToggleState = $null
$textEditValueSupported = $false
$sliderValueSupported = $false
$checkboxToggleSupported = $false
$primaryButtonElement = $null
$checkboxElement = $null
$sliderElement = $null
$textEditElement = $null
$checkboxAutomationId = ''
$sliderAutomationId = ''
$textEditAutomationId = ''
$invokePatternSupported = $false
$invokeActionIssued = $false
$actionsIssued = $false
$actionsVerified = $false
$focusActionIssued = $false
$focusVerified = $false
$primaryActionObserved = $false
$postTextEditValue = ''
$postFocusedAutomationId = ''
$postTextEditHasKeyboardFocus = $false
$postSliderValue = $null
$postCheckboxToggleState = $null
$probeError = ''
$forcedTermination = $false
$closePosted = $false
$exitCode = $null

try {
    $hwnd = Wait-ForWindow -Process $process -Deadline $deadline
    if ($hwnd -eq [IntPtr]::Zero) {
        throw "no visible showcase window for pid $($process.Id)"
    }
    $windowTitle = Read-WindowTitle -Hwnd $hwnd

    $discoveryDeadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $discoveryDeadline -and -not $process.HasExited) {
        try {
            $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
            $elements = $root.FindAll(
                [System.Windows.Automation.TreeScope]::Subtree,
                [System.Windows.Automation.Condition]::TrueCondition)
            $tinaProviderCount = 0
            $positiveBoundsCount = 0
            $focusableCount = 0
            $rootElementCount = 0
            $frameworkElementCount = 0
            $runtimeIdCount = 0
            $fragmentOrphanCount = 0
            $runtimeIds = @{}
            $automationIds = @{}
            $namedElements.Clear()
            $controlTypeCounts = @{}
            $textEditValue = ''
            $sliderValue = $null
            $checkboxToggleState = $null
            $textEditValueSupported = $false
            $sliderValueSupported = $false
            $checkboxToggleSupported = $false
            $primaryButtonElement = $null
            $checkboxElement = $null
            $sliderElement = $null
            $textEditElement = $null
            for ($index = 0; $index -lt $elements.Count; ++$index) {
                $element = $elements.Item($index)
                $current = $element.Current
                $isTinaElement = $current.AutomationId -like 'tina-ui-node-*' -or $current.Name -eq 'Tina UI Root'
                if ($isTinaElement) {
                    ++$tinaProviderCount
                    if ($current.Name -eq 'Tina UI Root') {
                        ++$rootElementCount
                    } else {
                        if (-not $automationIds.ContainsKey([string]$current.AutomationId)) {
                            $automationIds[[string]$current.AutomationId] = $true
                        }
                        $parent = [System.Windows.Automation.TreeWalker]::RawViewWalker.GetParent($element)
                        if ($null -eq $parent -or
                            ($parent.Current.Name -ne 'Tina UI Root' -and
                             $parent.Current.AutomationId -notlike 'tina-ui-node-*')) {
                            ++$fragmentOrphanCount
                        }
                    }
                    if ($current.FrameworkId -eq 'Tina') {
                        ++$frameworkElementCount
                    }
                    $runtimeId = @($element.GetRuntimeId())
                    if ($runtimeId.Count -gt 0) {
                        ++$runtimeIdCount
                        $runtimeIdKey = $runtimeId -join ':'
                        if (-not $runtimeIds.ContainsKey($runtimeIdKey)) {
                            $runtimeIds[$runtimeIdKey] = $true
                        }
                    }
                    $controlTypeKey = [string][int]$current.ControlType.Id
                    if ($controlTypeCounts.ContainsKey($controlTypeKey)) {
                        $controlTypeCounts[$controlTypeKey] = [int]$controlTypeCounts[$controlTypeKey] + 1
                    } else {
                        $controlTypeCounts[$controlTypeKey] = 1
                    }
                    $bounds = $current.BoundingRectangle
                    if ($bounds.Width -gt 0 -and $bounds.Height -gt 0) {
                        ++$positiveBoundsCount
                    }
                    if ($current.IsKeyboardFocusable) {
                        ++$focusableCount
                    }
                    if (-not [string]::IsNullOrWhiteSpace($current.Name)) {
                        [void]$namedElements.Add([string]$current.Name)
                    }
                    if ($current.ControlType.Id -eq 50000 -and $current.Name -eq 'Primary action') {
                        $primaryButtonElement = $element
                    }
                    if ($current.ControlType.Id -eq 50004 -and $current.Name -eq 'Profile name') {
                        $textEditElement = $element
                        $textEditAutomationId = [string]$current.AutomationId
                        $propertyValue = $element.GetCurrentPropertyValue(
                            [System.Windows.Automation.ValuePatternIdentifiers]::ValueProperty, $true)
                        if (-not [System.Runtime.InteropServices.Marshal]::IsComObject($propertyValue)) {
                            $textEditValue = [string]$propertyValue
                            $textEditValueSupported = $true
                        }
                    } elseif ($current.ControlType.Id -eq 50015) {
                        $sliderElement = $element
                        $sliderAutomationId = [string]$current.AutomationId
                        $propertyValue = $element.GetCurrentPropertyValue(
                            [System.Windows.Automation.RangeValuePatternIdentifiers]::ValueProperty, $true)
                        if (-not [System.Runtime.InteropServices.Marshal]::IsComObject($propertyValue)) {
                            $sliderValue = [double]$propertyValue
                            $sliderValueSupported = $true
                        }
                    } elseif ($current.ControlType.Id -eq 50002) {
                        $checkboxElement = $element
                        $checkboxAutomationId = [string]$current.AutomationId
                        $propertyValue = $element.GetCurrentPropertyValue(
                            [System.Windows.Automation.TogglePatternIdentifiers]::ToggleStateProperty, $true)
                        if (-not [System.Runtime.InteropServices.Marshal]::IsComObject($propertyValue)) {
                            $checkboxToggleState = [int]$propertyValue
                            $checkboxToggleSupported = $true
                        }
                    }
                }
            }
            $uniqueRuntimeIdCount = $runtimeIds.Count
            $uniqueAutomationIdCount = $automationIds.Count
            $fragmentIntegrityVerified = $rootElementCount -eq 1 -and
                $frameworkElementCount -eq $tinaProviderCount -and
                $runtimeIdCount -eq $tinaProviderCount -and
                $uniqueRuntimeIdCount -eq $tinaProviderCount -and
                $uniqueAutomationIdCount -eq ($tinaProviderCount - 1) -and
                $fragmentOrphanCount -eq 0
            if ($tinaProviderCount -gt 0 -and $null -ne $primaryButtonElement -and
                $null -ne $checkboxElement -and $null -ne $sliderElement -and $null -ne $textEditElement -and
                $textEditValueSupported -and $sliderValueSupported -and $checkboxToggleSupported -and
                $fragmentIntegrityVerified) {
                $probeError = ''
                break
            }
        } catch {
            $probeError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 100
    }

    $requiredTypes = [ordered]@{
        '50000' = 1 # Button
        '50002' = 1 # CheckBox
        '50003' = 1 # ComboBox
        '50004' = 1 # Edit
        '50008' = 1 # List
        '50012' = 1 # ProgressBar
        '50013' = 1 # RadioButton
        '50015' = 1 # Slider
        '50023' = 1 # Tree
    }
    $missingTypes = New-Object System.Collections.Generic.List[string]
    foreach ($entry in $requiredTypes.GetEnumerator()) {
        $actual = if ($controlTypeCounts.ContainsKey($entry.Key)) { [int]$controlTypeCounts[$entry.Key] } else { 0 }
        if ($actual -lt [int]$entry.Value) {
            [void]$missingTypes.Add("$($entry.Key):$actual/$($entry.Value)")
        }
    }
    $requiredNames = @('Primary action', 'Destructive action', 'Reset state', 'Profile name')
    $missingNames = @($requiredNames | Where-Object { -not $namedElements.Contains($_) })

    $initialTextEditValue = $textEditValue
    $initialSliderValue = $sliderValue
    $initialCheckboxToggleState = $checkboxToggleState
    if ($null -eq $primaryButtonElement -or $null -eq $checkboxElement -or
        $null -eq $sliderElement -or $null -eq $textEditElement) {
        throw 'required actionable UIA elements were not discovered'
    }

    $invokePattern = [System.Windows.Automation.InvokePattern]($primaryButtonElement.GetCurrentPattern(
        [System.Windows.Automation.InvokePattern]::Pattern))
    $invokePatternSupported = $null -ne $invokePattern
    if (-not $invokePatternSupported) {
        throw 'required UIA Invoke control pattern is unavailable'
    }

    $invokePattern.Invoke()
    $invokeActionIssued = $true

    # Verify the asynchronous Name republish before later actions replace the status text.
    $invokeDeadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $invokeDeadline -and -not $process.HasExited) {
        try {
            $latestRoot = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
            $latestElements = $latestRoot.FindAll(
                [System.Windows.Automation.TreeScope]::Subtree,
                [System.Windows.Automation.Condition]::TrueCondition)
            $checkboxElement = $null
            $sliderElement = $null
            $textEditElement = $null
            for ($index = 0; $index -lt $latestElements.Count; ++$index) {
                $element = $latestElements.Item($index)
                $current = $element.Current
                if ($current.Name -eq 'Primary action committed') {
                    $primaryActionObserved = $true
                }
                if ($current.AutomationId -eq $checkboxAutomationId) {
                    $checkboxElement = $element
                } elseif ($current.AutomationId -eq $sliderAutomationId) {
                    $sliderElement = $element
                } elseif ($current.AutomationId -eq $textEditAutomationId) {
                    $textEditElement = $element
                }
            }
            if ($primaryActionObserved -and $null -ne $checkboxElement -and
                $null -ne $sliderElement -and $null -ne $textEditElement) {
                $probeError = ''
                break
            }
        } catch {
            $probeError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $primaryActionObserved) {
        throw 'Invoke action did not publish the expected dynamic Name'
    }

    $togglePattern = [System.Windows.Automation.TogglePattern]($checkboxElement.GetCurrentPattern(
        [System.Windows.Automation.TogglePattern]::Pattern))
    $rangePattern = [System.Windows.Automation.RangeValuePattern]($sliderElement.GetCurrentPattern(
        [System.Windows.Automation.RangeValuePattern]::Pattern))
    $valuePattern = [System.Windows.Automation.ValuePattern]($textEditElement.GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern))
    if ($null -eq $togglePattern -or $null -eq $rangePattern -or $null -eq $valuePattern) {
        throw 'one or more required UIA mutation control patterns are unavailable'
    }

    $textEditElement.SetFocus()
    $focusActionIssued = $true
    $togglePattern.Toggle()
    $rangePattern.SetValue(64.0)
    $valuePattern.SetValue('UIA Player')
    $actionsIssued = $true

    $actionDeadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $actionDeadline -and -not $process.HasExited) {
        try {
            # Mutations can move keyboard focus; re-assert TextEdit each poll.
            try { $textEditElement.SetFocus() } catch {}
            Start-Sleep -Milliseconds 50
            $latestRoot = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
            $latestElements = $latestRoot.FindAll(
                [System.Windows.Automation.TreeScope]::Subtree,
                [System.Windows.Automation.Condition]::TrueCondition)
            for ($index = 0; $index -lt $latestElements.Count; ++$index) {
                $element = $latestElements.Item($index)
                $current = $element.Current
                if ($current.AutomationId -eq $textEditAutomationId) {
                    $postTextEditHasKeyboardFocus = [bool]$current.HasKeyboardFocus
                    $pattern = [System.Windows.Automation.ValuePattern]($element.GetCurrentPattern(
                        [System.Windows.Automation.ValuePattern]::Pattern))
                    $postTextEditValue = [string]$pattern.Current.Value
                    # Prefer the live element AutomationId when focus is reported on it.
                    if ($postTextEditHasKeyboardFocus) {
                        $postFocusedAutomationId = [string]$current.AutomationId
                    }
                } elseif ($current.AutomationId -eq $sliderAutomationId) {
                    $pattern = [System.Windows.Automation.RangeValuePattern]($element.GetCurrentPattern(
                        [System.Windows.Automation.RangeValuePattern]::Pattern))
                    $postSliderValue = [double]$pattern.Current.Value
                } elseif ($current.AutomationId -eq $checkboxAutomationId) {
                    $pattern = [System.Windows.Automation.TogglePattern]($element.GetCurrentPattern(
                        [System.Windows.Automation.TogglePattern]::Pattern))
                    $postCheckboxToggleState = [int]$pattern.Current.ToggleState
                }
            }
            if (-not $postTextEditHasKeyboardFocus) {
                $focusedElement = [System.Windows.Automation.AutomationElement]::FocusedElement
                $postFocusedAutomationId = if ($null -eq $focusedElement) {
                    ''
                } else {
                    [string]$focusedElement.Current.AutomationId
                }
            }
            # Authoritative product evidence for Focus action: TextEdit HasKeyboardFocus
            # after SetFocus. Global FocusedElement.AutomationId is diagnostic only.
            $focusVerified = [bool]$postTextEditHasKeyboardFocus
            $actionsVerified = $postTextEditValue -eq 'UIA Player' -and
                $null -ne $postSliderValue -and [Math]::Abs([double]$postSliderValue - 64.0) -lt 0.001 -and
                $null -ne $postCheckboxToggleState -and $postCheckboxToggleState -eq 0
            if ($actionsVerified -and $focusVerified) {
                $probeError = ''
                break
            }
        } catch {
            $probeError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 100
    }

    $probeOk = $windowTitle -eq 'Tina Modern Desktop Workbench' -and
        $tinaProviderCount -ge 20 -and $positiveBoundsCount -ge 20 -and $focusableCount -ge 8 -and
        $fragmentIntegrityVerified -and
        $missingTypes.Count -eq 0 -and $missingNames.Count -eq 0 -and $initialTextEditValue -eq 'Tina Player' -and
        $textEditValueSupported -and $sliderValueSupported -and
        [Math]::Abs([double]$initialSliderValue - 72.0) -lt 0.001 -and
        $checkboxToggleSupported -and $initialCheckboxToggleState -eq 1 -and
        $invokePatternSupported -and $invokeActionIssued -and $primaryActionObserved -and
        $actionsIssued -and $actionsVerified -and $focusActionIssued -and $focusVerified
    if (-not $probeOk) {
        $detail = "providers=$tinaProviderCount bounds=$positiveBoundsCount focusable=$focusableCount " +
            "roots=$rootElementCount framework=$frameworkElementCount runtimeIds=$runtimeIdCount/$uniqueRuntimeIdCount " +
            "automationIds=$uniqueAutomationIdCount orphans=$fragmentOrphanCount fragment=$fragmentIntegrityVerified " +
            "missingTypes=$($missingTypes -join ',') missingNames=$($missingNames -join ',') " +
            "initialEdit=$initialTextEditValue/$textEditValueSupported initialSlider=$initialSliderValue/$sliderValueSupported " +
            "initialToggle=$initialCheckboxToggleState/$checkboxToggleSupported invoke=$invokePatternSupported " +
            "actions=$actionsIssued/$actionsVerified postEdit=$postTextEditValue postSlider=$postSliderValue " +
            "postToggle=$postCheckboxToggleState invokeIssued=$invokeActionIssued primaryObserved=$primaryActionObserved " +
            "focus=$focusActionIssued/$focusVerified focusedId=$postFocusedAutomationId error=$probeError"
        Add-Step -Name 'external-uia-probe' -ExitCode 1 -Detail $detail
    }
    Add-Step -Name 'external-uia-probe' -ExitCode 0 -Detail "providers=$tinaProviderCount"
} catch {
    $probeError = $_.Exception.Message
} finally {
    if ($hwnd -ne [IntPtr]::Zero -and -not $process.HasExited) {
        $closePosted = [TinaUiaGateNative]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    $closeDeadline = (Get-Date).AddSeconds(10)
    while (-not $process.HasExited -and (Get-Date) -lt $closeDeadline) {
        Start-Sleep -Milliseconds 50
        try { $process.Refresh() } catch {}
    }
    if (-not $process.HasExited) {
        $forcedTermination = $true
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    $process.WaitForExit()
    try { $process.Refresh() } catch {}
    if ($process.HasExited) {
        $exitCode = [int]$process.ExitCode
    }
}

$stdoutTail = ''
if (Test-Path -LiteralPath $stdoutPath) {
    $stdoutTail = (@(Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue) | Select-Object -Last 20) -join "`n"
}
$stderrTail = ''
if (Test-Path -LiteralPath $stderrPath) {
    $stderrTail = (@(Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue) | Select-Object -Last 20) -join "`n"
}

$showcaseSummary = $null
if (-not [string]::IsNullOrWhiteSpace($stdoutTail)) {
    $summaryLines = @($stdoutTail -split "`n")
    for ($index = $summaryLines.Count - 1; $index -ge 0; --$index) {
        try {
            $candidate = $summaryLines[$index] | ConvertFrom-Json -ErrorAction Stop
            if ($candidate.sample -eq 'tina_sample_ui_showcase') {
                $showcaseSummary = $candidate
                break
            }
        } catch {}
    }
}
$normalShutdown = $closePosted -and -not $forcedTermination -and $exitCode -eq 0 -and
    $null -ne $showcaseSummary -and $showcaseSummary.status -eq 'ok'
$invokeVerifiedOnShutdown = $normalShutdown -and [int64]$showcaseSummary.buttonActivations -ge 1
$ok = [string]::IsNullOrWhiteSpace($probeError) -and $tinaProviderCount -ge 20 -and
    $fragmentIntegrityVerified -and $primaryActionObserved -and $actionsVerified -and
    $focusVerified -and $invokeVerifiedOnShutdown -and $normalShutdown

$report = [ordered]@{
    schema = 1
    gate = 'UI-002-windows-uia-external-client'
    ok = [bool]$ok
    head = (git rev-parse HEAD 2>$null)
    configurePreset = $ConfigurePreset
    buildPreset = $BuildPreset
    binDir = $BinDir
    processId = [int]$process.Id
    hwnd = [string]$hwnd
    windowTitle = $windowTitle
    externalClient = 'System.Windows.Automation'
    elementCount = if ($null -eq $elements) { 0 } else { [int]$elements.Count }
    tinaProviderCount = [int]$tinaProviderCount
    positiveBoundsCount = [int]$positiveBoundsCount
    focusableCount = [int]$focusableCount
    rootElementCount = [int]$rootElementCount
    frameworkElementCount = [int]$frameworkElementCount
    runtimeIdCount = [int]$runtimeIdCount
    uniqueRuntimeIdCount = [int]$uniqueRuntimeIdCount
    uniqueAutomationIdCount = [int]$uniqueAutomationIdCount
    fragmentOrphanCount = [int]$fragmentOrphanCount
    fragmentIntegrityVerified = [bool]$fragmentIntegrityVerified
    controlTypeCounts = $controlTypeCounts
    namedElements = $namedElements.ToArray()
    initialTextEditValue = $initialTextEditValue
    textEditValueSupported = [bool]$textEditValueSupported
    initialSliderValue = $initialSliderValue
    sliderValueSupported = [bool]$sliderValueSupported
    initialCheckboxToggleState = $initialCheckboxToggleState
    checkboxToggleSupported = [bool]$checkboxToggleSupported
    invokePatternSupported = [bool]$invokePatternSupported
    invokeActionIssued = [bool]$invokeActionIssued
    actionsIssued = [bool]$actionsIssued
    actionsVerified = [bool]$actionsVerified
    focusActionIssued = [bool]$focusActionIssued
    focusVerified = [bool]$focusVerified
    postFocusedAutomationId = $postFocusedAutomationId
    postTextEditHasKeyboardFocus = [bool]$postTextEditHasKeyboardFocus
    primaryActionObserved = [bool]$primaryActionObserved
    invokeVerifiedOnShutdown = [bool]$invokeVerifiedOnShutdown
    postTextEditValue = $postTextEditValue
    postSliderValue = $postSliderValue
    postCheckboxToggleState = $postCheckboxToggleState
    closePosted = [bool]$closePosted
    forcedTermination = [bool]$forcedTermination
    exitCode = $exitCode
    normalShutdown = [bool]$normalShutdown
    narratorGold = $false
    error = $probeError
    steps = $steps.ToArray()
    stdoutTail = $stdoutTail
    stderrTail = $stderrTail
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutJson -Encoding utf8
Write-Host "report: $OutJson"
Write-Host "ok=$ok providers=$tinaProviderCount focusable=$focusableCount normalShutdown=$normalShutdown"
if (-not $ok) {
    exit 1
}
exit 0
