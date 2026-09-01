#Requires -Version 5.1
<#
.SYNOPSIS
  DOC-002: scan docs for broken local links, unknown CMake presets, and suspicious targets.

.DESCRIPTION
  Scans docs/**/*.md (and AGENTS.md / README*.md at repo root). Excludes out/, build/,
  thirdparty/, dependencies/, .git/, artifacts/, logs/, temp/.

  Exit 0 if no errors; non-zero if any hard error. Warnings (soft) do not fail unless -Strict.
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [switch]$Strict,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    } else {
        $RepoRoot = (Get-Location).Path
    }
}
Set-Location -LiteralPath $RepoRoot

function Write-Info([string]$msg) {
    if (-not $Quiet) { Write-Output $msg }
}

$errors = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

function Add-Err([string]$msg) { [void]$errors.Add($msg) }
function Add-Warn([string]$msg) { [void]$warnings.Add($msg) }

# --- load presets ---
$presetsPath = Join-Path $RepoRoot 'CMakePresets.json'
if (-not (Test-Path -LiteralPath $presetsPath)) {
    Add-Err "missing CMakePresets.json"
} else {
    $presetsJson = Get-Content -LiteralPath $presetsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $configureNames = @{}
    $buildNames = @{}
    foreach ($p in @($presetsJson.configurePresets)) {
        if ($p.name) { $configureNames[$p.name] = $true }
    }
    foreach ($p in @($presetsJson.buildPresets)) {
        if ($p.name) { $buildNames[$p.name] = $true }
    }
}

# --- collect CMake target names (Tina-owned trees only) ---
$targetNames = @{}
$cmakeRoots = @('CMakeLists.txt', 'src', 'tests', 'samples', 'tools', 'cmake')
foreach ($root in $cmakeRoots) {
    $path = Join-Path $RepoRoot $root
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $files = @()
    if ((Get-Item -LiteralPath $path).PSIsContainer) {
        $files = Get-ChildItem -LiteralPath $path -Recurse -Filter 'CMakeLists.txt' -File -ErrorAction SilentlyContinue |
            Where-Object {
                $_.FullName -notmatch '[\\/](out|build|thirdparty|dependencies|\.git|artifacts|logs|temp)[\\/]'
            }
    } else {
        $files = @(Get-Item -LiteralPath $path)
    }
    foreach ($f in $files) {
        $text = Get-Content -LiteralPath $f.FullName -Raw -ErrorAction SilentlyContinue
        if (-not $text) { continue }
        foreach ($m in [regex]::Matches($text, 'add_(?:executable|library)\s*\(\s*([A-Za-z0-9_.:]+)')) {
            $name = $m.Groups[1].Value
            # strip generator expressions / aliases noise
            if ($name -match '^(tina_|Tina)') {
                $targetNames[$name] = $true
            }
        }
        foreach ($m in [regex]::Matches($text, 'add_library\s*\(\s*Tina::([A-Za-z0-9_]+)')) {
            $targetNames['Tina::' + $m.Groups[1].Value] = $true
        }
        foreach ($m in [regex]::Matches($text, 'ALIAS\s+(Tina::[A-Za-z0-9_]+)')) {
            $targetNames[$m.Groups[1].Value] = $true
        }
    }
}

# known product/test executables often referenced without full CMake parse
$knownTargets = @(
    'tina_tests', 'tina_ui_tests', 'tina_runtime_ui_tests', 'tina_ui_render_integration_tests',
    'tina_ui_freetype_tests', 'tina_platform_glfw_tests', 'tina_render_bgfx_tests',
    'tina_asset_tests', 'tina_physics2d_tests', 'tina_audio_tests', 'tina_audio_miniaudio_tests',
    'tina_network_tests', 'tina_network_tls_tests',
    'tina_sample_2d', 'tina_sample_3d', 'tina_sample_null', 'tina_sample_platform', 'tina_sample_desktop',
    'tina_sample_network',
    'tina_bootstrap_desktop', 'tina_assetc', 'tina_catalog_validate'
)
foreach ($t in $knownTargets) { $targetNames[$t] = $true }

