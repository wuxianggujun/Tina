[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("GameSDK", "PlatformGlfw", "AudioMiniaudio", "DesktopBootstrap")]
    [string]$Consumer = "GameSDK"
)

$ErrorActionPreference = "Stop"

$sourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$isPlatformGlfwConsumer = $Consumer -eq "PlatformGlfw"
$isAudioMiniaudioConsumer = $Consumer -eq "AudioMiniaudio"
$isDesktopBootstrapConsumer = $Consumer -eq "DesktopBootstrap"
if([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = if($isDesktopBootstrapConsumer) {
        "out/build/windows-msvc-vnext-bgfx"
    } elseif($isAudioMiniaudioConsumer) {
        "out/build/windows-msvc-vnext-audio-miniaudio"
    } elseif($isPlatformGlfwConsumer) {
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

$consumerDirectoryName = if($isDesktopBootstrapConsumer) {
    "sdk-desktop-bootstrap-consumer"
} elseif($isAudioMiniaudioConsumer) {
    "sdk-audio-miniaudio-consumer"
} elseif($isPlatformGlfwConsumer) {
    "sdk-platform-glfw-consumer"
} else {
    "sdk-consumer"
}
$installPrefix = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-prefix"
$consumerBuildDirectory = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-build"
$missingComponentBuildDirectory = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-missing-component-build"
$componentIsolationBuildDirectory = Join-Path $resolvedBuildDirectory "$consumerDirectoryName-component-isolation-build"
$consumerSourceDirectory = if($isDesktopBootstrapConsumer) {
    Join-Path $sourceDirectory "tests/sdk_consumer_desktop"
} elseif($isAudioMiniaudioConsumer) {
    Join-Path $sourceDirectory "tests/sdk_consumer_audio_miniaudio"
} elseif($isPlatformGlfwConsumer) {
    Join-Path $sourceDirectory "tests/sdk_consumer_platform_glfw"
} else {
    Join-Path $sourceDirectory "tests/sdk_consumer"
}
$consumerTarget = if($isDesktopBootstrapConsumer) {
    "tina_sdk_desktop_bootstrap_consumer"
} elseif($isAudioMiniaudioConsumer) {
    "tina_sdk_audio_miniaudio_consumer"
} elseif($isPlatformGlfwConsumer) {
    "tina_sdk_platform_glfw_consumer"
} else {
    "tina_sdk_consumer"
}
$consumerExecutableName = "$consumerTarget.exe"
$verificationScript = Join-Path $sourceDirectory "cmake/VerifyInstalledTinaSdkHeaders.cmake"
$generator = Get-CMakeCacheValue "CMAKE_GENERATOR"
$generatorPlatform = Get-CMakeCacheValue "CMAKE_GENERATOR_PLATFORM"
$toolchainFile = Get-CMakeCacheValue "CMAKE_TOOLCHAIN_FILE"
$vcpkgInstalledDirectory = Get-CMakeCacheValue "VCPKG_INSTALLED_DIR"
$vcpkgTargetTriplet = Get-CMakeCacheValue "VCPKG_TARGET_TRIPLET"

$buildDirectoryPrefix = $resolvedBuildDirectory.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
foreach($managedDirectory in @($installPrefix, $consumerBuildDirectory, $missingComponentBuildDirectory,
        $componentIsolationBuildDirectory)) {
    if(-not $managedDirectory.StartsWith($buildDirectoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to refresh SDK gate directory outside the Tina build tree: $managedDirectory"
    }
    if(Test-Path -LiteralPath $managedDirectory) {
        Remove-Item -LiteralPath $managedDirectory -Recurse -Force
    }
}

$sdkBuildTargets = @("tina_runtime", "tina_scene", "tina_asset")
if($isPlatformGlfwConsumer) {
    $sdkBuildTargets += "tina_platform_glfw"
}
if($isAudioMiniaudioConsumer) {
    $sdkBuildTargets += "tina_audio_miniaudio"
}
if($isDesktopBootstrapConsumer) {
    $sdkBuildTargets += "tina_bootstrap_desktop"
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

$audioMiniaudioEnabled = (Get-CMakeCacheValue "TINA_BUILD_AUDIO_MINIAUDIO") -eq "ON"
& cmake "-DTINA_SDK_INCLUDE_DIR=$($installPrefix -replace '\\', '/')/include" `
    "-DTINA_EXPECT_AUDIO_MINIAUDIO=$audioMiniaudioEnabled" -P $verificationScript
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

$missingComponents = if($isDesktopBootstrapConsumer) {
    if($audioMiniaudioEnabled) { "DefinitelyMissing" } else { "AudioMiniaudio;DefinitelyMissing" }
} elseif($isAudioMiniaudioConsumer) {
    "PlatformGlfw;RenderBgfx;UIFreetype;DesktopBootstrap;DefinitelyMissing"
} elseif($isPlatformGlfwConsumer) {
    if($audioMiniaudioEnabled) {
        "RenderBgfx;UIFreetype;DefinitelyMissing"
    } else {
        "RenderBgfx;UIFreetype;AudioMiniaudio;DefinitelyMissing"
    }
} else {
    "PlatformGlfw;RenderBgfx;UIFreetype;AudioMiniaudio;DesktopBootstrap;DefinitelyMissing"
}
$missingComponentConfigureArguments = @(
    "-S", (Join-Path $sourceDirectory "tests/sdk_consumer_missing_component"),
    "-B", $missingComponentBuildDirectory,
    "-DTINA_EXPECT_MISSING_COMPONENTS=$missingComponents"
) + $commonConfigureArguments
& cmake @missingComponentConfigureArguments
if($LASTEXITCODE -ne 0) { throw "Installed SDK missing-component probe failed with exit code $LASTEXITCODE" }

$componentIsolationConfigureArguments = @(
    "-S", (Join-Path $sourceDirectory "tests/sdk_consumer_component_isolation"),
    "-B", $componentIsolationBuildDirectory,
    "-DCMAKE_DISABLE_FIND_PACKAGE_glfw3=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_bgfx=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_Freetype=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_miniaudio=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_Vorbis=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_Opus=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_OpusFile=TRUE",
    "-DCMAKE_DISABLE_FIND_PACKAGE_Threads=TRUE"
) + $commonConfigureArguments
& cmake @componentIsolationConfigureArguments
if($LASTEXITCODE -ne 0) { throw "Installed SDK component-isolation probe failed with exit code $LASTEXITCODE" }

& cmake --build $consumerBuildDirectory --config $Configuration --target $consumerTarget --parallel 1 -- /nr:false
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer build failed with exit code $LASTEXITCODE" }

$consumerExecutable = Join-Path $consumerBuildDirectory "bin/$Configuration/$consumerExecutableName"
if(-not (Test-Path -LiteralPath $consumerExecutable)) {
    $consumerExecutable = Join-Path $consumerBuildDirectory "$Configuration/$consumerExecutableName"
}
& $consumerExecutable
if($LASTEXITCODE -ne 0) { throw "Installed SDK consumer failed with exit code $LASTEXITCODE" }
