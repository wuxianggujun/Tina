#Requires -Version 5.1
<#
.SYNOPSIS
  Launch a Tina sample executable, capture its HWND client area to PNG, write report.json.

.EXAMPLE
  .\tools\windows\CaptureSampleWindow.ps1 `
    -Exe out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_desktop.exe `
    -ArgString '--frames=180 --frame-delay-ms=16' `
    -OutDir artifacts\screenshots\desktop-freetype `
    -RequireNonBlank
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$ArgString = "--frames=180 --frame-delay-ms=16",
    [string]$OutDir = "artifacts/screenshots/capture",
    [int]$WarmupMs = 900,
    [int]$CaptureCount = 3,
    [int]$CaptureIntervalMs = 400,
    [int]$TimeoutMs = 45000,
    [int]$RequiredConsecutiveUsefulCaptures = 2,
    [switch]$RequireNonBlank
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not ("TinaWinCapture" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class TinaWinCapture {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr dpiContext);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    public const uint PW_CLIENTONLY = 1;
    public const uint PW_RENDERFULLCONTENT = 2;
    public const int SW_RESTORE = 9;
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
}

function Resolve-Exe([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return (Resolve-Path -LiteralPath $path).Path }
    return (Resolve-Path -LiteralPath (Join-Path (Get-Location) $path)).Path
}

function Ensure-Dir([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    return (Resolve-Path -LiteralPath $dir).Path
}

function Find-Hwnd([int]$processId) {
    $box = @{ h = [IntPtr]::Zero; processId = $processId }
    $cb = [TinaWinCapture+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lp)
        [uint32]$p = 0
        [void][TinaWinCapture]::GetWindowThreadProcessId($hWnd, [ref]$p)
        if ($p -ne [uint32]$box.processId) { return $true }
        if (-not [TinaWinCapture]::IsWindowVisible($hWnd)) { return $true }
        $r = New-Object TinaWinCapture+RECT
        if (-not [TinaWinCapture]::GetClientRect($hWnd, [ref]$r)) { return $true }
        if (($r.Right - $r.Left) -lt 32 -or ($r.Bottom - $r.Top) -lt 32) { return $true }
        $box.h = $hWnd
        return $false
    }
    [void][TinaWinCapture]::EnumWindows($cb, [IntPtr]::Zero)
    return $box.h
}

function Capture-Hwnd([IntPtr]$hwnd, [string]$png) {
    $r = New-Object TinaWinCapture+RECT
    if (-not [TinaWinCapture]::GetClientRect($hwnd, [ref]$r)) { throw "GetClientRect failed" }
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { throw "bad size ${w}x${h}" }

    $bmp = New-Object Drawing.Bitmap $w, $h, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $used = "none"
    try {
        $hdc = $g.GetHdc()
        try {
            $clientFlags = [TinaWinCapture]::PW_CLIENTONLY -bor [TinaWinCapture]::PW_RENDERFULLCONTENT
            if ([TinaWinCapture]::PrintWindow($hwnd, $hdc, $clientFlags) -or
                [TinaWinCapture]::PrintWindow($hwnd, $hdc, [TinaWinCapture]::PW_CLIENTONLY)) {
                $used = "PrintWindow"
            }
        } finally { $g.ReleaseHdc($hdc) }

        if ($used -eq "none") {
            $pt = New-Object TinaWinCapture+POINT
            [void][TinaWinCapture]::ClientToScreen($hwnd, [ref]$pt)
            $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object Drawing.Size $w, $h))
            $used = "CopyFromScreen"
        }
        $bmp.Save($png, [Drawing.Imaging.ImageFormat]::Png)
        return $used
    } finally {
        $g.Dispose(); $bmp.Dispose()
    }
}

function Analyze-Png([string]$png) {
    $img = [Drawing.Bitmap]::FromFile($png)
    try {
        $w = $img.Width; $h = $img.Height
        $samples = 0; $sumR = 0L; $sumG = 0L; $sumB = 0L; $black = 0
        $stepX = [Math]::Max(1, [int]($w / 40)); $stepY = [Math]::Max(1, [int]($h / 40))
        $hist = @{}
        for ($y = 0; $y -lt $h; $y += $stepY) {
            for ($x = 0; $x -lt $w; $x += $stepX) {
                $c = $img.GetPixel($x, $y)
                $sumR += $c.R; $sumG += $c.G; $sumB += $c.B
                if ($c.R -lt 8 -and $c.G -lt 8 -and $c.B -lt 8) { $black++ }
                $k = "{0:X2}{1:X2}{2:X2}" -f $c.R, $c.G, $c.B
                if ($hist.ContainsKey($k)) { $hist[$k]++ } else { $hist[$k] = 1 }
                $samples++
            }
        }
        $top = $hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1
        $blackRatio = [math]::Round($black / [double]$samples, 4)
        $topRatio = [math]::Round($top.Value / [double]$samples, 4)
        $blank = ($blackRatio -ge 0.98) -or ($topRatio -ge 0.995 -and $hist.Count -le 2)
        return [ordered]@{
            path = $png; width = $w; height = $h; samples = $samples
            avgRgb = @([math]::Round($sumR / $samples, 2), [math]::Round($sumG / $samples, 2), [math]::Round($sumB / $samples, 2))
            blackRatio = $blackRatio; dominantColor = $top.Key; dominantRatio = $topRatio
            uniqueSampleColors = $hist.Count; blankLike = [bool]$blank
        }
    } finally { $img.Dispose() }
}

