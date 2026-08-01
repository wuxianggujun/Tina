[CmdletBinding()]
param(
    [string]$BuildDirectory = "out/build/windows-msvc-vnext",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$sourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$resolvedBuildDirectory = if([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $sourceDirectory $BuildDirectory))
}

$cachePath = Join-Path $resolvedBuildDirectory "CMakeCache.txt"
if(-not (Test-Path -LiteralPath $cachePath)) {
    throw "Tina build tree is not configured: $resolvedBuildDirectory"
}

function Get-CMakeCacheValue([string]$Name) {
    $entry = Select-String -LiteralPath $cachePath -Pattern "^$([Regex]::Escape($Name)):[^=]*=(.*)$" | Select-Object -First 1
    if($null -eq $entry) {
        return $null
    }
    return $entry.Matches[0].Groups[1].Value
}

$installPrefix = Join-Path $resolvedBuildDirectory "sdk-consumer-prefix"
$consumerBuildDirectory = Join-Path $resolvedBuildDirectory "sdk-consumer-build"
$consumerSourceDirectory = Join-Path $sourceDirectory "tests/sdk_consumer"
$verificationScript = Join-Path $sourceDirectory "cmake/VerifyInstalledTinaSdkHeaders.cmake"
$generator = Get-CMakeCacheValue "CMAKE_GENERATOR"
$generatorPlatform = Get-CMakeCacheValue "CMAKE_GENERATOR_PLATFORM"
$toolchainFile = Get-CMakeCacheValue "CMAKE_TOOLCHAIN_FILE"
$vcpkgInstalledDirectory = Get-CMakeCacheValue "VCPKG_INSTALLED_DIR"
$vcpkgTargetTriplet = Get-CMakeCacheValue "VCPKG_TARGET_TRIPLET"

$buildDirectoryPrefix = $resolvedBuildDirectory.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if(-not $installPrefix.StartsWith($buildDirectoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to refresh SDK prefix outside the Tina build tree: $installPrefix"
}
if(Test-Path -LiteralPath $installPrefix) {
    Remove-Item -LiteralPath $installPrefix -Recurse -Force
}

& cmake --build $resolvedBuildDirectory --config $Configuration `
    --target tina_runtime tina_scene tina_asset --parallel 1 -- /nr:false
if($LASTEXITCODE -ne 0) { throw "Tina Game SDK build failed with exit code $LASTEXITCODE" }

& cmake --install $resolvedBuildDirectory --config $Configuration --prefix $installPrefix
if($LASTEXITCODE -ne 0) { throw "Tina Game SDK install failed with exit code $LASTEXITCODE" }

& cmake "-DTINA_SDK_INCLUDE_DIR=$($installPrefix -replace '\\', '/')/include" -P $verificationScript
if($LASTEXITCODE -ne 0) { throw "Installed Tina SDK header verification failed with exit code $LASTEXITCODE" }

$configureArguments = @(
    "-S", $consumerSourceDirectory,
    "-B", $consumerBuildDirectory,
    "-G", $generator,
    "-DCMAKE_CONFIGURATION_TYPES=$Configuration",
    "-DCMAKE_PREFIX_PATH=$installPrefix",
    "-DTINA_EXPECTED_INSTALL_PREFIX=$installPrefix",
    "-DTINA_FORBIDDEN_SOURCE_DIR=$(Join-Path $sourceDirectory 'include')"
)
if(-not [string]::IsNullOrWhiteSpace($generatorPlatform)) {
    $configureArguments += @("-A", $generatorPlatform)
}
if(-not [string]::IsNullOrWhiteSpace($toolchainFile)) {
    $configureArguments += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
}
if(-not [string]::IsNullOrWhiteSpace($vcpkgInstalledDirectory)) {
    $configureArguments += "-DVCPKG_INSTALLED_DIR=$vcpkgInstalledDirectory"
}
if(-not [string]::IsNullOrWhiteSpace($vcpkgTargetTriplet)) {
    $configureArguments += "-DVCPKG_TARGET_TRIPLET=$vcpkgTargetTriplet"
}

& cmake @configureArguments
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer configure failed with exit code $LASTEXITCODE" }

& cmake --build $consumerBuildDirectory --config $Configuration --target tina_sdk_consumer --parallel 1 -- /nr:false
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer build failed with exit code $LASTEXITCODE" }

$consumerExecutable = Join-Path $consumerBuildDirectory "bin/$Configuration/tina_sdk_consumer.exe"
if(-not (Test-Path -LiteralPath $consumerExecutable)) {
    $consumerExecutable = Join-Path $consumerBuildDirectory "$Configuration/tina_sdk_consumer.exe"
}
& $consumerExecutable
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer failed with exit code $LASTEXITCODE" }