# --- markdown files to scan ---
$mdFiles = @()
$mdFiles += Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'docs') -Recurse -Filter '*.md' -File -ErrorAction SilentlyContinue
foreach ($name in @('AGENTS.md', 'README.md', 'README_CN.md')) {
    $p = Join-Path $RepoRoot $name
    if (Test-Path -LiteralPath $p) { $mdFiles += Get-Item -LiteralPath $p }
}



$linkPattern = '\[([^\]]*)\]\(([^)]+)\)'
$presetConfigurePattern = 'cmake\s+--preset\s+([A-Za-z0-9_.:-]+)'
$presetBuildPattern = 'cmake\s+--build\s+--preset\s+([A-Za-z0-9_.:-]+)'
# backtick or plain tina_* targets
$targetPattern = '(?<![A-Za-z0-9_])(tina_[a-z0-9_]+)(?![A-Za-z0-9_])'

foreach ($file in $mdFiles) {
    $rel = $file.FullName.Substring($RepoRoot.Length).TrimStart('\', '/')
    $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
    $dir = Split-Path -Parent $file.FullName
    $content = ($lines -join "`n")
    $inFence = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $lineNo = $i + 1

        if ($line -match '^\s*(```|~~~)') {
            $inFence = -not $inFence
            continue
        }

        # local markdown links. Skipped inside fences: markdown does not render a link
        # there, and C++ lambda captures like [this](const Foo::Bar&) match the same
        # regex, which previously aborted the whole scan on the first such sample.
        $linkMatches = @()
        if (-not $inFence) { $linkMatches = [regex]::Matches($line, $linkPattern) }
        foreach ($m in $linkMatches) {
            $target = $m.Groups[2].Value.Trim()
            if ([string]::IsNullOrWhiteSpace($target)) { continue }
            # skip external / anchors-only / mailto / images with query
            if ($target -match '^(https?://|mailto:|ftp://|#)') { continue }
            if ($target.StartsWith('//')) { continue }
            # strip anchor
            $pathPart = ($target -split '#', 2)[0]
            if ([string]::IsNullOrWhiteSpace($pathPart)) { continue }
            # skip pure absolute drive paths outside repo docs convention
            if ($pathPart -match '^[A-Za-z]:') {
                Add-Warn "${rel}:${lineNo}: absolute path link '$pathPart'"
                continue
            }
            # A target that is not a legal path is reported and skipped rather than
            # thrown: an unhandled GetFullPath failure aborts every remaining file, so
            # one odd line would silently cost the whole gate its coverage.
            try {
                $resolved = [System.IO.Path]::GetFullPath((Join-Path $dir $pathPart.Replace('/', [IO.Path]::DirectorySeparatorChar)))
            } catch {
                Add-Warn "${rel}:${lineNo}: link target is not a usable path '$pathPart'"
                continue
            }
            if (-not $resolved.StartsWith($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
                Add-Warn "${rel}:${lineNo}: link escapes repo '$pathPart'"
                continue
            }
            if (-not (Test-Path -LiteralPath $resolved)) {
                Add-Err "${rel}:${lineNo}: broken local link -> $pathPart"
            }
        }

        # configure presets
        foreach ($m in [regex]::Matches($line, $presetConfigurePattern)) {
            $name = $m.Groups[1].Value
            if (-not $configureNames.ContainsKey($name) -and -not $buildNames.ContainsKey($name)) {
                # cmake --preset can be either; prefer configure, allow build name with warn
                if ($buildNames.ContainsKey($name)) {
                    # build preset used with --preset is valid for cmake --build --preset only
                } else {
                    Add-Err "${rel}:${lineNo}: unknown cmake --preset '$name'"
                }
            } elseif (-not $configureNames.ContainsKey($name) -and $buildNames.ContainsKey($name)) {
                Add-Warn "${rel}:${lineNo}: '$name' is a build preset used with cmake --preset (prefer configure name)"
            }
        }

        # build presets
        foreach ($m in [regex]::Matches($line, $presetBuildPattern)) {
            $name = $m.Groups[1].Value
            if (-not $buildNames.ContainsKey($name)) {
                if ($configureNames.ContainsKey($name)) {
                    Add-Warn "${rel}:${lineNo}: '$name' is configure preset used as --build --preset"
                } else {
                    Add-Err "${rel}:${lineNo}: unknown cmake --build --preset '$name'"
                }
            }
        }

        # target names (only backtick-wrapped or --target list style to reduce noise)
        if ($line -match '`--target\s+([^`]+)`' -or $line -match '--target\s+([A-Za-z0-9_\s-]+)') {
            $targetChunk = $Matches[1]
            foreach ($tok in ($targetChunk -split '\s+')) {
                if ($tok -match '^tina_[a-z0-9_]+$' -and -not $targetNames.ContainsKey($tok)) {
                    Add-Warn "${rel}:${lineNo}: unknown target '$tok' (not found in CMakeLists scan)"
                }
            }
        }

        # Legacy product regressions (per-line; skip ADR / M12 history paths)
        $isHistorical = $rel -match 'docs[\\/]adr[\\/]|m12-legacy|m12-evidence|m12-gate'
        if (-not $isHistorical) {
            if ($line -match '(?i)run\s+Tina\.exe' -and $line -notmatch '(?i)(not |don''t |do not |不再|禁止|删除|retired)') {
                Add-Warn "${rel}:${lineNo}: possible instruct run Tina.exe"
            }
            if ($line -match '(?i)--smoke-game\b' -and $line -notmatch '(?i)(retired|删除|不再|Legacy)') {
                Add-Warn "${rel}:${lineNo}: possible Legacy --smoke-game"
            }
            if ($line -match '(?i)--smoke-ui\b' -and $line -notmatch '(?i)(retired|删除|不再|Legacy)') {
                Add-Warn "${rel}:${lineNo}: possible Legacy --smoke-ui"
            }
            if ($line -match '(?i)TINA_BUILD_LEGACY\s*=\s*ON') {
                # ASCII + CJK "失败"/"退役"/"删除" via char codes (script file stays ASCII-safe).
                $failZh = ([string][char]0x5931) + [char]0x8D25
                $retireZh = ([string][char]0x9000) + [char]0x5F79
                $deleteZh = ([string][char]0x5220) + [char]0x9664
                $okContext = '(?i)(FATAL|fail|retired|OFF)|' + [regex]::Escape($failZh) + '|' +
                    [regex]::Escape($retireZh) + '|' + [regex]::Escape($deleteZh)
                if ($line -notmatch $okContext) {
                    Add-Warn "${rel}:${lineNo}: TINA_BUILD_LEGACY=ON without failure/retirement context"
                }
            }
        }
    }
}

Write-Info "DOC-002 scan root=$RepoRoot"
Write-Info "markdown_files=$($mdFiles.Count) configure_presets=$($configureNames.Count) build_presets=$($buildNames.Count) targets=$($targetNames.Count)"
Write-Info "errors=$($errors.Count) warnings=$($warnings.Count)"

foreach ($w in $warnings) { Write-Info "WARN: $w" }
foreach ($e in $errors) { Write-Output "ERROR: $e" }

if ($errors.Count -gt 0) {
    Write-Output "DOC-002 FAILED ($($errors.Count) errors)"
    exit 1
}
if ($Strict -and $warnings.Count -gt 0) {
    Write-Output "DOC-002 FAILED (strict warnings=$($warnings.Count))"
    exit 2
}
Write-Output "DOC-002 OK"
exit 0
