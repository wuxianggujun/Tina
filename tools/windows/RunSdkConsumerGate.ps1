[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("GameSDK", "PlatformGlfw")]
    [string]$Consumer = "GameSDK"
)

$ErrorActionPreference = "Stop"

$sourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$isPlatformGlfwConsumer = $Consumer -eq "PlatformGlfw"
if([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = if($isPlatformGlfwConsumer) {
        "out/build/windows-msvc-vnext-platform"
    } else {
        "out/build/windows-msvc-vnext"
    }
}
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

$consumerDirectoryName = if($isPlatformGlfwConsumer) { "sdk-platform-glfw-consumer" } else { "sdk-consumer" }
$installPrefix = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-prefix"
$consumerBuildDirectory = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-build"
$missingComponentBuildDirectory = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-missing-component-build"
$consumerSourceDirectory = if($isPlatformGlfwConsumer) {
    Join-Path $sourceDirectory "tests/sdk_consumer_platform_glfw"
} else {
    Join-Path $sourceDirectory "tests/sdk_consumer"
}
$consumerTarget = if($isPlatformGlfwConsumer) { "tina_sdk_platform_glfw_consumer" } else { "tina_sdk_consumer" }
$consumerExecutableName = "$consumerTarget.exe"
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

$sdkBuildTargets = @("tina_runtime", "tina_scene", "tina_asset")
if($isPlatformGlfwConsumer) {
    $sdkBuildTargets += "tina_platform_glfw"
}
$sdkBuildArguments = @(
    "--build", $resolvedBuildDirectory,
    "--config", $Configuration,
    "--target"
) + $sdkBuildTargets + @("--parallel", "1", "--", "/nr:false")
& cmake @sdkBuildArguments
if($LASTEXITCODE -ne 0) { throw "Tina Game SDK build failed with exit code $LASTEXITCODE" }

& cmake --install $resolvedBuildDirectory --config $Configuration --prefix $installPrefix
if($LASTEXITCODE -ne 0) { throw "Tina Game SDK install failed with exit code $LASTEXITCODE" }

& cmake "-DTINA_SDK_INCLUDE_DIR=$($installPrefix -replace '\\', '/')/include" -P $verificationScript
if($LASTEXITCODE -ne 0) { throw "Installed Tina SDK header verification failed with exit code $LASTEXITCODE" }

$commonConfigureArguments = @(
    "-G", $generator,
    "-DCMAKE_CONFIGURATION_TYPES=$Configuration",
    "-DCMAKE_PREFIX_PATH=$installPrefix"
)
if(-not [string]::IsNullOrWhiteSpace($generatorPlatform)) {
    $commonConfigureArguments += @("-A", $generatorPlatform)
}
if(-not [string]::IsNullOrWhiteSpace($toolchainFile)) {
    $commonConfigureArguments += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
}
if(-not [string]::IsNullOrWhiteSpace($vcpkgInstalledDirectory)) {
    $commonConfigureArguments += "-DVCPKG_INSTALLED_DIR=$vcpkgInstalledDirectory"
}
if(-not [string]::IsNullOrWhiteSpace($vcpkgTargetTriplet)) {
    $commonConfigureArguments += "-DVCPKG_TARGET_TRIPLET=$vcpkgTargetTriplet"
}

$configureArguments = @(
    "-S", $consumerSourceDirectory,
    "-B", $consumerBuildDirectory,
    "-DTINA_EXPECTED_INSTALL_PREFIX=$installPrefix",
    "-DTINA_FORBIDDEN_SOURCE_DIR=$(Join-Path $sourceDirectory 'include')"
) + $commonConfigureArguments
& cmake @configureArguments
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer configure failed with exit code $LASTEXITCODE" }

$missingComponents = if($isPlatformGlfwConsumer) { "DefinitelyMissing" } else { "PlatformGlfw;DefinitelyMissing" }
$missingComponentConfigureArguments = @(
    "-S", (Join-Path $sourceDirectory "tests/sdk_consumer_missing_component"),
    "-B", $missingComponentBuildDirectory,
    "-DTINA_EXPECT_MISSING_COMPONENTS=$missingComponents"
) + $commonConfigureArguments
& cmake @missingComponentConfigureArguments
if($LASTEXITCODE -ne 0) { throw "Installed SDK missing-component probe failed with exit code $LASTEXITCODE" }

& cmake --build $consumerBuildDirectory --config $Configuration --target $consumerTarget --parallel 1 -- /nr:false
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer build failed with exit code $LASTEXITCODE" }

$consumerExecutable = Join-Path $consumerBuildDirectory "bin/$Configuration/$consumerExecutableName"
if(-not (Test-Path -LiteralPath $consumerExecutable)) {
    $consumerExecutable = Join-Path $consumerBuildDirectory "$Configuration/$consumerExecutableName"
}
& $consumerExecutable
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer failed with exit code $LASTEXITCODE" }