function Test-UsefulCapture($capture) {
    return (-not [bool]$capture.blankLike) -and ([int]$capture.uniqueSampleColors -ge 3) -and
        ([double]$capture.blackRatio -lt 0.95)
}

function Measure-ConsecutiveUsefulSameSize($captures) {
    $bestRun = 0
    $currentRun = 0
    $currentWidth = $null
    $currentHeight = $null
    $bestWidth = $null
    $bestHeight = $null
    $bestPaths = @()
    $currentPaths = @()

    foreach ($capture in $captures) {
        if (Test-UsefulCapture $capture) {
            $width = [int]$capture.width
            $height = [int]$capture.height
            if ($currentRun -gt 0 -and $currentWidth -eq $width -and $currentHeight -eq $height) {
                $currentRun += 1
                $currentPaths += [string]$capture.path
            } else {
                $currentRun = 1
                $currentWidth = $width
                $currentHeight = $height
                $currentPaths = @([string]$capture.path)
            }

            if ($currentRun -gt $bestRun) {
                $bestRun = $currentRun
                $bestWidth = $currentWidth
                $bestHeight = $currentHeight
                $bestPaths = @($currentPaths)
            }
        } else {
            $currentRun = 0
            $currentWidth = $null
            $currentHeight = $null
            $currentPaths = @()
        }
    }

    return [ordered]@{
        longestRun = [int]$bestRun
        width = $bestWidth
        height = $bestHeight
        paths = @($bestPaths)
    }
}

if ($RequiredConsecutiveUsefulCaptures -lt 1) {
    throw "RequiredConsecutiveUsefulCaptures must be >= 1"
}
if ($CaptureCount -lt $RequiredConsecutiveUsefulCaptures) {
    throw "CaptureCount must be >= RequiredConsecutiveUsefulCaptures"
}

$exePath = Resolve-Exe $Exe
$runDir = Ensure-Dir (Join-Path (Ensure-Dir $OutDir) (Get-Date -Format "yyyyMMdd-HHmmss"))
$previousDpiContext = [TinaWinCapture]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$stdoutPath = Join-Path $runDir "stdout.txt"
$stderrPath = Join-Path $runDir "stderr.txt"
Write-Host "exe: $exePath"
Write-Host "args: $ArgString"
Write-Host "out: $runDir"

# Redirect logs to files (avoid pipe buffer deadlock with verbose bgfx output).
$p = Start-Process -FilePath $exePath -ArgumentList $ArgString `
    -WorkingDirectory (Split-Path -Parent $exePath) `
    -PassThru -WindowStyle Normal `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

$deadline = (Get-Date).AddMilliseconds($TimeoutMs)
$captures = New-Object System.Collections.Generic.List[object]
$errors = New-Object System.Collections.Generic.List[string]
$method = "n/a"
$forcedTermination = $false
try {
    Start-Sleep -Milliseconds $WarmupMs
    $hwnd = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $p.MainWindowHandle; break }
        $hwnd = Find-Hwnd -processId $p.Id
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
        try { $p.Refresh() } catch {}
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw "no visible window for pid $($p.Id)" }
    Write-Host "hwnd: $hwnd"
    [void][TinaWinCapture]::ShowWindow($hwnd, [TinaWinCapture]::SW_RESTORE)
    [void][TinaWinCapture]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 150

    for ($i = 1; $i -le $CaptureCount; $i++) {
        if ($p.HasExited) { [void]$errors.Add("process exited before capture $i"); break }
        $png = Join-Path $runDir ("frame-{0:D2}.png" -f $i)
        try {
            $method = Capture-Hwnd -hwnd $hwnd -png $png
            $a = Analyze-Png -png $png
            $a.method = $method
            [void]$captures.Add($a)
            Write-Host ("captured frame-{0:D2}.png method={1} blankLike={2} avg={3}" -f $i, $method, $a.blankLike, ($a.avgRgb -join ','))
        } catch {
            [void]$errors.Add("capture $i failed: $($_.Exception.Message)")
            Write-Warning $errors[$errors.Count - 1]
        }
        if ($i -lt $CaptureCount) { Start-Sleep -Milliseconds $CaptureIntervalMs }
    }
} finally {
    # On Windows PowerShell, WaitForExit(Int32) can leave ExitCode unreadable.
    # Poll to enforce the deadline, then use only the parameterless overload to
    # synchronize the process handle and redirected streams.
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 50
        try { $p.Refresh() } catch {}
    }
    if (-not $p.HasExited) {
        $forcedTermination = $true
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    $p.WaitForExit()
    if ($previousDpiContext -ne [IntPtr]::Zero) {
        [void][TinaWinCapture]::SetThreadDpiAwarenessContext($previousDpiContext)
    }
}

try { $p.Refresh() } catch {}
$exitCode = $null
try {
    if ($p.HasExited) { $exitCode = [int]$p.ExitCode }
} catch {}

$stdoutTail = ""
$stdoutStatusOk = $false
if (Test-Path -LiteralPath $stdoutPath) {
    $lines = @(Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue)
    $stdoutTail = ($lines | Select-Object -Last 15) -join "`n"
    $stdoutStatusOk = ($stdoutTail -match '"status"\s*:\s*"ok"')
}

