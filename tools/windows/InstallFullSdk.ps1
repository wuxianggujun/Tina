[CmdletBinding()]
param(
    [string]$BuildDirectory = "out/build/windows-msvc-vnext-sdk",
    [string]$Prefix = "D:/ProgramData/Tina"
)

# Publication only: the caller builds Debug and Release first. Never configure,
# rebuild, start the editor, run tests, or erase an installation/build directory.
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$sourceDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $sourceDirectory $BuildDirectory))
}
$installPrefix = [IO.Path]::GetFullPath($Prefix)
if ($installPrefix.TrimEnd('\', '/') -eq [IO.Path]::GetPathRoot($installPrefix).TrimEnd('\', '/') -or
    $installPrefix -eq $sourceDirectory -or $installPrefix -eq $build) {
    throw "An SDK prefix must not be a drive root, source root or build root: $installPrefix"
}
$cachePath = Join-Path $build "CMakeCache.txt"
$cache = [IO.File]::ReadAllText($cachePath, [Text.Encoding]::UTF8)
function Read-Cache([string]$Name) {
    $match = [regex]::Match($cache, "(?m)^$([regex]::Escape($Name)):[^=]*=([^`r`n]*)")
    if (-not $match.Success) { throw "Missing producer cache value: $Name" }
    return $match.Groups[1].Value
}
if ([IO.Path]::GetFullPath((Read-Cache "CMAKE_HOME_DIRECTORY")) -ne $sourceDirectory) {
    throw "Build tree belongs to a different source checkout: $build"
}
$requiredFeatures = @("GameSDK", "Physics2D", "Physics3D", "PlatformGlfw", "RenderBgfx",
    "UIFreetype", "UIUia", "AudioMiniaudio", "NetworkTls", "Desktop", "TraceTracy")
$features = (Read-Cache "TINA_SDK_FEATURES").Split(';')
foreach ($feature in $requiredFeatures) {
    if ($feature -notin $features) { throw "Full SDK is missing the compiled capability: $feature" }
}
foreach ($option in @("TINA_BUILD_EDITOR", "TINA_BUILD_TOOLS", "TINA_BUILD_SHADERS",
        "TINA_RENDER_BGFX_MOBILE_SHADERS")) {
    if ((Read-Cache $option) -ne "ON") { throw "Full SDK requires $option=ON" }
}
$triplet = Read-Cache "VCPKG_TARGET_TRIPLET"
if ($triplet -ne "x64-windows") { throw "This publisher requires x64-windows, found $triplet" }
$dependencies = Join-Path (Read-Cache "VCPKG_INSTALLED_DIR") $triplet
$editorInstall = Join-Path $build "editor/app/cmake_install.cmake"
$requiredFiles = @($editorInstall, (Join-Path $build "bin/Release/TinaEditor.exe"))
foreach ($configuration in @("Debug", "Release")) {
    $requiredFiles += Join-Path $build "lib/$configuration/Tina.lib"
    $requiredFiles += Join-Path $build "bin/$configuration/tina_assetc.exe"
    $requiredFiles += Join-Path $build "bin/$configuration/tina_catalog_validate.exe"
    $requiredFiles += Join-Path $build "bin/$configuration/tina_msdfgen.exe"
    $requiredFiles += Join-Path $build "bin/$configuration/shaderc.exe"
}
foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Build both SDK configurations and the Release editor before publishing: $file"
    }
}

# Preserve the relocatable vcpkg layout (including both CRT configurations), but
# omit its package manager/build caches and unrelated host-tool executables.
# Copy bytes even when timestamps match: Debug/Release DLL timestamps can match.
$dependencyPrefix = Join-Path $installPrefix "dependencies"
$copiedBytes = [long]0
$copiedFiles = 0
foreach ($relativeDirectory in @("include", "lib", "bin", "debug/lib", "debug/bin", "share")) {
    $origin = Join-Path $dependencies $relativeDirectory
    if (-not (Test-Path -LiteralPath $origin -PathType Container)) {
        throw "Prebuilt dependency directory is absent: $origin"
    }
    foreach ($file in Get-ChildItem -LiteralPath $origin -File -Recurse) {
        $relative = $file.FullName.Substring($dependencies.TrimEnd('\', '/').Length + 1)
        $destination = Join-Path $dependencyPrefix $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        $copiedBytes += $file.Length
        ++$copiedFiles
    }
}

$removedDebugHostFiles = 0
foreach ($configuration in @("Debug", "Release")) {
    & cmake --install $build --config $configuration --prefix $installPrefix --component sdk
    if ($LASTEXITCODE -ne 0) { throw "$configuration SDK install failed: $LASTEXITCODE" }
    if ($configuration -eq "Debug") {
        # The archives have per-config directories, but host tools share bin/.
        # vcpkg DLLs can have identical Debug/Release timestamps: CMake install
        # then incorrectly keeps the Debug bytes. Remove only files just
        # published by this component, never the bin/ directory or user files.
        $hostDirectory = [IO.Path]::GetFullPath((Join-Path $installPrefix "bin"))
        $manifestPath = Join-Path $build "install_manifest_sdk.txt"
        foreach ($installedFile in [IO.File]::ReadAllLines($manifestPath, [Text.Encoding]::UTF8)) {
            if ([string]::IsNullOrWhiteSpace($installedFile)) { continue }
            $installedPath = [IO.Path]::GetFullPath($installedFile)
            if ([IO.Path]::GetDirectoryName($installedPath) -ne $hostDirectory -or
                [IO.Path]::GetExtension($installedPath) -notin @(".exe", ".dll")) { continue }
            if (Test-Path -LiteralPath $installedPath -PathType Leaf) {
                Remove-Item -LiteralPath $installedPath -Force
                ++$removedDebugHostFiles
            }
        }
    }
}
# Only the editor subdirectory: do not publish stale or unbuilt sample products.
& cmake "-DCMAKE_INSTALL_PREFIX=$($installPrefix -replace '\\', '/')" `
    -DCMAKE_INSTALL_CONFIG_NAME=Release -DCMAKE_INSTALL_COMPONENT=products -P $editorInstall
if ($LASTEXITCODE -ne 0) { throw "Release editor install failed: $LASTEXITCODE" }

$inventory = [ordered]@{
    schemaVersion = 1
    triplet = $triplet
    buildId = Read-Cache "TINA_SDK_BUILD_ID"
    configurations = @("Debug", "Release")
    hostToolConfiguration = "Release"
    features = $features
    dependencyPrefix = "dependencies"
    copiedFiles = $copiedFiles
    copiedBytes = $copiedBytes
    removedDebugHostFiles = $removedDebugHostFiles
}
$inventoryPath = Join-Path $installPrefix "share/Tina/prebuilt-dependencies.json"
[IO.File]::WriteAllText($inventoryPath, ($inventory | ConvertTo-Json -Depth 4) + "`n",
    [Text.UTF8Encoding]::new($false))
Write-Output "Full SDK published: $installPrefix (Debug + Release); editor: Release"
Write-Output "Prebuilt dependency files=$copiedFiles bytes=$copiedBytes; no builds or programs were run."