$blankCount = @($captures | Where-Object { $_.blankLike }).Count
$usefulCount = @($captures | Where-Object {
        Test-UsefulCapture $_
    }).Count
$consecutiveUsefulSameSize = Measure-ConsecutiveUsefulSameSize $captures
$consecutiveUsefulSameSizePassed = [int]$consecutiveUsefulSameSize.longestRun -ge $RequiredConsecutiveUsefulCaptures
$processOk = -not $forcedTermination -and $null -ne $exitCode -and $exitCode -eq 0
$ok = $processOk -and ($captures.Count -gt 0) -and ($errors.Count -eq 0) -and $consecutiveUsefulSameSizePassed
if ($RequireNonBlank -and ($blankCount -eq $captures.Count)) { $ok = $false }

$reportPath = Join-Path $runDir "report.json"
try {
    $captureArr = @()
    foreach ($c in $captures) {
        $captureArr += [pscustomobject]@{
            path = [string]$c.path
            width = [int]$c.width
            height = [int]$c.height
            samples = [int]$c.samples
            avgRgb = @([double]$c.avgRgb[0], [double]$c.avgRgb[1], [double]$c.avgRgb[2])
            blackRatio = [double]$c.blackRatio
            dominantColor = [string]$c.dominantColor
            dominantRatio = [double]$c.dominantRatio
            uniqueSampleColors = [int]$c.uniqueSampleColors
            blankLike = [bool]$c.blankLike
            usefulNonBlank = [bool](Test-UsefulCapture $c)
            method = [string]$c.method
        }
    }
    $consecutiveUsefulSameSizeGate = [pscustomobject]@{
        required = [int]$RequiredConsecutiveUsefulCaptures
        passed = [bool]$consecutiveUsefulSameSizePassed
        longestRun = [int]$consecutiveUsefulSameSize.longestRun
        width = $consecutiveUsefulSameSize.width
        height = $consecutiveUsefulSameSize.height
        paths = @($consecutiveUsefulSameSize.paths | ForEach-Object { [string]$_ })
    }
    $reportObj = [pscustomobject]@{
        ok = [bool]$ok
        exe = [string]$exePath
        args = [string]$ArgString
        processId = [int]$p.Id
        exitCode = $exitCode
        forcedTermination = [bool]$forcedTermination
        processOk = [bool]$processOk
        stdoutStatusOk = [bool]$stdoutStatusOk
        outDir = [string]$runDir
        captureCount = [int]$captures.Count
        blankLikeCount = [int]$blankCount
        usefulNonBlankCount = [int]$usefulCount
        requiredConsecutiveUsefulCaptures = [int]$RequiredConsecutiveUsefulCaptures
        consecutiveUsefulSameSizeGate = $consecutiveUsefulSameSizeGate
        captureMethod = [string]$method
        errors = @($errors | ForEach-Object { [string]$_ })
        captures = $captureArr
        stdoutTail = [string]$stdoutTail
    }
    $reportObj | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
} catch {
    "report serialization failed: $($_.Exception.Message)" | Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Warning $_.Exception.Message
}
Write-Host "report: $reportPath"
Write-Host ("ok={0} exit={1} forced={2} captures={3} useful={4} consecutiveUsefulSameSize={5}/{6} blankLike={7}" -f `
        $ok, $exitCode, $forcedTermination, $captures.Count, $usefulCount, `
        $consecutiveUsefulSameSize.longestRun, $RequiredConsecutiveUsefulCaptures, $blankCount)
if (-not $ok) { exit 1 }
exit 0
