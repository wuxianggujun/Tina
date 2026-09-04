#include <tina/asset/AssetGpuEnvironmentMap.hpp>
#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/Animator3D.hpp>
#include <tina/scene/DirectionalLight3D.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PointLight3D.hpp>
#include <tina/scene/PrefabInstantiate.hpp>
#include <tina/scene/SkinnedMeshRenderer3D.hpp>
#include <tina/scene/SpotLight3D.hpp>
#include <tina/scene/World.hpp>

#include "DeviceCapture.hpp"
#include "Product3DUI.hpp"
#include "ProductEnvironmentMapFixture.hpp"
#include "SampleContentDirectory.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 DefaultWindowLogicalWidth = 1280;
inline constexpr u32 DefaultWindowLogicalHeight = 720;
inline constexpr u32 TransparentWitnessCount = 2;
// Khronos MetalRoughSpheres* is a mesh-per-sphere MR grid (often 50–100+ meshes).
inline constexpr u32 MaxProductMeshSlots = 128;

[[nodiscard]] std::string contentHashToHex(const Tina::Core::ContentHash& hash)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(32, '0');
    const auto& bytes = hash.bytes();
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const auto value = static_cast<unsigned>(std::to_integer<unsigned char>(bytes[index]));
        out[index * 2] = kHex[(value >> 4U) & 0x0FU];
        out[index * 2 + 1] = kHex[value & 0x0FU];
    }
    return out;
}

[[nodiscard]] Tina::Math::Quaternion rotationFromPositiveZ(
    Tina::Math::Vec3 directionTowardLight) noexcept
{
    const double lengthSquared =
        static_cast<double>(directionTowardLight.x) * directionTowardLight.x +
        static_cast<double>(directionTowardLight.y) * directionTowardLight.y +
        static_cast<double>(directionTowardLight.z) * directionTowardLight.z;
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
    directionTowardLight = directionTowardLight * inverseLength;
    if (directionTowardLight.z <= -0.999999F)
    {
        return {1.0F, 0.0F, 0.0F, 0.0F};
    }
    return Tina::Math::normalized(Tina::Math::Quaternion{
        .x = -directionTowardLight.y,
        .y = directionTowardLight.x,
        .z = 0.0F,
        .w = 1.0F + directionTowardLight.z,
    });
}

enum class ImageBasedLightingMode {
    On,
    Off,
};

[[nodiscard]] constexpr std::string_view imageBasedLightingModeName(ImageBasedLightingMode mode) noexcept
{
    return mode == ImageBasedLightingMode::On ? "on" : "off";
}

enum class PointLightShadowMode {
    On,
    Off,
};

[[nodiscard]] constexpr std::string_view pointLightShadowModeName(
    PointLightShadowMode mode) noexcept
{
    return mode == PointLightShadowMode::On ? "on" : "off";
}

enum class SkinAnimationMode {
    On,
    Off,
};

[[nodiscard]] constexpr std::string_view skinAnimationModeName(SkinAnimationMode mode) noexcept
{
    return mode == SkinAnimationMode::On ? "on" : "off";
}

enum class TransparencyMode {
    On,
    Off,
};

[[nodiscard]] constexpr std::string_view transparencyModeName(TransparencyMode mode) noexcept
{
    return mode == TransparencyMode::On ? "on" : "off";
}

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    u32 windowLogicalWidth = DefaultWindowLogicalWidth;
    u32 windowLogicalHeight = DefaultWindowLogicalHeight;
    // Empty → in-memory two-mesh fixture. Non-empty → cook external .gltf/.glb path.
    std::string gltfPath{};
    // Empty = capture-only; non-empty = require an exact machine-local pixel match.
    std::string expectPixelFingerprint{};
    // Optional gate-only raw RGB output for exact cross-run ROI comparison.
    std::string sceneRgbOutputPath{};
    Tina::Sample3D::Product3DUITheme initialUiTheme = Tina::Sample3D::Product3DUITheme::Dark;
    ImageBasedLightingMode imageBasedLightingMode = ImageBasedLightingMode::On;
    PointLightShadowMode pointLightShadowMode = PointLightShadowMode::On;
    SkinAnimationMode skinAnimationMode = SkinAnimationMode::On;
    TransparencyMode transparencyMode = TransparencyMode::On;
    bool uiThemeDemo = false;
    bool help = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 sceneLightingFrames = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    Tina::Sample3D::Product3DUIEvidence ui{};
    u64 meshesUploaded = 0;
    u64 materialsLoaded = 0;
    u64 texturesUploaded = 0;
    u64 catalogCooked = 0;
    u64 prefabNodes = 0;
    u64 prefabInstances = 0;
    u64 meshAssetHandlesPublished = 0;
    u64 materialAssetHandlesPublished = 0;
    u64 meshFrameResourceResolverHits = 0;
    u64 skinnedMeshFrameResourceResolverHits = 0;
    u64 skinnedPoseProviderHits = 0;
    u64 materialFrameResourceResolverHits = 0;
    u64 meshBindingsRegistered = 0;
    u64 materialBindingsRegistered = 0;
    u64 meshBindingsReleased = 0;
    u64 materialBindingsReleased = 0;
    u64 meshRetirementsAccepted = 0;
    u64 textureRetirementsAccepted = 0;
    u64 meshRetirementRecords = 0;
    u64 textureRetirementRecords = 0;
    u64 meshRetirementReleased = 0;
    u64 textureRetirementReleased = 0;
    u64 retirementRecordsLive = 0;
    u64 meshAssetHandlesInvalidated = 0;
    u64 materialAssetHandlesInvalidated = 0;
    u64 textureAssetHandlesInvalidated = 0;
    u64 animationClipAssetHandlesPublished = 0;
    u64 animationClipAssetHandlesInvalidated = 0;
    u64 skinnedPrefabAssetHandlesPublished = 0;
    u64 skinnedPrefabAssetHandlesInvalidated = 0;
    u64 animatorUpdates = 0;
    u64 animatorPoseChanges = 0;
    u32 animatorJointCount = 0;
    u32 skinnedPrefabInstances = 0;
    u32 authoredTransparentStaticWitnessCount = 0;
    bool transparentWitnessMaterialBound = false;
    bool cookedEnvironmentMap = false;
    bool imageBasedLightingConfigured = false;
    bool meshBound = false;
    bool materialTextureBound = false;
    bool materialFactorsBound = false;
    bool materialMrTextureBound = false;
    bool materialNormalTextureBound = false;
    u32 directionalLightCount = 0;
    u32 cascadedDirectionalShadowCount = 0;
    u32 cascadedDirectionalShadowCascadeCount = 0;
    u32 authoredPointLight3DCount = 0;
    u32 pointLight3DCount = 0;
    u32 culledPointLight3DCount = 0;
    u32 authoredPointLightShadowCount = 0;
    u32 authoredSpotLight3DCount = 0;
    u32 spotLight3DCount = 0;
    u32 culledSpotLight3DCount = 0;
    u32 authoredSpotLightShadowCount = 0;
    u64 submittedLightingFrames = 0;
    u32 submittedDirectionalLightCount = 0;
    u64 windowMetricsEvents = 0;
    u32 logicalPixelWidth = DefaultWindowLogicalWidth;
    u32 logicalPixelHeight = DefaultWindowLogicalHeight;
    u32 framebufferPixelWidth = DefaultWindowLogicalWidth;
    u32 framebufferPixelHeight = DefaultWindowLogicalHeight;
    float submittedCameraAspectRatio = 0.0F;
    u64 cameraAspectChanges = 0;
    bool cameraAspectMatchesSurface = false;
    bool lightingCountsStable = false;
    bool lightingConfigured = false;
    bool gltfCooked = false;
    bool prefabInstantiated = false;
    bool completePbrFixture = false;
    bool pixelCaptureAttempted = false;
    bool pixelCaptureOk = false;
    bool bindingRegistryReleased = false;
    u32 pixelCaptureWidth = 0;
    u32 pixelCaptureHeight = 0;
    u64 pixelCaptureBytes = 0;
    std::string pixelFingerprint{};
    u64 sceneRgbPixelCount = 0;
    std::array<u64, 3> sceneRgbChannelSums{};
    std::string sceneRgbFingerprint{};
    bool sceneRgbOutputWritten = false;
};

using DeviceCapture = Tina::Sample3D::DeviceCapture;

void recordPixelCapture(LifecycleCounters& counters,
                        const Tina::Render::Rgba8FrameCapture& capture,
                        std::string_view sceneRgbOutputPath)
{
    if (capture.empty())
    {
        return;
    }
    const u64 expectedCaptureBytes = static_cast<u64>(capture.width) * capture.height * 4U;
    if (capture.byteCount() != expectedCaptureBytes || capture.width < 4U || capture.height < 4U)
    {
        return;
    }
    auto pixelHash = Tina::Core::digestContentHashV1(capture.rgba8Pixels);
    if (!pixelHash.has_value() || !pixelHash->hasValue())
    {
        return;
    }

    // Central product viewport, excluding the header, right inspector, and footer UI.
    const u32 sceneLeft = capture.width / 4U;
    const u32 sceneRight = static_cast<u32>(static_cast<u64>(capture.width) * 2U / 3U);
    const u32 sceneTop = capture.height / 4U;
    const u32 sceneBottom = static_cast<u32>(static_cast<u64>(capture.height) * 3U / 4U);
    const u64 scenePixelCount = static_cast<u64>(sceneRight - sceneLeft) * (sceneBottom - sceneTop);
    std::vector<std::byte> sceneRgbPixels(static_cast<std::size_t>(scenePixelCount * 3U));
    std::array<u64, 3> sceneRgbChannelSums{};
    std::size_t destination = 0;
    for (u32 y = sceneTop; y < sceneBottom; ++y)
    {
        for (u32 x = sceneLeft; x < sceneRight; ++x)
        {
            const std::size_t source =
                static_cast<std::size_t>((static_cast<u64>(y) * capture.width + x) * 4U);
            for (std::size_t channel = 0; channel < sceneRgbChannelSums.size(); ++channel)
            {
                const std::byte value = capture.rgba8Pixels[source + channel];
                sceneRgbPixels[destination++] = value;
                sceneRgbChannelSums[channel] += std::to_integer<unsigned char>(value);
            }
        }
    }
    auto sceneRgbHash = Tina::Core::digestContentHashV1(sceneRgbPixels);
    if (!sceneRgbHash.has_value() || !sceneRgbHash->hasValue())
    {
        return;
    }

    counters.pixelCaptureOk = true;
    counters.pixelCaptureWidth = capture.width;
    counters.pixelCaptureHeight = capture.height;
    counters.pixelCaptureBytes = static_cast<u64>(capture.byteCount());
    counters.pixelFingerprint = contentHashToHex(*pixelHash);
    counters.sceneRgbPixelCount = scenePixelCount;
    counters.sceneRgbChannelSums = sceneRgbChannelSums;
    counters.sceneRgbFingerprint = contentHashToHex(*sceneRgbHash);
    if (!sceneRgbOutputPath.empty())
    {
        std::ofstream output(std::filesystem::path{sceneRgbOutputPath},
                             std::ios::binary | std::ios::trunc);
        if (output.good())
        {
            output.write(reinterpret_cast<const char*>(sceneRgbPixels.data()),
                         static_cast<std::streamsize>(sceneRgbPixels.size()));
            counters.sceneRgbOutputWritten = output.good();
        }
    }
}

struct ProductMeshSlot final {
    Tina::Asset::AssetHandle meshAsset{};
    Tina::Asset::AssetHandle materialAsset{};
    Tina::Asset::AssetHandle textureAsset{};
    Tina::Asset::AssetHandle metallicRoughnessTextureAsset{};
    Tina::Asset::AssetHandle normalTextureAsset{};
    Tina::Render::RenderLinearColor materialColor{.red = 0.2F, .green = 0.6F, .blue = 0.9F, .alpha = 1.0F};
    Tina::Render::Mesh3DAlphaMode alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque;
    float metallicFactor = 0.0F;
    float roughnessFactor = 1.0F;
    float meshBoundsRadius = 1.75F;
    Tina::Core::AssetId meshId{};
    Tina::Core::AssetId materialId{};
    Tina::Core::AssetId textureId{};
    Tina::Core::AssetId metallicRoughnessTextureId{};
    Tina::Core::AssetId normalTextureId{};
    Tina::AssetFormat::AssetKind meshKind = Tina::AssetFormat::AssetKind::Invalid;
};

struct ProductTextureAsset final {
    Tina::Core::AssetId id{};
    Tina::Asset::AssetHandle handle{};
};

struct Product3DResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::optional<Tina::Asset::AssetSystem> assetSystem{};
    std::filesystem::path workRoot{};
    std::filesystem::path catalogRoot{};
    Tina::Asset::AssetHandle prefabAsset{};
    Tina::Core::AssetId prefabId{};
    Tina::Asset::AssetHandle skinnedPrefabAsset{};
    Tina::Core::AssetId skinnedPrefabId{};
    Tina::Asset::AssetHandle animationClipAsset{};
    Tina::Core::AssetId animationClipId{};
    Tina::Asset::AssetHandle transparentMaterialAsset{};
    Tina::Core::AssetId transparentMaterialId{};
    std::optional<Tina::Asset::CookedAssetFile> environmentMapAsset{};
    Tina::Core::AssetId environmentMapId{};
    std::array<ProductMeshSlot, MaxProductMeshSlots> meshes{};
    std::array<ProductTextureAsset, MaxProductMeshSlots * 3U> textures{};
    u32 meshSlotCount = 0;
    u32 staticMeshSlotCount = 0;
    u32 skinnedMeshSlotCount = 0;
    u32 witnessSkinnedMeshSlot = MaxProductMeshSlots;
    u32 textureAssetCount = 0;
    u32 blendMaterialCount = 0;
    bool hasDedicatedSkinnedPrefab = false;
    bool externalGltf = false;
    bool completePbrFixture = false;
    std::string gltfSourcePath{};
};

// High marker so prefab id stays above mesh/material product indices for small slot counts
// and remains unique for MaxProductMeshSlots (uses 0xFFFE).
[[nodiscard]] Tina::Core::AssetId::Bytes prefabIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0xFFU);
    bytes[15] = static_cast<std::byte>(0xFEU);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes skinnedPrefabIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0xFFU);
    bytes[15] = static_cast<std::byte>(0xFDU);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes animationClipIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0xFFU);
    bytes[15] = static_cast<std::byte>(0xFCU);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes transparentMaterialIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0xFFU);
    bytes[15] = static_cast<std::byte>(0xFBU);
    return bytes;
}

// 1x1 red PNG fixture (same as GltfCookTests).
[[nodiscard]] std::span<const unsigned char> productTinyRedPng() noexcept
{
    static constexpr unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xFC, 0xCF, 0xC0, 0x50,
        0x0F, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xA9, 0x8C, 0x21, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    return std::span<const unsigned char>{kPng, sizeof(kPng)};
}

// Fallback only when the complete PBR fixture is not staged (minimal two-mesh + baseColor).
[[nodiscard]] std::string_view productTwoMeshGltfJsonFallback() noexcept
{
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"mesh": 0},
    {"mesh": 1, "translation": [2.0, 0.0, 0.0]}
  ],
  "meshes": [
    {"primitives": [{"attributes": {"POSITION": 0}, "indices": 2, "mode": 4, "material": 0}]},
    {"primitives": [{"attributes": {"POSITION": 1}, "indices": 3, "mode": 4, "material": 1}]}
  ],
  "materials": [
    {"pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "metallicFactor": 0.15,
      "roughnessFactor": 0.65,
      "baseColorTexture": {"index": 0}
    }},
    {"pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "metallicFactor": 0.85,
      "roughnessFactor": 0.25,
      "baseColorTexture": {"index": 0}
    }}
  ],
  "textures": [{"source": 0}],
  "images": [{"uri": "tex.png"}],
  "accessors": [
    {
      "bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 0, "byteOffset": 36, "componentType": 5126, "count": 3, "type": "VEC3",
      "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]
    },
    {"bufferView": 1, "byteOffset": 0, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 1, "byteOffset": 6, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 72},
    {"buffer": 0, "byteOffset": 72, "byteLength": 12}
  ],
  "buffers": [{
    "byteLength": 84,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAEAAAACAAAAAAAAAAEAAAACAAAA"
  }]
})json";
}

[[nodiscard]] std::string_view productSkinnedGltfJson() noexcept
{
    return R"json({
  "asset":{"version":"2.0"},
  "scene":0,
  "scenes":[{"nodes":[0]}],
  "nodes":[
    {"children":[1]},
    {"children":[2]},
    {"mesh":0,"skin":0,"translation":[-2.5,0.0,0.0],"scale":[2.5,2.5,2.5]}
  ],
  "skins":[{"joints":[1,0],"inverseBindMatrices":7}],
  "meshes":[{"primitives":[{
    "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3,"JOINTS_0":4,"WEIGHTS_0":5},
    "indices":6,"mode":4,"material":0
  }]}],
  "materials":[{"doubleSided":true,"pbrMetallicRoughness":{
    "baseColorFactor":[0.12,0.78,0.55,1.0],"metallicFactor":0.15,"roughnessFactor":0.48
  }}],
  "animations":[{"samplers":[{"input":8,"output":9,"interpolation":"LINEAR"}],
    "channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
    {"bufferView":4,"componentType":5123,"count":3,"type":"VEC4"},
    {"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"},
    {"bufferView":6,"componentType":5123,"count":3,"type":"SCALAR"},
    {"bufferView":7,"componentType":5126,"count":2,"type":"MAT4"},
    {"bufferView":8,"componentType":5126,"count":2,"type":"SCALAR"},
    {"bufferView":9,"componentType":5126,"count":2,"type":"VEC3"}
  ],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":36},
    {"buffer":0,"byteOffset":36,"byteLength":36},
    {"buffer":0,"byteOffset":72,"byteLength":24},
    {"buffer":0,"byteOffset":96,"byteLength":48},
    {"buffer":0,"byteOffset":144,"byteLength":24},
    {"buffer":0,"byteOffset":168,"byteLength":48},
    {"buffer":0,"byteOffset":216,"byteLength":6},
    {"buffer":0,"byteOffset":224,"byteLength":128},
    {"buffer":0,"byteOffset":352,"byteLength":8},
    {"buffer":0,"byteOffset":360,"byteLength":24}
  ],
  "buffers":[{"byteLength":384,"uri":"geometry.bin"}]
})json";
}

[[nodiscard]] std::vector<unsigned char> productSkinnedBufferBytes()
{
    std::vector<unsigned char> bytes(384U, 0U);
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<float, 9> normals{0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::array<float, 6> uv{0, 0, 1, 0, 0, 1};
    const std::array<float, 12> tangents{1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    // skin.joints is [child, root]; cooker remaps these source indices to [root, child].
    const std::array<Tina::Core::u16, 12> joints{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::array<float, 12> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const std::array<Tina::Core::u16, 3> indices{0, 1, 2};
    std::array<float, 32> inverseBind{};
    inverseBind[0] = 1.0F;
    inverseBind[5] = 1.0F;
    inverseBind[10] = 1.0F;
    inverseBind[15] = 1.0F;
    inverseBind[16] = 1.0F;
    inverseBind[21] = 1.0F;
    inverseBind[26] = 1.0F;
    inverseBind[31] = 1.0F;
    const std::array<float, 2> times{0, 1};
    const std::array<float, 6> translations{0, 0, 0, 0, 0.75F, 0};
    std::memcpy(bytes.data() + 0, positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + 36, normals.data(), sizeof(normals));
    std::memcpy(bytes.data() + 72, uv.data(), sizeof(uv));
    std::memcpy(bytes.data() + 96, tangents.data(), sizeof(tangents));
    std::memcpy(bytes.data() + 144, joints.data(), sizeof(joints));
    std::memcpy(bytes.data() + 168, weights.data(), sizeof(weights));
    std::memcpy(bytes.data() + 216, indices.data(), sizeof(indices));
    std::memcpy(bytes.data() + 224, inverseBind.data(), sizeof(inverseBind));
    std::memcpy(bytes.data() + 352, times.data(), sizeof(times));
    std::memcpy(bytes.data() + 360, translations.data(), sizeof(translations));
    return bytes;
}

[[nodiscard]] Tina::Core::Status writeSkinnedGltfFixture(
    const std::filesystem::path& workRoot,
    std::filesystem::path& outGltfPath)
{
    const std::filesystem::path skinRoot = workRoot / "skinned";
    std::error_code error;
    if (!std::filesystem::create_directories(skinRoot, error) || error)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "failed to create temporary skinned glTF fixture directory");
    }
    const auto geometry = productSkinnedBufferBytes();
    std::ofstream binary(skinRoot / "geometry.bin", std::ios::binary);
    if (!binary.good())
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "failed to write temporary skinned glTF geometry");
    }
    binary.write(reinterpret_cast<const char*>(geometry.data()),
                 static_cast<std::streamsize>(geometry.size()));
    if (!binary.good())
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "failed to finish temporary skinned glTF geometry");
    }
    outGltfPath = skinRoot / "product_skinned.gltf";
    std::ofstream document(outGltfPath, std::ios::binary);
    if (!document.good())
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "failed to write temporary skinned glTF document");
    }
    document << productSkinnedGltfJson();
    if (!document.good())
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "failed to finish temporary skinned glTF document");
    }
    return Tina::Core::success();
}

// Staged beside the executable by tina_product_data_file(), so the build tree and an
// installed copy resolve it the same way. Absence is not an error: the caller falls back
// to the generated two-mesh document, which is what a build without the repo fixture got
// when this path was a compile-time definition.
[[nodiscard]] bool tryResolveCompletePbrFixture(std::filesystem::path& outPath) noexcept
{
    auto resolved = Tina::Core::applicationFilePath("assets/complete_pbr/complete_pbr.gltf");
    if (!resolved)
    {
        return false;
    }
    std::error_code ec;
    outPath = std::filesystem::path{*resolved};
    return std::filesystem::exists(outPath, ec) && !ec;
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("sample", "tina_sample_3d");
        writer.member("code", errorCodeName(error.code));
        writer.member("message", error.message);
        if (!error.context.empty())
        {
            writer.beginArrayMember("context");
            for (const Tina::Core::ErrorContext& context : error.context)
            {
                writer.beginObjectElement();
                writer.member("operation", context.operation);
                writer.member("detail", context.detail);
                writer.endObject();
            }
            writer.endArray();
        }
        writer.endObject();
    }
    std::cerr << '\n';
}

void printUsage()
{
    std::cerr
        << "tina_sample_3d [options]\n"
        << "  Product 3D gate: glTF/GLB cook -> Catalog -> GPU mesh/material bind -> Prefab/Scene/bgfx.\n"
        << "\n"
        << "  --frames=N              exit after N frames (default " << DefaultFrameCount << ")\n"
        << "  --frame-delay-ms=N      sleep N ms per frame (default 0)\n"
        << "  --width=N               initial logical window width (default 1280)\n"
        << "  --height=N              initial logical window height (default 720)\n"
        << "  --gltf=<path>           cook an external static .gltf/.glb scene (omit = built-in PBR fixture)\n"
        << "  --gltf <path>           same as --gltf=<path>\n"
        << "  --ui-theme=dark|light   select the initial retained UI theme (default dark)\n"
        << "  --ui-theme-demo         exercise initial -> alternate -> initial theme in UI phase\n"
        << "  --ibl=on|off            bind or leave unbound the uploaded EnvironmentMap (default on)\n"
        << "  --point-shadow=on|off   author the fixed PointLight shadow (default on)\n"
        << "  --skin-animation=on|off advance or pause the skin witness Animator3D (default on)\n"
        << "  --transparency=on|off   submit or omit the overlapping Blend witness (default on)\n"
        << "  --expect-pixel-fingerprint=<32 lowercase hex chars>\n"
        << "                           require an exact machine-local RGBA8 frame match\n"
        << "  --scene-rgb-output=<path> write the captured central RGB ROI as raw bytes\n"
        << "  --help, -h              print this help\n"
        << "\n"
        << "External path is opt-in. Runtime never parses glTF; only the cooker (cgltf) does.\n"
        << "Unsupported glTF features (multi-primitive mesh, Draco, morph, ...) fail with\n"
        << "structured JSON on stderr (status=error, code, message, optional context).\n";
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";
    constexpr std::string_view WidthPrefix = "--width=";
    constexpr std::string_view HeightPrefix = "--height=";
    constexpr std::string_view GltfPrefix = "--gltf=";
    constexpr std::string_view UiThemePrefix = "--ui-theme=";
    constexpr std::string_view ImageBasedLightingPrefix = "--ibl=";
    constexpr std::string_view PointLightShadowPrefix = "--point-shadow=";
    constexpr std::string_view SkinAnimationPrefix = "--skin-animation=";
    constexpr std::string_view TransparencyPrefix = "--transparency=";
    constexpr std::string_view PixelFingerprintPrefix = "--expect-pixel-fingerprint=";
    constexpr std::string_view SceneRgbOutputPrefix = "--scene-rgb-output=";
    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasWidth = false;
    bool hasHeight = false;
    bool hasGltf = false;
    bool hasUiTheme = false;
    bool hasImageBasedLightingMode = false;
    bool hasPointLightShadowMode = false;
    bool hasSkinAnimationMode = false;
    bool hasTransparencyMode = false;
    bool hasPixelFingerprint = false;
    bool hasSceneRgbOutput = false;

    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument == "--help" || argument == "-h")
        {
            options.help = true;
            return options;
        }
        if (argument.starts_with(FramesPrefix))
        {
            if (hasFrames || !Tina::Core::parseArgUnsigned(argument.substr(FramesPrefix.size()), options.targetFrameCount) ||
                options.targetFrameCount == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must appear once and be greater than zero");
            }
            hasFrames = true;
        }
        else if (argument.starts_with(DelayPrefix))
        {
            if (hasDelay || !Tina::Core::parseArgUnsigned(argument.substr(DelayPrefix.size()), options.frameDelayMilliseconds))
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must appear once and be unsigned");
            }
            hasDelay = true;
        }
        else if (argument.starts_with(WidthPrefix))
        {
            if (hasWidth || !Tina::Core::parseArgUnsigned(argument.substr(WidthPrefix.size()), options.windowLogicalWidth) ||
                options.windowLogicalWidth == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--width must appear once and be greater than zero");
            }
            hasWidth = true;
        }
        else if (argument.starts_with(HeightPrefix))
        {
            if (hasHeight || !Tina::Core::parseArgUnsigned(argument.substr(HeightPrefix.size()), options.windowLogicalHeight) ||
                options.windowLogicalHeight == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--height must appear once and be greater than zero");
            }
            hasHeight = true;
        }
        else if (argument.starts_with(GltfPrefix))
        {
            const std::string_view path = argument.substr(GltfPrefix.size());
            if (hasGltf || path.empty())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--gltf must appear once with a non-empty path");
            }
            options.gltfPath.assign(path);
            hasGltf = true;
        }
        else if (argument.starts_with(UiThemePrefix))
        {
            const std::string_view theme = argument.substr(UiThemePrefix.size());
            if (hasUiTheme || (theme != "dark" && theme != "light"))
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--ui-theme must appear once with dark or light");
            }
            options.initialUiTheme = theme == "light" ? Tina::Sample3D::Product3DUITheme::Light
                                                       : Tina::Sample3D::Product3DUITheme::Dark;
            hasUiTheme = true;
        }
        else if (argument == "--ui-theme-demo")
        {
            options.uiThemeDemo = true;
        }
        else if (argument.starts_with(ImageBasedLightingPrefix))
        {
            const std::string_view mode = argument.substr(ImageBasedLightingPrefix.size());
            if (hasImageBasedLightingMode || (mode != "on" && mode != "off"))
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--ibl must appear once with on or off");
            }
            options.imageBasedLightingMode =
                mode == "on" ? ImageBasedLightingMode::On : ImageBasedLightingMode::Off;
            hasImageBasedLightingMode = true;
        }
        else if (argument.starts_with(PointLightShadowPrefix))
        {
            const std::string_view mode = argument.substr(PointLightShadowPrefix.size());
            if (hasPointLightShadowMode || (mode != "on" && mode != "off"))
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--point-shadow must appear once with on or off");
            }
            options.pointLightShadowMode =
                mode == "on" ? PointLightShadowMode::On : PointLightShadowMode::Off;
            hasPointLightShadowMode = true;
        }
        else if (argument.starts_with(SkinAnimationPrefix))
        {
            const std::string_view mode = argument.substr(SkinAnimationPrefix.size());
            if (hasSkinAnimationMode || (mode != "on" && mode != "off"))
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--skin-animation must appear once with on or off");
            }
            options.skinAnimationMode =
                mode == "on" ? SkinAnimationMode::On : SkinAnimationMode::Off;
            hasSkinAnimationMode = true;
        }
        else if (argument.starts_with(TransparencyPrefix))
        {
            const std::string_view mode = argument.substr(TransparencyPrefix.size());
            if (hasTransparencyMode || (mode != "on" && mode != "off"))
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--transparency must appear once with on or off");
            }
            options.transparencyMode =
                mode == "on" ? TransparencyMode::On : TransparencyMode::Off;
            hasTransparencyMode = true;
        }
        else if (argument.starts_with(PixelFingerprintPrefix))
        {
            const std::string_view fingerprint = argument.substr(PixelFingerprintPrefix.size());
            const bool validHex = fingerprint.size() == 32 &&
                                  std::ranges::all_of(fingerprint, [](char character) noexcept {
                                      return (character >= '0' && character <= '9') ||
                                             (character >= 'a' && character <= 'f');
                                  });
            if (hasPixelFingerprint || !validHex)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--expect-pixel-fingerprint must appear once with 32 lowercase hex chars");
            }
            options.expectPixelFingerprint.assign(fingerprint);
            hasPixelFingerprint = true;
        }
        else if (argument.starts_with(SceneRgbOutputPrefix))
        {
            const std::string_view path = argument.substr(SceneRgbOutputPrefix.size());
            if (hasSceneRgbOutput || path.empty())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--scene-rgb-output must appear once with a non-empty path");
            }
            options.sceneRgbOutputPath.assign(path);
            hasSceneRgbOutput = true;
        }
        else if (argument == "--gltf")
        {
            if (hasGltf || index + 1 >= argumentCount)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--gltf must appear once with a non-empty path");
            }
            ++index;
            const std::string_view path{arguments[index]};
            if (path.empty())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--gltf must appear once with a non-empty path");
            }
            options.gltfPath.assign(path);
            hasGltf = true;
        }
        else
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Unsupported command-line argument"};
            error.addContext("parseOptions", argument);
            return Tina::Core::failure(std::move(error));
        }
    }
    if (options.uiThemeDemo && options.targetFrameCount < 3)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--ui-theme-demo requires --frames=3 or greater");
    }
    return options;
}

// path::string() is the active narrow code page on Windows, not UTF-8, and it is lossy for
// non-ASCII. The asset API this feeds expects UTF-8. Separators are left native so the paths this
// prints keep their current form.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// Stable product AssetIds (16-bit slot index in bytes[14..15], no 8-bit wrap).
// mesh: 0x10 + 2*slot, material: 0x11 + 2*slot so mesh < mat and unique for slot < 32760.
// Prefab: 0xF000. Texture ids from GltfCook are left unchanged by rewrite (only mesh/mat/prefab).
[[nodiscard]] Tina::Core::AssetId productMeshIdForSlot(u32 slot) noexcept
{
    const u32 index = 0x10U + slot * 2U;
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(static_cast<unsigned char>((index >> 8) & 0xFFU));
    bytes[15] = static_cast<std::byte>(static_cast<unsigned char>(index & 0xFFU));
    return *Tina::Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Tina::Core::AssetId productMaterialIdForSlot(u32 slot) noexcept
{
    const u32 index = 0x11U + slot * 2U;
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(static_cast<unsigned char>((index >> 8) & 0xFFU));
    bytes[15] = static_cast<std::byte>(static_cast<unsigned char>(index & 0xFFU));
    return *Tina::Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Tina::Core::Status writeFallbackGltfFixture(const std::filesystem::path& workRoot,
                                                          std::filesystem::path& outGltfPath)
{
    {
        const auto png = productTinyRedPng();
        std::ofstream out(workRoot / "tex.png", std::ios::binary);
        if (!out.good())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io, "failed to write temporary texture fixture");
        }
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    outGltfPath = workRoot / "product_two_mesh.gltf";
    {
        std::ofstream out(outGltfPath, std::ios::binary);
        if (!out.good())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io, "failed to write temporary glTF fixture");
        }
        out << productTwoMeshGltfJsonFallback();
    }
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status prepareCookedProductAssets(Product3DResources& resources,
                                                            LifecycleCounters& counters,
                                                            const SampleOptions& options)
{
    // Beside the executable, not in %TEMP%. The generated glTF fixtures and the cooked catalog
    // are the same run's intermediates, so they share one root that the next run wipes; a
    // published catalog only ever writes manifest.tmnft and objects/, so the fixture files
    // cannot collide with it.
    auto workRoot = Tina::Sample::prepareApplicationContentDirectory("content");
    if (!workRoot)
    {
        return Tina::Core::failure(std::move(workRoot.error()));
    }
    resources.workRoot = std::move(*workRoot);
    resources.catalogRoot = resources.workRoot / "catalog";
    resources.externalGltf = !options.gltfPath.empty();
    resources.completePbrFixture = false;
    resources.gltfSourcePath = options.gltfPath;

    std::filesystem::path gltfPath;
    std::error_code ec;
    if (resources.externalGltf)
    {
        gltfPath = std::filesystem::path{options.gltfPath};
        if (!std::filesystem::exists(gltfPath, ec) || ec)
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::NotFound, "external glTF/GLB file not found"};
            error.addContext("prepareCookedProductAssets", options.gltfPath);
            return Tina::Core::failure(std::move(error));
        }
        const auto extension = gltfPath.extension().string();
        const bool gltfExt = extension == ".gltf" || extension == ".GLTF";
        const bool glbExt = extension == ".glb" || extension == ".GLB";
        if (!gltfExt && !glbExt)
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "external model path must end with .gltf or .glb"};
            error.addContext("prepareCookedProductAssets", options.gltfPath);
            return Tina::Core::failure(std::move(error));
        }
    }
    else if (tryResolveCompletePbrFixture(gltfPath))
    {
        resources.completePbrFixture = true;
        resources.gltfSourcePath = toUtf8(gltfPath);
    }
    else
    {
        if (auto status = writeFallbackGltfFixture(resources.workRoot, gltfPath); !status)
        {
            return status;
        }
    }
    counters.completePbrFixture = resources.completePbrFixture;

    // Cook via GltfCook (cgltf PRIVATE). Unsupported features fail with structured Asset errors.
    auto request = Tina::Asset::cookGltfFileToCatalogRequest(
        toUtf8(gltfPath), Tina::AssetFormat::TargetPlatform::WindowsX64,
        Tina::Asset::GltfCookIds{});
    if (!request)
    {
        Tina::Core::Error error = std::move(request.error());
        error.addContext("cookGltfFileToCatalogRequest", toUtf8(gltfPath));
        if (resources.externalGltf)
        {
            error.addContext("externalGltf", options.gltfPath);
        }
        return Tina::Core::failure(std::move(error));
    }
    counters.gltfCooked = true;

    Tina::Core::AssetId cookedPrefabId{};
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == Tina::AssetFormat::AssetKind::Prefab)
        {
            cookedPrefabId = asset.assetId;
            break;
        }
    }
    if (!cookedPrefabId)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "primary glTF cook did not yield a Prefab");
    }
    if (std::ranges::any_of(request->assets, [](const Tina::Asset::CatalogCookAssetSpec& asset) {
            return asset.assetKind == Tina::AssetFormat::AssetKind::SkinnedMesh;
        }))
    {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "the product --gltf scene must be static while the dedicated skin witness owns the Animator3D mapping");
    }

    std::filesystem::path skinnedGltfPath;
    if (auto status = writeSkinnedGltfFixture(resources.workRoot, skinnedGltfPath); !status)
    {
        return status;
    }
    auto skinnedRequest = Tina::Asset::cookGltfFileToCatalogRequest(
        toUtf8(skinnedGltfPath), Tina::AssetFormat::TargetPlatform::WindowsX64,
        Tina::Asset::GltfCookIds{});
    if (!skinnedRequest)
    {
        Tina::Core::Error error = std::move(skinnedRequest.error());
        error.addContext("cookProductSkinnedFixture", toUtf8(skinnedGltfPath));
        return Tina::Core::failure(std::move(error));
    }
    Tina::Core::AssetId cookedSkinnedPrefabId{};
    Tina::Core::AssetId cookedAnimationClipId{};
    Tina::Core::AssetId cookedWitnessSkinnedMeshId{};
    u32 witnessSkinnedMeshCount = 0;
    for (const auto& asset : skinnedRequest->assets)
    {
        if (asset.assetKind == Tina::AssetFormat::AssetKind::SkinnedMesh)
        {
            ++witnessSkinnedMeshCount;
            cookedWitnessSkinnedMeshId = asset.assetId;
        }
        else if (asset.assetKind == Tina::AssetFormat::AssetKind::AnimationClip3D)
        {
            cookedAnimationClipId = asset.assetId;
        }
        else if (asset.assetKind == Tina::AssetFormat::AssetKind::Prefab)
        {
            cookedSkinnedPrefabId = asset.assetId;
        }
    }
    if (witnessSkinnedMeshCount != 1U || !cookedAnimationClipId || !cookedSkinnedPrefabId)
    {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "product skin fixture must cook one SkinnedMesh, one AnimationClip3D, and one Prefab");
    }
    request->assets.insert(request->assets.end(),
                           std::make_move_iterator(skinnedRequest->assets.begin()),
                           std::make_move_iterator(skinnedRequest->assets.end()));

    std::array<Tina::Core::AssetId, MaxProductMeshSlots> cookedMeshIds{};
    std::array<Tina::AssetFormat::AssetKind, MaxProductMeshSlots> cookedMeshKinds{};
    std::array<Tina::Core::AssetId, MaxProductMeshSlots> cookedMaterialIds{};
    u32 meshIdCount = 0;
    u32 materialIdCount = 0;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == Tina::AssetFormat::AssetKind::StaticMesh ||
            asset.assetKind == Tina::AssetFormat::AssetKind::SkinnedMesh)
        {
            if (meshIdCount >= MaxProductMeshSlots)
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::CapacityExceeded,
                                        "glTF cook produced more mesh assets than product slot cap"};
                error.addContext("maxProductMeshSlots", std::to_string(MaxProductMeshSlots));
                return Tina::Core::failure(std::move(error));
            }
            cookedMeshIds[meshIdCount] = asset.assetId;
            cookedMeshKinds[meshIdCount++] = asset.assetKind;
        }
        else if (asset.assetKind == Tina::AssetFormat::AssetKind::Material)
        {
            if (materialIdCount >= MaxProductMeshSlots)
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::CapacityExceeded,
                                        "glTF cook produced more Material assets than product slot cap"};
                error.addContext("maxProductMeshSlots", std::to_string(MaxProductMeshSlots));
                return Tina::Core::failure(std::move(error));
            }
            cookedMaterialIds[materialIdCount++] = asset.assetId;
        }
    }
    if (meshIdCount == 0 || materialIdCount == 0 || !cookedPrefabId)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "glTF cook did not yield at least one mesh, one material, and one prefab");
    }
    if (meshIdCount != materialIdCount)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "glTF cook mesh/material count mismatch"};
        error.addContext("meshCount", std::to_string(meshIdCount));
        error.addContext("materialCount", std::to_string(materialIdCount));
        return Tina::Core::failure(std::move(error));
    }
    // Built-in product gate keeps its existing static/PBR mesh set and adds one skin witness.
    if (!resources.externalGltf && meshIdCount < 3U)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "built-in product fixture must cook static meshes plus a skin witness");
    }

    const u32 slotCount = meshIdCount;
    auto assetSystem = Tina::Asset::AssetSystem::Create({
        .storeCapacity = static_cast<Tina::usize>(slotCount) * 5U + 4U,
        .memoryResource = &resources.memory,
    });
    if (!assetSystem)
    {
        return Tina::Core::failure(std::move(assetSystem.error()));
    }
    resources.assetSystem.emplace(std::move(*assetSystem));
    std::array<Tina::Core::AssetId, MaxProductMeshSlots> productMeshIds{};
    std::array<Tina::Core::AssetId, MaxProductMeshSlots> productMaterialIds{};
    for (u32 slot = 0; slot < slotCount; ++slot)
    {
        productMeshIds[slot] = productMeshIdForSlot(slot);
        productMaterialIds[slot] = productMaterialIdForSlot(slot);
    }
    resources.prefabId = *Tina::Core::AssetId::fromBytes(prefabIdBytes());
    resources.skinnedPrefabId = *Tina::Core::AssetId::fromBytes(skinnedPrefabIdBytes());
    resources.animationClipId = *Tina::Core::AssetId::fromBytes(animationClipIdBytes());
    for (u32 slot = 0; slot < slotCount; ++slot)
    {
        if (cookedMeshIds[slot] == cookedWitnessSkinnedMeshId)
        {
            resources.witnessSkinnedMeshSlot = slot;
            break;
        }
    }
    if (resources.witnessSkinnedMeshSlot >= slotCount)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "product skin witness mesh slot could not be resolved");
    }

    const auto rewriteId = [&](Tina::Core::AssetId id) -> Tina::Core::AssetId {
        for (u32 slot = 0; slot < slotCount; ++slot)
        {
            if (id == cookedMeshIds[slot])
            {
                return productMeshIds[slot];
            }
            if (id == cookedMaterialIds[slot])
            {
                return productMaterialIds[slot];
            }
        }
        if (id == cookedPrefabId)
        {
            return resources.prefabId;
        }
        if (id == cookedSkinnedPrefabId)
        {
            return resources.skinnedPrefabId;
        }
        if (id == cookedAnimationClipId)
        {
            return resources.animationClipId;
        }
        return id;
    };

    const auto rewritePrefabPayload = [&rewriteId](std::span<const std::byte> payload)
        -> Tina::Core::Result<std::vector<std::byte>> {
        std::vector<Tina::AssetFormat::PrefabNodeView> sourceNodes;
        auto source = Tina::AssetFormat::parsePrefabPayload(payload, sourceNodes);
        if (!source)
        {
            return Tina::Core::failure(std::move(source.error()));
        }

        std::vector<Tina::AssetFormat::PrefabNodeDesc> rewrittenNodes;
        rewrittenNodes.reserve(source->nodes.size());
        for (const Tina::AssetFormat::PrefabNodeView& node : source->nodes)
        {
            rewrittenNodes.push_back(Tina::AssetFormat::PrefabNodeDesc{
                .stableNodeId = node.stableNodeId,
                .parentIndex = node.parentIndex,
                .nodeKind = node.nodeKind,
                .name = node.name,
                .positionX = node.positionX,
                .positionY = node.positionY,
                .positionZ = node.positionZ,
                .rotationX = node.rotationX,
                .rotationY = node.rotationY,
                .rotationZ = node.rotationZ,
                .rotationW = node.rotationW,
                .scaleX = node.scaleX,
                .scaleY = node.scaleY,
                .scaleZ = node.scaleZ,
                .meshId = node.hasMesh ? rewriteId(node.meshId) : Tina::Core::AssetId{},
                .materialId =
                    node.hasMaterial ? rewriteId(node.materialId) : Tina::Core::AssetId{},
                .visible = node.visible,
                .camera = node.camera,
                .light = node.light,
            });
        }
        return Tina::AssetFormat::writePrefabPayloadBytes(
            Tina::AssetFormat::PrefabPayloadDesc{.nodes = rewrittenNodes});
    };

    for (auto& asset : request->assets)
    {
        if (asset.assetKind == Tina::AssetFormat::AssetKind::Prefab)
        {
            auto rewrittenPayload = rewritePrefabPayload(asset.payload);
            if (!rewrittenPayload)
            {
                Tina::Core::Error error = std::move(rewrittenPayload.error());
                error.addContext("prepareCookedProductAssets", "rewritePrefabPayload");
                return Tina::Core::failure(std::move(error));
            }
            asset.payload = std::move(*rewrittenPayload);
        }
        asset.assetId = rewriteId(asset.assetId);
        for (auto& dep : asset.dependencies)
        {
            dep.assetId = rewriteId(dep.assetId);
        }
        // Rewrite can disorder dependency streams; CatalogCook requires strictly increasing ids.
        std::sort(asset.dependencies.begin(), asset.dependencies.end(),
                  [](const Tina::AssetFormat::CookedAssetWriteDependency& a,
                     const Tina::AssetFormat::CookedAssetWriteDependency& b) {
                      return a.assetId < b.assetId;
                  });
        asset.dependencies.erase(std::unique(asset.dependencies.begin(), asset.dependencies.end(),
                                             [](const Tina::AssetFormat::CookedAssetWriteDependency& a,
                                                const Tina::AssetFormat::CookedAssetWriteDependency& b) {
                                                 return a.assetId == b.assetId;
                                             }),
                                 asset.dependencies.end());
    }

    resources.transparentMaterialId =
        *Tina::Core::AssetId::fromBytes(transparentMaterialIdBytes());
    const bool transparentMaterialIdCollision =
        std::ranges::any_of(request->assets, [&resources](const Tina::Asset::CatalogCookAssetSpec& asset) {
            return asset.assetId == resources.transparentMaterialId;
        });
    if (transparentMaterialIdCollision)
    {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::AlreadyExists,
            "product transparent Material AssetId collides with cooked glTF output");
    }
    auto transparentMaterialPayload = Tina::AssetFormat::writeMaterialPayloadBytes(
        Tina::AssetFormat::MaterialPayloadDesc{
            .baseColorR = 1.0F,
            .baseColorG = 1.0F,
            .baseColorB = 1.0F,
            .baseColorA = 0.58F,
            .metallicFactor = 0.05F,
            .roughnessFactor = 0.28F,
            .doubleSided = true,
            .alphaMode = Tina::AssetFormat::MaterialAlphaMode::Blend,
        });
    if (!transparentMaterialPayload)
    {
        return Tina::Core::failure(std::move(transparentMaterialPayload.error()));
    }
    request->assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Material,
        .assetId = resources.transparentMaterialId,
        .assetTypeVersion = Tina::AssetFormat::MaterialWire::SchemaVersion,
        .payload = std::move(*transparentMaterialPayload),
    });

    resources.environmentMapId = Tina::Sample3D::productEnvironmentMapAssetId();
    const bool environmentIdCollision =
        std::ranges::any_of(request->assets, [&resources](const Tina::Asset::CatalogCookAssetSpec& asset) {
            return asset.assetId == resources.environmentMapId;
        });
    if (environmentIdCollision)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::AlreadyExists,
                                   "product EnvironmentMap AssetId collides with cooked glTF output");
    }
    auto environmentPayload = Tina::Sample3D::makeProductEnvironmentMapPayload();
    if (!environmentPayload)
    {
        return Tina::Core::failure(std::move(environmentPayload.error()));
    }
    request->assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::EnvironmentMap,
        .assetId = resources.environmentMapId,
        .assetTypeVersion = Tina::AssetFormat::EnvironmentMapWire::SchemaVersion,
        .payload = std::move(*environmentPayload),
    });

    if (auto status = Tina::Asset::cookAndPublishCatalogPackage(toUtf8(resources.catalogRoot), *request); !status)
    {
        Tina::Core::Error error = std::move(status.error());
        error.addContext("cookAndPublishCatalogPackage", toUtf8(resources.catalogRoot));
        return Tina::Core::failure(std::move(error));
    }
    ++counters.catalogCooked;

    // Prefab may list mesh+material for every node; spheres grids need hundreds of deps.
    const u32 maxCatalogEntries = 16U + slotCount * 8U;
    const u32 maxDepsPerAsset =
        (std::max)(16U, slotCount * 2U + 8U); // mesh+mat pairs + headroom for material tex deps
    const u32 maxDepsTotal = (std::max)(maxCatalogEntries, maxDepsPerAsset * 4U);
    Tina::Asset::CatalogPackageOpenConfig openConfig{
        .manifest =
            Tina::Asset::CatalogFileLoadConfig{
                .catalog =
                    Tina::Asset::CatalogConfig{
                        .maxEntries = maxCatalogEntries,
                        .maxDependencies = maxDepsTotal,
                        .maxDependenciesPerAsset = maxDepsPerAsset,
                        .memoryResource = &resources.memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            Tina::Asset::CatalogPackageValidationConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = Tina::Asset::openCatalogPackage(toUtf8(resources.catalogRoot), openConfig);
    if (!catalog)
    {
        return Tina::Core::failure(std::move(catalog.error()));
    }

    auto environmentMapAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.environmentMapId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!environmentMapAsset)
    {
        return Tina::Core::failure(std::move(environmentMapAsset.error()));
    }
    auto environmentMap = Tina::Asset::parseEnvironmentMapFromCooked(*environmentMapAsset);
    if (!environmentMap)
    {
        return Tina::Core::failure(std::move(environmentMap.error()));
    }
    if (environmentMap->diffuseFaceSize != Tina::Sample3D::ProductEnvironmentDiffuseFaceSize ||
        environmentMap->specularFaceSize != Tina::Sample3D::ProductEnvironmentSpecularFaceSize ||
        environmentMap->specularMipCount != Tina::Sample3D::ProductEnvironmentSpecularMipCount ||
        environmentMap->brdfWidth != Tina::Sample3D::ProductEnvironmentBrdfSize ||
        environmentMap->brdfHeight != Tina::Sample3D::ProductEnvironmentBrdfSize)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "cooked product EnvironmentMap dimensions changed during publication");
    }
    resources.environmentMapAsset.emplace(std::move(*environmentMapAsset));
    counters.cookedEnvironmentMap = true;

    for (u32 slot = 0; slot < slotCount; ++slot)
    {
        ProductMeshSlot& productMesh = resources.meshes[slot];
        productMesh.meshId = productMeshIds[slot];
        productMesh.materialId = productMaterialIds[slot];
        productMesh.meshKind = cookedMeshKinds[slot];

        auto meshAsset = Tina::Asset::loadCookedAssetFromCatalog(
            toUtf8(resources.catalogRoot), *catalog, productMesh.meshId,
            Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
        if (!meshAsset)
        {
            return Tina::Core::failure(std::move(meshAsset.error()));
        }
        if (productMesh.meshKind == Tina::AssetFormat::AssetKind::StaticMesh)
        {
            auto meshView = Tina::Asset::parseStaticMeshFromCooked(*meshAsset);
            if (!meshView)
            {
                return Tina::Core::failure(std::move(meshView.error()));
            }
            if (meshView->vertexCount == 0 || meshView->indexCount == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "cooked glTF StaticMesh is empty");
            }
            productMesh.meshBoundsRadius = meshView->boundsRadius > 0.0F ? meshView->boundsRadius : 1.75F;
            ++resources.staticMeshSlotCount;
        }
        else if (productMesh.meshKind == Tina::AssetFormat::AssetKind::SkinnedMesh)
        {
            auto meshView = Tina::Asset::parseSkinnedMeshFromCooked(*meshAsset);
            if (!meshView)
            {
                return Tina::Core::failure(std::move(meshView.error()));
            }
            if (meshView->empty())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "cooked glTF SkinnedMesh is empty");
            }
            productMesh.meshBoundsRadius = meshView->boundsRadius > 0.0F ? meshView->boundsRadius : 1.75F;
            ++resources.skinnedMeshSlotCount;
        }
        else
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "product mesh slot has an unsupported AssetKind");
        }
        auto publishedMesh = resources.assetSystem->store().publish(std::move(*meshAsset));
        if (!publishedMesh)
        {
            return Tina::Core::failure(std::move(publishedMesh.error()));
        }
        productMesh.meshAsset = *publishedMesh;
        ++counters.meshAssetHandlesPublished;

        auto materialAsset = Tina::Asset::loadCookedAssetFromCatalog(
            toUtf8(resources.catalogRoot), *catalog, productMesh.materialId,
            Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
        if (!materialAsset)
        {
            return Tina::Core::failure(std::move(materialAsset.error()));
        }
        auto material = Tina::Asset::parseMaterialFromCooked(*materialAsset);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }
        productMesh.materialColor = Tina::Render::RenderLinearColor{
            .red = material->baseColorR,
            .green = material->baseColorG,
            .blue = material->baseColorB,
            .alpha = material->baseColorA,
        };
        productMesh.alphaMode = material->alphaMode == Tina::AssetFormat::MaterialAlphaMode::Blend
            ? Tina::Render::Mesh3DAlphaMode::Blend
            : Tina::Render::Mesh3DAlphaMode::Opaque;
        if (productMesh.alphaMode == Tina::Render::Mesh3DAlphaMode::Blend)
        {
            ++resources.blendMaterialCount;
        }
        productMesh.metallicFactor = material->metallicFactor;
        productMesh.roughnessFactor = material->roughnessFactor;
        // Cooked Material v2 deps are ordered: baseColor, metallicRoughness, normal (flag order).
        u32 depIndex = 0;
        const auto loadTextureDep = [&](bool present, Tina::Core::AssetId& outId,
                                        Tina::Asset::AssetHandle& outHandle,
                                        const char* missingLabel) -> Tina::Core::Status {
            if (!present)
            {
                return Tina::Core::success();
            }
            if (depIndex >= materialAsset->header().dependencyCount)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, missingLabel);
            }
            auto textureDep = materialAsset->dependency(depIndex++);
            if (!textureDep || textureDep->expectedKind != Tina::AssetFormat::AssetKind::Texture2D)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "product material texture dependency is not Texture2D");
            }
            outId = textureDep->assetId;
            for (u32 textureIndex = 0; textureIndex < resources.textureAssetCount; ++textureIndex)
            {
                if (resources.textures[textureIndex].id == outId)
                {
                    outHandle = resources.textures[textureIndex].handle;
                    return Tina::Core::success();
                }
            }
            if (resources.textureAssetCount >= resources.textures.size())
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::CapacityExceeded,
                    "product texture dependency count exceeds the fixed texture table");
            }
            auto textureAsset = Tina::Asset::loadCookedAssetFromCatalog(
                toUtf8(resources.catalogRoot), *catalog, outId,
                Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
            if (!textureAsset)
            {
                return Tina::Core::failure(std::move(textureAsset.error()));
            }
            auto publishedTexture = resources.assetSystem->store().publish(std::move(*textureAsset));
            if (!publishedTexture)
            {
                return Tina::Core::failure(std::move(publishedTexture.error()));
            }
            outHandle = *publishedTexture;
            resources.textures[resources.textureAssetCount++] = ProductTextureAsset{
                .id = outId,
                .handle = outHandle,
            };
            return Tina::Core::success();
        };
        if (auto status = loadTextureDep(material->hasBaseColorTexture, productMesh.textureId,
                                         productMesh.textureAsset, "missing baseColor texture dep");
            !status)
        {
            return status;
        }
        if (!resources.externalGltf &&
            productMesh.meshKind == Tina::AssetFormat::AssetKind::StaticMesh &&
            !material->hasBaseColorTexture)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "product material missing baseColorTexture dependency");
        }
        if (auto status = loadTextureDep(material->hasMetallicRoughnessTexture,
                                         productMesh.metallicRoughnessTextureId,
                                         productMesh.metallicRoughnessTextureAsset,
                                         "missing metallicRoughness texture dep");
            !status)
        {
            return status;
        }
        if (auto status = loadTextureDep(material->hasNormalTexture, productMesh.normalTextureId,
                                         productMesh.normalTextureAsset, "missing normal texture dep");
            !status)
        {
            return status;
        }
        auto publishedMaterial = resources.assetSystem->store().publish(std::move(*materialAsset));
        if (!publishedMaterial)
        {
            return Tina::Core::failure(std::move(publishedMaterial.error()));
        }
        productMesh.materialAsset = *publishedMaterial;
        ++counters.materialAssetHandlesPublished;
        ++counters.materialsLoaded;
    }
    resources.meshSlotCount = slotCount;

    auto transparentMaterialAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.transparentMaterialId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!transparentMaterialAsset)
    {
        return Tina::Core::failure(std::move(transparentMaterialAsset.error()));
    }
    auto transparentMaterial = Tina::Asset::parseMaterialFromCooked(*transparentMaterialAsset);
    if (!transparentMaterial)
    {
        return Tina::Core::failure(std::move(transparentMaterial.error()));
    }
    if (transparentMaterial->alphaMode != Tina::AssetFormat::MaterialAlphaMode::Blend)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "product transparent Material did not retain Blend alphaMode");
    }
    auto publishedTransparentMaterial =
        resources.assetSystem->store().publish(std::move(*transparentMaterialAsset));
    if (!publishedTransparentMaterial)
    {
        return Tina::Core::failure(std::move(publishedTransparentMaterial.error()));
    }
    resources.transparentMaterialAsset = *publishedTransparentMaterial;
    ++resources.blendMaterialCount;
    ++counters.materialAssetHandlesPublished;
    ++counters.materialsLoaded;

    auto animationClipAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.animationClipId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!animationClipAsset)
    {
        return Tina::Core::failure(std::move(animationClipAsset.error()));
    }
    auto animationClip = Tina::Asset::parseAnimationClip3DFromCooked(*animationClipAsset);
    if (!animationClip)
    {
        return Tina::Core::failure(std::move(animationClip.error()));
    }
    auto publishedAnimationClip =
        resources.assetSystem->store().publish(std::move(*animationClipAsset));
    if (!publishedAnimationClip)
    {
        return Tina::Core::failure(std::move(publishedAnimationClip.error()));
    }
    resources.animationClipAsset = *publishedAnimationClip;
    ++counters.animationClipAssetHandlesPublished;

    auto prefabAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.prefabId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!prefabAsset)
    {
        return Tina::Core::failure(std::move(prefabAsset.error()));
    }
    auto prefab = Tina::Asset::parsePrefabFromCooked(*prefabAsset);
    if (!prefab)
    {
        return Tina::Core::failure(std::move(prefab.error()));
    }
    counters.prefabNodes = prefab->nodes.size();
    if (counters.prefabNodes == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked Prefab has no nodes");
    }
    if (!resources.externalGltf && counters.prefabNodes < 2U)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "cooked Prefab must contain at least two meshed nodes");
    }
    auto publishedPrefab = resources.assetSystem->store().publish(std::move(*prefabAsset));
    if (!publishedPrefab)
    {
        return Tina::Core::failure(std::move(publishedPrefab.error()));
    }
    resources.prefabAsset = *publishedPrefab;

    auto skinnedPrefabAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.skinnedPrefabId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!skinnedPrefabAsset)
    {
        return Tina::Core::failure(std::move(skinnedPrefabAsset.error()));
    }
    auto skinnedPrefab = Tina::Asset::parsePrefabFromCooked(*skinnedPrefabAsset);
    if (!skinnedPrefab)
    {
        return Tina::Core::failure(std::move(skinnedPrefab.error()));
    }
    auto publishedSkinnedPrefab =
        resources.assetSystem->store().publish(std::move(*skinnedPrefabAsset));
    if (!publishedSkinnedPrefab)
    {
        return Tina::Core::failure(std::move(publishedSkinnedPrefab.error()));
    }
    resources.skinnedPrefabAsset = *publishedSkinnedPrefab;
    resources.hasDedicatedSkinnedPrefab = true;
    ++counters.skinnedPrefabAssetHandlesPublished;
    return Tina::Core::success();
}

class Product3DState final : public Tina::IGameState {
  public:
    Product3DState(SampleOptions options, LifecycleCounters& counters, Product3DResources& resources,
                   DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture), ui_(counters.ui)
    {
    }

    ~Product3DState() override
    {
        skinnedAnimator_.reset();
        skinnedMeshEntity_ = {};
        skinnedPrefabEntities_.clear();
        transparentWitnessEntities_.clear();
        world_.reset();
        prefabEntities_.clear();
        releaseRuntimeOnlyAssets();
        releaseProductGpuResources();
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        // Host-lifetime borrow. releaseProductGpuResources() runs from onExit and from
        // the destructor, neither of which has a phase context, so the address is kept.
        device_ = &context.renderDevice();
        const Tina::Platform::LogicalExtent initialLogicalExtent =
            context.engineConfig().primaryWindow.initialLogicalExtent;
        logicalExtent_ = initialLogicalExtent;
        framebufferExtent_ = {
            .width = initialLogicalExtent.width,
            .height = initialLogicalExtent.height,
        };
        counters_->logicalPixelWidth = logicalExtent_.width;
        counters_->logicalPixelHeight = logicalExtent_.height;
        counters_->framebufferPixelWidth = framebufferExtent_.width;
        counters_->framebufferPixelHeight = framebufferExtent_.height;

        auto platformEvents = context.platformEventSubscriptions().subscribe(
            [this](const Tina::PlatformEventNotification& notification) {
                if (!std::holds_alternative<Tina::Platform::WindowMetricsChangedEvent>(
                        notification.event().payload))
                {
                    return;
                }
                const auto* metrics = notification.primaryWindowMetrics();
                if (metrics == nullptr)
                {
                    return;
                }
                ++counters_->windowMetricsEvents;
                logicalExtent_ = metrics->logicalExtent;
                framebufferExtent_ = metrics->framebufferExtent;
                counters_->logicalPixelWidth = logicalExtent_.width;
                counters_->logicalPixelHeight = logicalExtent_.height;
                counters_->framebufferPixelWidth = framebufferExtent_.width;
                counters_->framebufferPixelHeight = framebufferExtent_.height;
            });
        if (!platformEvents)
        {
            return Tina::Core::failure(std::move(platformEvents.error()));
        }
        platformEvents_.emplace(std::move(*platformEvents));

        Tina::Render::IRenderDevice* const device = device_;
        if (resources_->meshSlotCount == 0 || !resources_->assetSystem.has_value())
        {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "AssetSystem or product mesh slots missing");
        }
        auto registry = Tina::Asset::Mesh3DBindingRegistry::Create(
            *resources_->assetSystem,
            *device,
            Tina::Asset::Mesh3DBindingRegistryConfig{
                .meshCapacity = resources_->meshSlotCount,
                .materialCapacity = resources_->meshSlotCount + 1U,
                .textureCapacity = (std::max)(1U, resources_->textureAssetCount),
                .memoryResource = &resources_->memory,
            });
        if (!registry)
        {
            return Tina::Core::failure(std::move(registry.error()));
        }
        mesh3DBindings_.emplace(std::move(*registry));
        // EngineHost discards a failed onEnter candidate without calling onExit.
        // Keep GPU resource ownership transactional until the state is fully entered.
        auto gpuRollback = Tina::Core::makeScopeExit([this]() noexcept {
            skinnedAnimator_.reset();
            skinnedMeshEntity_ = {};
            skinnedPrefabEntities_.clear();
            transparentWitnessEntities_.clear();
            world_.reset();
            prefabEntities_.clear();
            releaseProductGpuResources();
        });

        if (resources_->witnessSkinnedMeshSlot >= resources_->meshSlotCount ||
            !resources_->animationClipAsset)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "product skin witness assets are missing");
        }
        const ProductMeshSlot& witnessMesh =
            resources_->meshes[resources_->witnessSkinnedMeshSlot];
        const Tina::Asset::CookedAssetFile* witnessMeshFile =
            resources_->assetSystem->tryGet(witnessMesh.meshAsset);
        const Tina::Asset::CookedAssetFile* animationClipFile =
            resources_->assetSystem->tryGet(resources_->animationClipAsset);
        if (witnessMesh.meshKind != Tina::AssetFormat::AssetKind::SkinnedMesh ||
            witnessMeshFile == nullptr || animationClipFile == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "product skin witness payloads are unavailable");
        }
        auto witnessMeshView = Tina::Asset::parseSkinnedMeshFromCooked(*witnessMeshFile);
        if (!witnessMeshView)
        {
            return Tina::Core::failure(std::move(witnessMeshView.error()));
        }
        auto animationClipView = Tina::Asset::parseAnimationClip3DFromCooked(*animationClipFile);
        if (!animationClipView)
        {
            return Tina::Core::failure(std::move(animationClipView.error()));
        }
        auto animator = Tina::Scene::Animator3D::Create(
            *witnessMeshView, *animationClipView, resources_->memory);
        if (!animator)
        {
            return Tina::Core::failure(std::move(animator.error()));
        }
        skinnedAnimator_.emplace(std::move(*animator));
        if (options_.skinAnimationMode == SkinAnimationMode::Off)
        {
            skinnedAnimator_->pause();
        }
        counters_->animatorJointCount = skinnedAnimator_->jointCount();

        if (!resources_->environmentMapAsset.has_value())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "cooked product EnvironmentMap is missing");
        }
        auto uploadedEnvironment =
            Tina::Asset::uploadEnvironmentMapFromCooked(*device, *resources_->environmentMapAsset);
        if (!uploadedEnvironment)
        {
            return Tina::Core::failure(std::move(uploadedEnvironment.error()));
        }
        environmentMap_ = *uploadedEnvironment;
        if (options_.imageBasedLightingMode == ImageBasedLightingMode::On)
        {
            if (auto status = device->setMesh3DImageBasedLighting(
                    Tina::Render::Mesh3DImageBasedLightingDesc{
                        .environmentMap = environmentMap_,
                        .intensity = Tina::Sample3D::ProductEnvironmentIntensity,
                        .rotationRadians = Tina::Sample3D::ProductEnvironmentRotationRadians,
                    });
                !status)
            {
                return status;
            }
            imageBasedLightingBound_ = true;
            counters_->imageBasedLightingConfigured = true;
        }

        for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
        {
            ProductMeshSlot& productMesh = resources_->meshes[slot];
            const Tina::Asset::CookedAssetFile* meshFile =
                resources_->assetSystem->tryGet(productMesh.meshAsset);
            if (meshFile == nullptr)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "cooked mesh missing for product slot");
            }
            Tina::Core::Result<Tina::Render::GpuMeshId> mesh =
                productMesh.meshKind == Tina::AssetFormat::AssetKind::SkinnedMesh
                    ? Tina::Asset::uploadSkinnedMeshFromCooked(*device, *meshFile)
                    : Tina::Asset::uploadStaticMeshFromCooked(*device, *meshFile);
            if (!mesh)
            {
                return Tina::Core::failure(std::move(mesh.error()));
            }
            Tina::Render::GpuMeshId gpuMesh = *mesh;
            auto meshCleanup = Tina::Core::makeScopeExit([device, &gpuMesh]() noexcept {
                if (gpuMesh)
                {
                    (void)device->destroyGpuMesh(gpuMesh);
                }
            });
            auto meshBinding = productMesh.meshKind == Tina::AssetFormat::AssetKind::SkinnedMesh
                ? mesh3DBindings_->registerSkinnedMeshBinding(productMesh.meshAsset, gpuMesh)
                : mesh3DBindings_->registerMeshBinding(productMesh.meshAsset, gpuMesh);
            if (!meshBinding)
            {
                return Tina::Core::failure(std::move(meshBinding.error()));
            }
            meshCleanup.release();
            ++counters_->meshBindingsRegistered;
            ++counters_->meshesUploaded;

            const auto registerTextureOwner =
                [this, device](Tina::Asset::AssetHandle textureAsset,
                               const char* missingMessage) -> Tina::Core::Status {
                if (!textureAsset || mesh3DBindings_->hasMaterialTexture(textureAsset))
                {
                    return Tina::Core::success();
                }
                const Tina::Asset::CookedAssetFile* textureFile =
                    resources_->assetSystem->tryGet(textureAsset);
                if (textureFile == nullptr)
                {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, missingMessage);
                }
                auto uploaded = Tina::Asset::uploadTexture2DFromCooked(*device, *textureFile);
                if (!uploaded)
                {
                    return Tina::Core::failure(std::move(uploaded.error()));
                }
                Tina::Render::GpuTextureId gpuTexture = *uploaded;
                auto textureCleanup = Tina::Core::makeScopeExit([device, &gpuTexture]() noexcept {
                    if (gpuTexture)
                    {
                        (void)device->destroyTexture2D(gpuTexture);
                    }
                });
                if (auto status = mesh3DBindings_->registerMaterialTexture(textureAsset, gpuTexture); !status)
                {
                    return status;
                }
                textureCleanup.release();
                ++counters_->texturesUploaded;
                return Tina::Core::success();
            };
            if (auto status = registerTextureOwner(
                    productMesh.textureAsset, "cooked baseColor Texture2D missing for product slot");
                !status)
            {
                return status;
            }
            if (auto status = registerTextureOwner(
                    productMesh.metallicRoughnessTextureAsset,
                    "cooked metallic-roughness Texture2D missing for product slot");
                !status)
            {
                return status;
            }
            if (productMesh.metallicRoughnessTextureAsset)
            {
                counters_->materialMrTextureBound = true;
            }
            if (auto status = registerTextureOwner(
                    productMesh.normalTextureAsset, "cooked normal Texture2D missing for product slot");
                !status)
            {
                return status;
            }
            if (productMesh.normalTextureAsset)
            {
                counters_->materialNormalTextureBound = true;
            }

            auto materialBinding = mesh3DBindings_->registerMaterialBinding(productMesh.materialAsset);
            if (!materialBinding)
            {
                return Tina::Core::failure(std::move(materialBinding.error()));
            }
            ++counters_->materialBindingsRegistered;
            counters_->materialFactorsBound = true;
        }
        auto transparentMaterialBinding =
            mesh3DBindings_->registerMaterialBinding(resources_->transparentMaterialAsset);
        if (!transparentMaterialBinding)
        {
            return Tina::Core::failure(std::move(transparentMaterialBinding.error()));
        }
        ++counters_->materialBindingsRegistered;
        counters_->transparentWitnessMaterialBound = true;
        counters_->meshBound = true;
        bool everyBaseTextureBound = true;
        bool everyCompletePbrTextureBound = true;
        for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
        {
            const ProductMeshSlot& productMesh = resources_->meshes[slot];
            if (productMesh.meshKind != Tina::AssetFormat::AssetKind::StaticMesh)
            {
                continue;
            }
            everyBaseTextureBound = everyBaseTextureBound && productMesh.textureAsset;
            everyCompletePbrTextureBound =
                everyCompletePbrTextureBound && productMesh.textureAsset &&
                productMesh.metallicRoughnessTextureAsset && productMesh.normalTextureAsset;
        }
        const bool allTextureOwnersRegistered =
            counters_->texturesUploaded == resources_->textureAssetCount &&
            mesh3DBindings_->textureOwnerCount() == resources_->textureAssetCount;
        // Shared dependencies have one GPU owner even when every Material uses them.
        // External assets may omit textures entirely.
        if (resources_->externalGltf)
        {
            counters_->materialTextureBound = true;
        }
        else if (resources_->completePbrFixture)
        {
            counters_->materialTextureBound =
                everyCompletePbrTextureBound && allTextureOwnersRegistered &&
                counters_->materialMrTextureBound && counters_->materialNormalTextureBound &&
                counters_->materialFactorsBound;
        }
        else
        {
            counters_->materialTextureBound = everyBaseTextureBound && allTextureOwnersRegistered;
        }

        // Prefab may expand multi-prim parents + draw children; scale with cooked mesh slots.
        const Tina::Core::usize worldCapacity =
            static_cast<Tina::Core::usize>((std::max)(32U, resources_->meshSlotCount * 4U + 16U));
        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{worldCapacity});
        if (!worldResult)
        {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        struct ProductDirectionalLight final {
            Tina::Math::Vec3 directionTowardLight{};
            Tina::Render::RenderLinearColor color{};
            bool hasCascadedShadow = false;
        };
        constexpr std::array ProductLights{
            ProductDirectionalLight{
                .directionTowardLight = {0.35F, 0.9F, 0.4F},
                .color = {.red = 1.0F, .green = 0.98F, .blue = 0.92F},
                .hasCascadedShadow = true,
            },
            ProductDirectionalLight{
                .directionTowardLight = {-0.55F, 0.25F, -0.35F},
                .color = {.red = 0.28F, .green = 0.34F, .blue = 0.45F},
            },
            ProductDirectionalLight{
                .directionTowardLight = {0.15F, 0.45F, -0.9F},
                .color = {.red = 0.14F, .green = 0.18F, .blue = 0.30F},
            },
        };
        for (const ProductDirectionalLight& light : ProductLights)
        {
            auto lightEntity = world_->createEntity(Tina::Scene::LocalTransform{
                .rotation = rotationFromPositiveZ(light.directionTowardLight),
            });
            if (!lightEntity)
            {
                return Tina::Core::failure(std::move(lightEntity.error()));
            }
            if (auto status = world_->setDirectionalLight3D(
                    *lightEntity,
                    Tina::Scene::DirectionalLight3D{
                        .color = light.color,
                        .cascadedShadow = light.hasCascadedShadow
                            ? std::optional<Tina::Scene::CascadedDirectionalShadow3D>{
                                  Tina::Scene::CascadedDirectionalShadow3D{
                                      .maximumDistanceMeters = 45.0F,
                                      .depthBias = 0.0015F,
                                      .normalBiasMeters = 0.025F,
                                  }}
                            : std::nullopt,
                    });
                !status)
            {
                return status;
            }
        }
        counters_->directionalLightCount = static_cast<u32>(ProductLights.size());
        counters_->cascadedDirectionalShadowCount = 1U;
        counters_->cascadedDirectionalShadowCascadeCount =
            Tina::Render::Mesh3DCascadedDirectionalShadow::CascadeCount;
        counters_->lightingConfigured = true;

        const Tina::Asset::CookedAssetFile* prefabFile =
            resources_->assetSystem->tryGet(resources_->prefabAsset);
        if (prefabFile == nullptr)
        {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "cooked Prefab missing from product AssetSystem");
        }
        auto prefab = Tina::Asset::parsePrefabFromCooked(*prefabFile);
        if (!prefab)
        {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        // Per-node stable AssetId resolves to weak mesh/material handles. Backend
        // binding keys are resolved only during Scene extraction.
        Product3DResources* productResources = resources_;
        Tina::Scene::PrefabMeshBinding productBinding{
                .mesh = resources_->meshes[0].meshAsset,
                .material = resources_->meshes[0].materialAsset,
                .localBounds = {.radius = resources_->meshes[0].meshBoundsRadius},
                .baseColorFactor = resources_->meshes[0].materialColor,
                .alphaMode = resources_->meshes[0].alphaMode,
                .resolveMesh =
                    [productResources](Tina::Core::AssetId id) -> Tina::Asset::AssetHandle {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].meshId == id)
                            {
                                return productResources->meshes[slot].meshAsset;
                            }
                        }
                        return {};
                    },
                .resolveMaterial =
                    [productResources](Tina::Core::AssetId id) -> Tina::Asset::AssetHandle {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].materialId == id)
                            {
                                return productResources->meshes[slot].materialAsset;
                            }
                        }
                        return {};
                    },
                .resolveLocalBounds =
                    [productResources](Tina::Core::AssetId id) -> Tina::Render::RenderBoundingSphereInput {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].meshId == id)
                            {
                                return {.radius = productResources->meshes[slot].meshBoundsRadius};
                            }
                        }
                        return {.radius = 1.75F};
                    },
                .resolveBaseColor =
                    [productResources](Tina::Core::AssetId id) -> Tina::Render::RenderLinearColor {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].materialId == id)
                            {
                                return productResources->meshes[slot].materialColor;
                            }
                        }
                        return {};
                    },
                .resolveAlphaMode =
                    [productResources](Tina::Core::AssetId id) -> Tina::Render::Mesh3DAlphaMode {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].materialId == id)
                            {
                                return productResources->meshes[slot].alphaMode;
                            }
                        }
                        return Tina::Render::Mesh3DAlphaMode::Opaque;
                    },
            };
        auto instances = Tina::Scene::instantiatePrefab(*world_, prefab->view, productBinding);
        if (!instances)
        {
            return Tina::Core::failure(std::move(instances.error()));
        }
        prefabEntities_ = std::move(*instances);
        counters_->prefabInstances = prefabEntities_.size();
        counters_->prefabInstantiated = true;
        if (counters_->prefabInstances == 0)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "prefab instantiate produced no product instances");
        }
        if (!resources_->externalGltf && counters_->prefabInstances < 2U)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "prefab instantiate produced fewer than two product instances");
        }

        const Tina::Asset::CookedAssetFile* skinnedPrefabFile =
            resources_->assetSystem->tryGet(resources_->skinnedPrefabAsset);
        if (!resources_->hasDedicatedSkinnedPrefab || skinnedPrefabFile == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "cooked skinned Prefab missing from product AssetSystem");
        }
        auto skinnedPrefab = Tina::Asset::parsePrefabFromCooked(*skinnedPrefabFile);
        if (!skinnedPrefab)
        {
            return Tina::Core::failure(std::move(skinnedPrefab.error()));
        }
        auto skinnedInstances = Tina::Scene::instantiatePrefab(
            *world_, skinnedPrefab->view, productBinding);
        if (!skinnedInstances)
        {
            return Tina::Core::failure(std::move(skinnedInstances.error()));
        }
        skinnedPrefabEntities_ = std::move(*skinnedInstances);
        for (const Tina::Scene::EntityId entity : skinnedPrefabEntities_)
        {
            if (world_->skinnedMeshRenderer3D(entity) == nullptr)
            {
                continue;
            }
            if (skinnedMeshEntity_)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "product skin witness Prefab produced more than one skinned renderer");
            }
            skinnedMeshEntity_ = entity;
        }
        if (!skinnedMeshEntity_)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "product skin witness Prefab produced no skinned renderer");
        }
        counters_->skinnedPrefabInstances =
            static_cast<u32>(skinnedPrefabEntities_.size());

        const bool hasBuiltInPointShadowWitness = !resources_->externalGltf;
        if (hasBuiltInPointShadowWitness)
        {
            constexpr std::array<Tina::Math::Vec3, 2> WitnessPositions{
                Tina::Math::Vec3{-1.0F, -1.0F, 0.0F},
                Tina::Math::Vec3{-0.35F, -0.35F, 0.85F},
            };
            constexpr std::array<float, 2> WitnessScales{2.4F, 0.85F};
            for (std::size_t index = 0; index < WitnessPositions.size(); ++index)
            {
                const Tina::Scene::EntityId entity = prefabEntities_[index];
                const Tina::Scene::LocalTransform* existing = world_->localTransform(entity);
                const Tina::Scene::MeshRenderer3D* existingMesh =
                    world_->meshRenderer3D(entity);
                if (existing == nullptr || existingMesh == nullptr)
                {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "built-in point-shadow witness is missing a prefab transform or mesh");
                }

                Tina::Scene::LocalTransform local = *existing;
                local.position = WitnessPositions[index];
                local.scale = Tina::Math::Vec3{
                    WitnessScales[index], WitnessScales[index], WitnessScales[index]};
                if (auto status = world_->setLocalTransform(entity, local); !status)
                {
                    return status;
                }

                Tina::Scene::MeshRenderer3D mesh = *existingMesh;
                mesh.doubleSided = true;
                if (auto status = world_->setMeshRenderer3D(entity, mesh); !status)
                {
                    return status;
                }
            }
        }
        if (auto status = world_->updateWorldTransforms(); !status)
        {
            return status;
        }

        // Frame camera to prefab mesh AABB after transforms (works for spheres grid / external models).
        float minX = 0.0F;
        float minY = 0.0F;
        float minZ = 0.0F;
        float maxX = 0.0F;
        float maxY = 0.0F;
        float maxZ = 0.0F;
        bool haveBounds = false;
        for (const Tina::Scene::EntityId entity : world_->liveEntities())
        {
            const Tina::Scene::MeshRenderer3D* staticMesh = world_->meshRenderer3D(entity);
            const Tina::Scene::SkinnedMeshRenderer3D* skinnedMesh =
                world_->skinnedMeshRenderer3D(entity);
            const auto* worldXf = world_->worldTransform(entity);
            if ((staticMesh == nullptr && skinnedMesh == nullptr) || worldXf == nullptr)
            {
                continue;
            }
            const float localRadius = staticMesh != nullptr
                ? staticMesh->localBounds.radius
                : skinnedMesh->localBounds.radius;
            const float r = localRadius > 0.0F ? localRadius : 1.0F;
            const float sx = (std::max)(std::abs(worldXf->scale.x), 1.0e-3F);
            const float sy = (std::max)(std::abs(worldXf->scale.y), 1.0e-3F);
            const float sz = (std::max)(std::abs(worldXf->scale.z), 1.0e-3F);
            const float wr = r * (std::max)(sx, (std::max)(sy, sz));
            const float x = worldXf->position.x;
            const float y = worldXf->position.y;
            const float z = worldXf->position.z;
            if (!haveBounds)
            {
                minX = x - wr;
                maxX = x + wr;
                minY = y - wr;
                maxY = y + wr;
                minZ = z - wr;
                maxZ = z + wr;
                haveBounds = true;
            }
            else
            {
                minX = (std::min)(minX, x - wr);
                maxX = (std::max)(maxX, x + wr);
                minY = (std::min)(minY, y - wr);
                maxY = (std::max)(maxY, y + wr);
                minZ = (std::min)(minZ, z - wr);
                maxZ = (std::max)(maxZ, z + wr);
            }
        }
        const float centerX = haveBounds ? 0.5F * (minX + maxX) : 1.0F;
        const float centerY = haveBounds ? 0.5F * (minY + maxY) : 0.0F;
        const float centerZ = haveBounds ? 0.5F * (minZ + maxZ) : 0.0F;
        const float extentX = haveBounds ? (maxX - minX) : 3.0F;
        const float extentY = haveBounds ? (maxY - minY) : 2.0F;
        const float extentZ = haveBounds ? (maxZ - minZ) : 2.0F;
        const float radius =
            0.5F * std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);
        constexpr float kFovDegrees = 55.0F;
        const float fovRad = kFovDegrees * 0.01745329252F;
        const float distance = (std::max)(radius / std::tan(0.5F * fovRad) * 1.35F, 3.0F);
        Tina::Scene::LocalTransform cameraLocal{};
        cameraLocal.position = {centerX, centerY + radius * 0.15F, centerZ + distance};
        auto cameraEntity = world_->createEntity(cameraLocal);
        if (!cameraEntity)
        {
            return Tina::Core::failure(std::move(cameraEntity.error()));
        }
        cameraEntity_ = *cameraEntity;
        if (auto status = world_->setPerspectiveCamera3D(
                cameraEntity_,
                Tina::Scene::PerspectiveCamera3D{
                    .verticalFovDegrees = kFovDegrees,
                    .nearPlaneMeters = (std::max)(0.05F, distance * 0.02F),
                    .farPlaneMeters = (std::max)(100.0F, distance + radius * 8.0F),
                    .active = true,
                });
            !status)
        {
            return status;
        }

        if (options_.transparencyMode == TransparencyMode::On)
        {
            const ProductMeshSlot* witnessMesh = nullptr;
            for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
            {
                if (resources_->meshes[slot].meshKind == Tina::AssetFormat::AssetKind::StaticMesh)
                {
                    witnessMesh = &resources_->meshes[slot];
                    break;
                }
            }
            if (witnessMesh == nullptr || !resources_->transparentMaterialAsset)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "transparent witness requires a static mesh and its Blend Material");
            }

            const float witnessScale =
                (std::max)(radius * 0.48F / witnessMesh->meshBoundsRadius, 0.15F);
            const std::array witnessPositions{
                Tina::Math::Vec3{centerX - radius * 0.08F, centerY, centerZ + radius * 0.62F},
                Tina::Math::Vec3{centerX + radius * 0.08F, centerY, centerZ + radius * 0.84F},
            };
            constexpr std::array witnessColors{
                Tina::Render::RenderLinearColor{
                    .red = 1.0F, .green = 0.16F, .blue = 0.05F, .alpha = 0.62F},
                Tina::Render::RenderLinearColor{
                    .red = 0.04F, .green = 0.62F, .blue = 1.0F, .alpha = 0.54F},
            };
            transparentWitnessEntities_.reserve(TransparentWitnessCount);
            for (u32 index = 0; index < TransparentWitnessCount; ++index)
            {
                auto entity = world_->createEntity(Tina::Scene::LocalTransform{
                    .position = witnessPositions[index],
                    .scale = {witnessScale, witnessScale, witnessScale},
                });
                if (!entity)
                {
                    return Tina::Core::failure(std::move(entity.error()));
                }
                if (auto status = world_->setMeshRenderer3D(
                        *entity,
                        Tina::Scene::MeshRenderer3D{
                            .mesh = witnessMesh->meshAsset,
                            .material = resources_->transparentMaterialAsset,
                            .localBounds = {.radius = witnessMesh->meshBoundsRadius},
                            .baseColorFactor = witnessColors[index],
                            .alphaMode = Tina::Render::Mesh3DAlphaMode::Blend,
                            .doubleSided = true,
                        });
                    !status)
                {
                    return status;
                }
                transparentWitnessEntities_.push_back(*entity);
            }
            counters_->authoredTransparentStaticWitnessCount =
                static_cast<u32>(transparentWitnessEntities_.size());
        }

        struct ProductPointLight final {
            Tina::Math::Vec3 position{};
            Tina::Render::RenderLinearColor color{};
            float intensity = 1.0F;
        };
        const float pointInfluenceRadius = hasBuiltInPointShadowWitness
            ? (std::max)(radius * 3.0F, 4.0F)
            : (std::max)(radius * 2.0F, 2.5F);
        const std::array ProductPointLights{
            ProductPointLight{
                .position = hasBuiltInPointShadowWitness
                    ? Tina::Math::Vec3{centerX + radius * 0.2F,
                                        centerY + radius * 0.15F,
                                        centerZ + radius * 1.8F}
                    : Tina::Math::Vec3{centerX - radius * 0.8F,
                                        centerY + radius * 0.9F,
                                        centerZ + radius * 0.75F},
                .color = {.red = 1.0F, .green = 0.32F, .blue = 0.12F},
                .intensity = 1.15F,
            },
            ProductPointLight{
                .position = {centerX + radius * 0.9F, centerY - radius * 0.25F,
                             centerZ + radius * 0.35F},
                .color = {.red = 0.12F, .green = 0.42F, .blue = 1.0F},
                .intensity = 0.8F,
            },
            ProductPointLight{
                .position = {centerX + distance * 5.0F + pointInfluenceRadius * 2.0F,
                             centerY, centerZ},
                .color = {.red = 0.2F, .green = 1.0F, .blue = 0.35F},
            },
        };
        for (std::size_t lightIndex = 0; lightIndex < ProductPointLights.size(); ++lightIndex)
        {
            const ProductPointLight& light = ProductPointLights[lightIndex];
            auto lightEntity = world_->createEntity(Tina::Scene::LocalTransform{
                .position = light.position,
            });
            if (!lightEntity)
            {
                return Tina::Core::failure(std::move(lightEntity.error()));
            }
            Tina::Scene::PointLight3D component{
                .color = light.color,
                .intensity = light.intensity,
                .influenceRadiusMeters = pointInfluenceRadius,
            };
            if (lightIndex == 0U &&
                options_.pointLightShadowMode == PointLightShadowMode::On)
            {
                component.shadow = Tina::Scene::PointLightShadow3D{};
                ++counters_->authoredPointLightShadowCount;
            }
            if (auto status = world_->setPointLight3D(*lightEntity, component);
                !status)
            {
                return status;
            }
        }
        counters_->authoredPointLight3DCount = static_cast<u32>(ProductPointLights.size());

        struct ProductSpotLight final {
            Tina::Math::Vec3 position{};
            Tina::Math::Vec3 target{};
            Tina::Render::RenderLinearColor color{};
            float intensity = 1.0F;
            float innerConeHalfAngleDegrees = 16.0F;
            float outerConeHalfAngleDegrees = 30.0F;
            bool castsShadow = false;
        };
        const float spotInfluenceRadius = (std::max)(radius * 2.25F, 3.0F);
        const Tina::Math::Vec3 sceneCenter{centerX, centerY, centerZ};
        const std::array ProductSpotLights{
            ProductSpotLight{
                .position = {centerX - radius * 1.15F, centerY + radius * 1.35F,
                             centerZ + radius * 1.1F},
                .target = sceneCenter,
                .color = {.red = 1.0F, .green = 0.18F, .blue = 0.62F},
                .intensity = 0.9F,
                .innerConeHalfAngleDegrees = 14.0F,
                .outerConeHalfAngleDegrees = 28.0F,
                .castsShadow = true,
            },
            ProductSpotLight{
                .position = {centerX + radius * 1.25F, centerY + radius * 0.65F,
                             centerZ + radius * 1.15F},
                .target = sceneCenter,
                .color = {.red = 0.18F, .green = 0.9F, .blue = 0.72F},
                .intensity = 0.75F,
                .innerConeHalfAngleDegrees = 18.0F,
                .outerConeHalfAngleDegrees = 34.0F,
            },
            ProductSpotLight{
                .position = {centerX - distance * 5.0F - spotInfluenceRadius * 2.0F,
                             centerY, centerZ},
                .target = sceneCenter,
                .color = {.red = 0.82F, .green = 0.4F, .blue = 1.0F},
            },
        };
        for (const ProductSpotLight& light : ProductSpotLights)
        {
            const Tina::Math::Vec3 positiveZDirection = light.position - light.target;
            auto lightEntity = world_->createEntity(Tina::Scene::LocalTransform{
                .position = light.position,
                .rotation = rotationFromPositiveZ(positiveZDirection),
            });
            if (!lightEntity)
            {
                return Tina::Core::failure(std::move(lightEntity.error()));
            }
            Tina::Scene::SpotLight3D component{
                .color = light.color,
                .intensity = light.intensity,
                .influenceRadiusMeters = spotInfluenceRadius,
                .innerConeHalfAngleDegrees = light.innerConeHalfAngleDegrees,
                .outerConeHalfAngleDegrees = light.outerConeHalfAngleDegrees,
            };
            if (light.castsShadow)
            {
                component.shadow = Tina::Scene::SpotLightShadow3D{};
                ++counters_->authoredSpotLightShadowCount;
            }
            if (auto status = world_->setSpotLight3D(*lightEntity, component);
                !status)
            {
                return status;
            }
        }
        counters_->authoredSpotLight3DCount = static_cast<u32>(ProductSpotLights.size());

        if (auto status = world_->updateWorldTransforms(); !status)
        {
            return status;
        }

        if (auto status = ui_.build(
                context,
                Tina::Sample3D::Product3DUIConfig{
                    .initialTheme = options_.initialUiTheme,
                    .targetFrameCount = options_.targetFrameCount,
                    .automatedThemeDemo = options_.uiThemeDemo,
                });
            !status)
        {
            return status;
        }
        gpuRollback.release();
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        platformEvents_.reset();
        skinnedAnimator_.reset();
        skinnedMeshEntity_ = {};
        skinnedPrefabEntities_.clear();
        transparentWitnessEntities_.clear();
        world_.reset();
        prefabEntities_.clear();
        releaseRuntimeOnlyAssets();
        releaseProductGpuResources();
        ui_.release();
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (!skinnedAnimator_.has_value())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "3D product Animator3D was not initialized");
        }
        auto animationUpdate = skinnedAnimator_->update(Tina::Core::Duration{1.0 / 60.0});
        if (!animationUpdate)
        {
            return Tina::Core::failure(std::move(animationUpdate.error()));
        }
        ++counters_->animatorUpdates;
        if (animationUpdate->poseChanged)
        {
            ++counters_->animatorPoseChanges;
        }
        // Animation advances once per game frame even if the scene is extracted more than once.
        if (ui_.autoRotate())
        {
            rotationHalfAngle_ += 0.0125F * ui_.rotationSpeed();
        }
        if (options_.frameDelayMilliseconds != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        if (counters_->frameUpdates >= options_.targetFrameCount)
        {
            if (capture_ != nullptr)
            {
                capture_->requestCaptureNextPresent();
            }
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        return ui_.update(context, counters_->frameUpdates);
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        if (!world_.has_value())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "3D product World was not initialized");
        }

        // Spin root-ish prefab entities for visible motion without hand-built cubes.
        const float halfAngle = rotationHalfAngle_;
        for (std::size_t index = 0; index < prefabEntities_.size(); ++index)
        {
            const Tina::Scene::LocalTransform* existing = world_->localTransform(prefabEntities_[index]);
            if (existing == nullptr)
            {
                continue;
            }
            Tina::Scene::LocalTransform local = *existing;
            const float phase = halfAngle + static_cast<float>(index) * 0.45F;
            local.rotation = {0.0F, std::sin(phase), 0.0F, std::cos(phase)};
            if (auto status = world_->setLocalTransform(prefabEntities_[index], local); !status)
            {
                return status;
            }
        }

        auto& writer = context.renderSceneWriter();
        if (auto status = Tina::Scene::extractRenderSceneFromWorld(
                *world_,
                writer,
                context.frameResourceSink(),
                Tina::Scene::ExtractRenderSceneParams{
                    .surfaceViewport =
                        Tina::Render::Camera2DSurfaceViewport{
                            .pixelWidth = framebufferExtent_.width,
                            .pixelHeight = framebufferExtent_.height,
                        },
                    .mesh3DBindingResolver = Tina::Asset::AssetFrameResourceResolver{
                        .userData = const_cast<Product3DState*>(this),
                        .resolve = &resolveProductMeshBinding,
                    },
                    .material3DBindingResolver = Tina::Asset::AssetFrameResourceResolver{
                        .userData = const_cast<Product3DState*>(this),
                        .resolve = &resolveProductMaterialBinding,
                    },
                    .skinnedMesh3DBindingResolver = Tina::Asset::AssetFrameResourceResolver{
                        .userData = const_cast<Product3DState*>(this),
                        .resolve = &resolveProductSkinnedMeshBinding,
                    },
                    .skinnedPose3DProvider = Tina::Scene::SkinnedPose3DProvider{
                        .userData = const_cast<Product3DState*>(this),
                        .resolve = &resolveProductSkinnedPose,
                    },
                    .ambientLightScale = 0.16F,
                });
            !status)
        {
            return status;
        }
        ++counters_->renderExtractions;
        ++counters_->sceneLightingFrames;
        return Tina::Core::success();
    }

  private:
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef> resolveProductMeshBinding(
        void* userData,
        Tina::Asset::AssetHandle asset,
        Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<Product3DState*>(userData);
        if (!self.mesh3DBindings_.has_value())
        {
            return Tina::Render::FrameResourceRef{};
        }
        auto resource = self.mesh3DBindings_->internMeshFrameResource(asset, sink);
        if (!resource)
        {
            return Tina::Core::failure(std::move(resource.error()));
        }
        if (resource->hasValue())
        {
            ++self.counters_->meshFrameResourceResolverHits;
        }
        return resource;
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef> resolveProductMaterialBinding(
        void* userData,
        Tina::Asset::AssetHandle asset,
        Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<Product3DState*>(userData);
        if (!self.mesh3DBindings_.has_value())
        {
            return Tina::Render::FrameResourceRef{};
        }
        auto resource = self.mesh3DBindings_->internMaterialFrameResource(asset, sink);
        if (!resource)
        {
            return Tina::Core::failure(std::move(resource.error()));
        }
        if (resource->hasValue())
        {
            ++self.counters_->materialFrameResourceResolverHits;
        }
        return resource;
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolveProductSkinnedMeshBinding(
        void* userData,
        Tina::Asset::AssetHandle asset,
        Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<Product3DState*>(userData);
        if (!self.mesh3DBindings_.has_value())
        {
            return Tina::Render::FrameResourceRef{};
        }
        auto resource = self.mesh3DBindings_->internSkinnedMeshFrameResource(asset, sink);
        if (!resource)
        {
            return Tina::Core::failure(std::move(resource.error()));
        }
        if (resource->hasValue())
        {
            ++self.counters_->skinnedMeshFrameResourceResolverHits;
        }
        return resource;
    }

    [[nodiscard]] static std::span<const float> resolveProductSkinnedPose(
        void* userData, Tina::Scene::EntityId entity) noexcept
    {
        auto& self = *static_cast<Product3DState*>(userData);
        if (!self.skinnedAnimator_.has_value() || entity != self.skinnedMeshEntity_)
        {
            return {};
        }
        ++self.counters_->skinnedPoseProviderHits;
        return self.skinnedAnimator_->skinningMatrices();
    }

    void releaseRuntimeOnlyAssets() noexcept
    {
        if (!resources_->assetSystem.has_value())
        {
            return;
        }
        if (!animationClipReleased_ && resources_->animationClipAsset)
        {
            if (resources_->assetSystem->tryGet(resources_->animationClipAsset) != nullptr)
            {
                if (auto status = resources_->assetSystem->unload(resources_->animationClipAsset);
                    !status)
                {
                    std::terminate();
                }
            }
            animationClipReleased_ = true;
        }
        if (!skinnedPrefabReleased_ && resources_->skinnedPrefabAsset)
        {
            if (resources_->assetSystem->tryGet(resources_->skinnedPrefabAsset) != nullptr)
            {
                if (auto status = resources_->assetSystem->unload(resources_->skinnedPrefabAsset);
                    !status)
                {
                    std::terminate();
                }
            }
            skinnedPrefabReleased_ = true;
        }
    }

    void releaseProductGpuResources() noexcept
    {
        Tina::Render::IRenderDevice* device = device_;
        if (environmentMap_)
        {
            if (device == nullptr)
            {
                std::terminate();
            }
            if (imageBasedLightingBound_)
            {
                if (auto status = device->clearMesh3DImageBasedLighting(); !status)
                {
                    std::terminate();
                }
                imageBasedLightingBound_ = false;
            }
            Tina::Render::FramePin completionPin{};
            if (auto status = device->retireEnvironmentMap(environmentMap_, completionPin); !status)
            {
                std::terminate();
            }
            environmentMap_ = {};
        }

        if (!mesh3DBindings_.has_value())
        {
            return;
        }
        const u64 meshCount = mesh3DBindings_->meshBindingCount();
        const u64 materialCount = mesh3DBindings_->materialBindingCount();
        const u64 textureCount = mesh3DBindings_->textureOwnerCount();
        if (auto status = mesh3DBindings_->retireAllBindings(); !status)
        {
            std::terminate();
        }
        counters_->meshBindingsReleased += meshCount;
        counters_->materialBindingsReleased += materialCount;
        counters_->meshRetirementsAccepted += meshCount;
        counters_->textureRetirementsAccepted += textureCount;
        counters_->bindingRegistryReleased =
            mesh3DBindings_->meshBindingCount() == 0 &&
            mesh3DBindings_->materialBindingCount() == 0 &&
            mesh3DBindings_->textureOwnerCount() == 0;
        mesh3DBindings_.reset();
    }

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Product3DResources* resources_ = nullptr;
    // Kept only for requestCaptureNextPresent(), the decorator's own telemetry API.
    DeviceCapture* capture_ = nullptr;
    Tina::Render::IRenderDevice* device_ = nullptr;
    Tina::Sample3D::Product3DUI ui_;
    std::optional<Tina::PlatformEventSubscription> platformEvents_{};
    std::optional<Tina::Asset::Mesh3DBindingRegistry> mesh3DBindings_{};
    std::optional<Tina::Scene::Animator3D> skinnedAnimator_{};
    Tina::Render::GpuEnvironmentMapId environmentMap_{};
    bool imageBasedLightingBound_ = false;
    mutable std::optional<Tina::Scene::World> world_{};
    Tina::Platform::LogicalExtent logicalExtent_{DefaultWindowLogicalWidth, DefaultWindowLogicalHeight};
    Tina::Platform::FramebufferExtent framebufferExtent_{DefaultWindowLogicalWidth, DefaultWindowLogicalHeight};
    float rotationHalfAngle_ = 0.0F;
    Tina::Scene::EntityId cameraEntity_{};
    Tina::Scene::EntityId skinnedMeshEntity_{};
    std::vector<Tina::Scene::EntityId> prefabEntities_{};
    std::vector<Tina::Scene::EntityId> skinnedPrefabEntities_{};
    std::vector<Tina::Scene::EntityId> transparentWitnessEntities_{};
    bool animationClipReleased_ = false;
    bool skinnedPrefabReleased_ = false;
};

class Product3DApplication final : public Tina::IGameApplication {
  public:
    Product3DApplication(SampleOptions options, LifecycleCounters& counters, Product3DResources& resources,
                         DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<Product3DState>(options_, *counters_, *resources_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override { ++counters_->applicationShutdowns; }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Product3DResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(const SampleOptions& options)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina vNext 3D Product";
    config.primaryWindow.title = "Tina vNext - glTF Prefab Product 3D";
    config.primaryWindow.initialLogicalExtent = {options.windowLogicalWidth, options.windowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    // MetalRoughSpheres-scale external models: one item/batch per mesh instance.
    config.renderSceneCapacities.mesh3DItemCapacity =
        MaxProductMeshSlots + TransparentWitnessCount;
    config.renderSceneCapacities.mesh3DBatchCapacity = MaxProductMeshSlots;
    config.renderSceneCapacities.skinnedMesh3DItemCapacity = 1U;
    config.renderSceneCapacities.transparent3DDrawCapacity =
        MaxProductMeshSlots + TransparentWitnessCount;
    config.renderSceneCapacities.skinnedMesh3DPaletteJointCapacity =
        Tina::Render::MaxSkinnedMesh3DPaletteJointCount;
    return config;
}

[[nodiscard]] int runSample(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult)
    {
        writeError(optionsResult.error());
        return 2;
    }
    const SampleOptions options = *optionsResult;
    if (options.help)
    {
        printUsage();
        return 0;
    }

    LifecycleCounters counters;
    Product3DResources resources;
    if (auto status = prepareCookedProductAssets(resources, counters, options); !status)
    {
        writeError(status.error());
        return 1;
    }
    // The work root stays beside the executable; prepareCookedProductAssets wipes it before each
    // cook, so deleting it on exit only made a failed cook impossible to inspect.
    DeviceCapture capture;
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&capture](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
            return Tina::Sample3D::wrapCapturingRenderDevice(std::move(device), capture);
        };
    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(options), std::move(desktopOptions));
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    Product3DApplication application{options, counters, resources, capture};
    auto runResult = (*hostResult)->run(application);

    counters.pixelCaptureAttempted = true;
    if (capture.hasLastCapture() && capture.lastCapture() != nullptr)
    {
        recordPixelCapture(counters, *capture.lastCapture(), options.sceneRgbOutputPath);
    }
    else if (Tina::Render::IRenderDevice* device = capture.get(); device != nullptr)
    {
        auto captured = device->capturePrimaryFrameRgba8();
        if (captured.has_value())
        {
            recordPixelCapture(counters, *captured, options.sceneRgbOutputPath);
        }
    }

    const bool ledgerBalanced =
        capture.get() == nullptr || capture.get()->statistics().liveResources == 0;
    counters.submittedLightingFrames = capture.submittedLightingFrames();
    counters.submittedDirectionalLightCount = capture.directionalLightCount();
    const u32 submittedCascadedDirectionalShadowCount = capture.cascadedDirectionalShadowCount();
    const u32 submittedCascadedDirectionalShadowCascadeCount =
        capture.cascadedDirectionalShadowCascadeCount();
    const u32 submittedSpotLightShadowCount = capture.spotLightShadowCount();
    const u32 submittedPointLightShadowCount = capture.pointLightShadowCount();
    counters.submittedCameraAspectRatio = capture.submittedCameraAspectRatio();
    counters.cameraAspectChanges = capture.cameraAspectChanges();
    counters.pointLight3DCount = capture.pointLight3DCount();
    counters.spotLight3DCount = capture.spotLight3DCount();
    counters.lightingCountsStable = capture.lightingCountsStable();
    counters.culledPointLight3DCount =
        counters.authoredPointLight3DCount >= counters.pointLight3DCount
            ? counters.authoredPointLight3DCount - counters.pointLight3DCount
            : 0U;
    counters.culledSpotLight3DCount =
        counters.authoredSpotLight3DCount >= counters.spotLight3DCount
            ? counters.authoredSpotLight3DCount - counters.spotLight3DCount
            : 0U;
    if (counters.framebufferPixelWidth != 0 && counters.framebufferPixelHeight != 0)
    {
        const float expectedAspect = static_cast<float>(counters.framebufferPixelWidth) /
                                     static_cast<float>(counters.framebufferPixelHeight);
        counters.cameraAspectMatchesSurface =
            std::isfinite(counters.submittedCameraAspectRatio) &&
            std::abs(counters.submittedCameraAspectRatio - expectedAspect) <= 1.0e-4F;
    }
    hostResult->reset();

    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }
    const u32 expectedMeshes = resources.meshSlotCount;
    const u32 expectedStaticMeshes = resources.staticMeshSlotCount;
    const u32 expectedSkinnedMeshes = resources.skinnedMeshSlotCount;
    const u64 tangentMeshesUploaded = capture.tangentMeshesUploaded();
    const u64 skinnedMeshesUploaded = capture.skinnedMeshesUploaded();
    const u64 submittedSkinnedMesh3DFrames = capture.submittedSkinnedMesh3DFrames();
    const u32 submittedSkinnedMesh3DCount = capture.submittedSkinnedMesh3DCount();
    const u32 visibleSkinnedMesh3DCount = capture.visibleSkinnedMesh3DCount();
    const u32 submittedSkinnedPaletteJointCount = capture.submittedSkinnedPaletteJointCount();
    const u64 firstSubmittedSkinnedPoseFingerprint =
        capture.firstSubmittedSkinnedPoseFingerprint();
    const u64 submittedSkinnedPoseFingerprint = capture.submittedSkinnedPoseFingerprint();
    const u64 submittedSkinnedPoseFingerprintChanges =
        capture.submittedSkinnedPoseFingerprintChanges();
    const u64 submittedTransparent3DFrames = capture.submittedTransparent3DFrames();
    const u32 submittedTransparentStaticMesh3DCount =
        capture.submittedTransparentStaticMesh3DCount();
    const u32 submittedTransparentSkinnedMesh3DCount =
        capture.submittedTransparentSkinnedMesh3DCount();
    const u32 submittedTransparent3DDrawCount = capture.submittedTransparent3DDrawCount();
    const u64 submittedTransparent3DSortOrderChecksum =
        capture.submittedTransparent3DSortOrderChecksum();
    const bool transparent3DSortOrderStable = capture.transparent3DSortOrderStable();
    const u64 environmentMapsUploaded = capture.environmentMapsUploaded();
    const u64 imageBasedLightingBindings = capture.imageBasedLightingBindings();
    const u64 imageBasedLightingClears = capture.imageBasedLightingClears();
    const u64 environmentMapRetirements = capture.environmentMapRetirements();
    auto& assetSystem = *resources.assetSystem;
    for (const Tina::Asset::AssetRetirementRecord& record : assetSystem.retirement().records())
    {
        if (record.kind == Tina::Asset::AssetRetirementKind::GpuMesh)
        {
            ++counters.meshRetirementRecords;
            if (record.state == Tina::Asset::AssetRetirementState::Released)
            {
                ++counters.meshRetirementReleased;
            }
        }
        else if (record.kind == Tina::Asset::AssetRetirementKind::GpuTexture2D)
        {
            ++counters.textureRetirementRecords;
            if (record.state == Tina::Asset::AssetRetirementState::Released)
            {
                ++counters.textureRetirementReleased;
            }
        }
    }
    counters.retirementRecordsLive = assetSystem.retirementStats().live;

    for (u32 slot = 0; slot < resources.meshSlotCount; ++slot)
    {
        const ProductMeshSlot& productMesh = resources.meshes[slot];
        if (assetSystem.state(productMesh.meshAsset) == Tina::Asset::AssetLogicalState::Unloaded &&
            assetSystem.tryGet(productMesh.meshAsset) == nullptr)
        {
            ++counters.meshAssetHandlesInvalidated;
        }
        if (assetSystem.state(productMesh.materialAsset) == Tina::Asset::AssetLogicalState::Unloaded &&
            assetSystem.tryGet(productMesh.materialAsset) == nullptr)
        {
            ++counters.materialAssetHandlesInvalidated;
        }
    }
    if (assetSystem.state(resources.transparentMaterialAsset) ==
            Tina::Asset::AssetLogicalState::Unloaded &&
        assetSystem.tryGet(resources.transparentMaterialAsset) == nullptr)
    {
        ++counters.materialAssetHandlesInvalidated;
    }
    for (u32 index = 0; index < resources.textureAssetCount; ++index)
    {
        const Tina::Asset::AssetHandle texture = resources.textures[index].handle;
        if (assetSystem.state(texture) == Tina::Asset::AssetLogicalState::Unloaded &&
            assetSystem.tryGet(texture) == nullptr)
        {
            ++counters.textureAssetHandlesInvalidated;
        }
    }
    if (assetSystem.state(resources.animationClipAsset) == Tina::Asset::AssetLogicalState::Unloaded &&
        assetSystem.tryGet(resources.animationClipAsset) == nullptr)
    {
        ++counters.animationClipAssetHandlesInvalidated;
    }
    if (assetSystem.state(resources.skinnedPrefabAsset) == Tina::Asset::AssetLogicalState::Unloaded &&
        assetSystem.tryGet(resources.skinnedPrefabAsset) == nullptr)
    {
        ++counters.skinnedPrefabAssetHandlesInvalidated;
    }
    const Tina::usize assetStoreActiveCount =
        assetSystem.store().activeCount();
    const bool prefabAssetResident = assetSystem.tryGet(resources.prefabAsset) != nullptr;
    const bool multiMesh = expectedStaticMeshes >= 2U;
    const bool texturesOk = resources.externalGltf
                                ? true
                                : (resources.completePbrFixture
                                       ? (counters.texturesUploaded == resources.textureAssetCount &&
                                          resources.textureAssetCount >= 3U &&
                                          counters.materialMrTextureBound && counters.materialNormalTextureBound &&
                                          counters.materialFactorsBound)
                                       : (counters.texturesUploaded == resources.textureAssetCount &&
                                          resources.textureAssetCount >= 1U));
    const bool pixelGoldenChecked = !options.expectPixelFingerprint.empty();
    const bool pixelGoldenMatched =
        !pixelGoldenChecked || counters.pixelFingerprint == options.expectPixelFingerprint;
    const bool sceneRgbOutputRequested = !options.sceneRgbOutputPath.empty();
    const bool imageBasedLightingEnabled = options.imageBasedLightingMode == ImageBasedLightingMode::On;
    const bool pointLightShadowEnabled = options.pointLightShadowMode == PointLightShadowMode::On;
    const bool transparencyEnabled = options.transparencyMode == TransparencyMode::On;
    const u32 expectedTransparentStaticWitnessCount = transparencyEnabled ? TransparentWitnessCount : 0U;
    const u64 expectedTransparent3DFrames = transparencyEnabled ? options.targetFrameCount : 0U;
    const bool observedExternalTransparentDraw = submittedTransparent3DDrawCount != 0U;
    const bool externalTransparentActivityValid =
        observedExternalTransparentDraw
            ? (submittedTransparent3DFrames != 0U && submittedTransparent3DFrames <= options.targetFrameCount &&
               submittedTransparent3DSortOrderChecksum != 0U)
            : (submittedTransparent3DFrames == 0U && submittedTransparent3DSortOrderChecksum == 0U);
    const bool transparentCaptureValid =
        resources.externalGltf
            ? (submittedTransparent3DDrawCount ==
                   submittedTransparentStaticMesh3DCount + submittedTransparentSkinnedMesh3DCount &&
               externalTransparentActivityValid &&
               (!transparencyEnabled || (submittedTransparent3DFrames == options.targetFrameCount &&
                                         submittedTransparentStaticMesh3DCount >= TransparentWitnessCount)))
            : (submittedTransparent3DFrames == expectedTransparent3DFrames &&
               submittedTransparentStaticMesh3DCount == expectedTransparentStaticWitnessCount &&
               submittedTransparentSkinnedMesh3DCount == 0U &&
               submittedTransparent3DDrawCount == expectedTransparentStaticWitnessCount &&
               (submittedTransparent3DSortOrderChecksum != 0U) == transparencyEnabled && transparent3DSortOrderStable);
    const u32 expectedMaterials = expectedMeshes + 1U;
    const u32 expectedPointLightShadowCount = pointLightShadowEnabled ? 1U : 0U;
    const u64 expectedImageBasedLightingTransitions = imageBasedLightingEnabled ? 1U : 0U;
    const auto& ui = counters.ui;
    const bool expectedInitialThemeLight = options.initialUiTheme == Tina::Sample3D::Product3DUITheme::Light;
    const bool unattendedThemeValid =
        ui.themeButtonActivations != 0 ||
        (ui.themeSwitches == (options.uiThemeDemo ? 2U : 0U) &&
         ui.finalThemeLight == expectedInitialThemeLight);
    constexpr Tina::UI::UIListViewItemKey InitialAssetSelectionKey = 2'000;
    constexpr Tina::UI::UIListViewItemKey AutomatedAssetSelectionKey = 2'003;
    constexpr Tina::UI::UITreeViewItemKey InitialSceneSelectionKey = 1;
    constexpr Tina::UI::UITreeViewItemKey AutomatedSceneSelectionKey = 4;
    const bool collectionDemoValid =
        ui.automatedCollectionSteps == (options.uiThemeDemo ? 2U : 0U) &&
        ui.treeExpansionChanges == (options.uiThemeDemo ? 2U : 0U) &&
        ui.listSelectionKey == (options.uiThemeDemo ? AutomatedAssetSelectionKey : InitialAssetSelectionKey) &&
        ui.treeSelectionKey == (options.uiThemeDemo ? AutomatedSceneSelectionKey : InitialSceneSelectionKey);
    const bool uiValid =
        ui.rootsCreated == 1 && ui.rootsReleased == 1 && !ui.rootAlive && ui.panelsCreated == 7 &&
        ui.labelsCreated == 13 && ui.buttonsCreated == 1 && ui.checkboxesCreated == 1 &&
        ui.slidersCreated == 1 && ui.progressBarsCreated == 1 && ui.listViewsCreated == 1 &&
        ui.treeViewsCreated == 1 && ui.themeDemoRequested == options.uiThemeDemo &&
        ui.initialThemeLight == expectedInitialThemeLight &&
        ui.automatedThemeSteps == (options.uiThemeDemo ? 2U : 0U) && unattendedThemeValid &&
        collectionDemoValid && ui.inheritedChromeVerified && ui.controlsInitialStateVerified &&
        ui.responsiveLayoutVerified && ui.progressUpdates >= options.targetFrameCount &&
        ui.finalProgress >= 99.9F;
    const u64 expectedStaticMeshResolverHits =
        static_cast<u64>(options.targetFrameCount) *
        (expectedStaticMeshes + expectedTransparentStaticWitnessCount);
    const u64 expectedSkinnedMeshResolverHits =
        static_cast<u64>(options.targetFrameCount) * expectedSkinnedMeshes;
    const u64 expectedMaterialResolverHits =
        static_cast<u64>(options.targetFrameCount) *
        (expectedMeshes + expectedTransparentStaticWitnessCount);
    const bool skinAnimationEnabled = options.skinAnimationMode == SkinAnimationMode::On;
    const u64 expectedAnimatorPoseChanges =
        skinAnimationEnabled ? options.targetFrameCount : 0U;
    const u64 expectedSkinnedPoseFingerprintChanges =
        skinAnimationEnabled && options.targetFrameCount > 0U
            ? static_cast<u64>(options.targetFrameCount - 1U)
            : 0U;
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount ||
        counters.sceneLightingFrames != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || !uiValid ||
        counters.meshesUploaded != expectedMeshes || counters.materialsLoaded != expectedMaterials || !texturesOk ||
        tangentMeshesUploaded != expectedStaticMeshes ||
        skinnedMeshesUploaded != expectedSkinnedMeshes || expectedSkinnedMeshes != 1U ||
        capture.uploadedSkinnedJointCount() != counters.animatorJointCount ||
        counters.animationClipAssetHandlesPublished != 1U ||
        counters.animationClipAssetHandlesInvalidated != 1U ||
        counters.skinnedPrefabAssetHandlesPublished != 1U ||
        counters.skinnedPrefabAssetHandlesInvalidated != 1U ||
        counters.animatorUpdates != options.targetFrameCount ||
        counters.animatorPoseChanges != expectedAnimatorPoseChanges ||
        counters.animatorJointCount != 2U || counters.skinnedPrefabInstances != 3U ||
        submittedSkinnedMesh3DFrames != options.targetFrameCount ||
        submittedSkinnedMesh3DCount != 1U || visibleSkinnedMesh3DCount != 1U ||
        submittedSkinnedPaletteJointCount != counters.animatorJointCount ||
        firstSubmittedSkinnedPoseFingerprint == 0 || submittedSkinnedPoseFingerprint == 0 ||
        submittedSkinnedPoseFingerprintChanges != expectedSkinnedPoseFingerprintChanges ||
        counters.meshAssetHandlesPublished != expectedMeshes
        || counters.materialAssetHandlesPublished != expectedMaterials
        || counters.meshBindingsRegistered != expectedMeshes
        || counters.materialBindingsRegistered != expectedMaterials
        || counters.meshBindingsReleased != expectedMeshes
        || counters.materialBindingsReleased != expectedMaterials
        || counters.meshRetirementsAccepted != counters.meshesUploaded
        || counters.textureRetirementsAccepted != counters.texturesUploaded
        || counters.meshRetirementRecords != counters.meshesUploaded
        || counters.textureRetirementRecords != counters.texturesUploaded
        || counters.meshRetirementReleased != counters.meshesUploaded
        || counters.textureRetirementReleased != counters.texturesUploaded
        || counters.retirementRecordsLive != 0
        || counters.meshAssetHandlesInvalidated != expectedMeshes
        || counters.materialAssetHandlesInvalidated != expectedMaterials
        || counters.textureAssetHandlesInvalidated != counters.texturesUploaded
        || !counters.cookedEnvironmentMap
        || counters.imageBasedLightingConfigured != imageBasedLightingEnabled
        || environmentMapsUploaded != 1U
        || imageBasedLightingBindings != expectedImageBasedLightingTransitions
        || imageBasedLightingClears != expectedImageBasedLightingTransitions
        || environmentMapRetirements != 1U
        || capture.environmentDiffuseFaceSize() != Tina::Sample3D::ProductEnvironmentDiffuseFaceSize
        || capture.environmentSpecularFaceSize() != Tina::Sample3D::ProductEnvironmentSpecularFaceSize
        || capture.environmentSpecularMipCount() != Tina::Sample3D::ProductEnvironmentSpecularMipCount
        || capture.environmentBrdfWidth() != Tina::Sample3D::ProductEnvironmentBrdfSize
        || capture.environmentBrdfHeight() != Tina::Sample3D::ProductEnvironmentBrdfSize
        || counters.meshFrameResourceResolverHits != expectedStaticMeshResolverHits
        || counters.skinnedMeshFrameResourceResolverHits != expectedSkinnedMeshResolverHits
        || counters.skinnedPoseProviderHits != expectedSkinnedMeshResolverHits
        || counters.materialFrameResourceResolverHits != expectedMaterialResolverHits
        || resources.blendMaterialCount == 0U
        || !counters.transparentWitnessMaterialBound
        || counters.authoredTransparentStaticWitnessCount != expectedTransparentStaticWitnessCount
        || !transparentCaptureValid
        || assetStoreActiveCount != 1U || !prefabAssetResident ||
        !counters.meshBound || !counters.materialTextureBound || counters.catalogCooked != 1 || !counters.gltfCooked ||
        !counters.prefabInstantiated || counters.prefabNodes == 0 || counters.prefabInstances == 0 ||
        !counters.lightingConfigured || counters.directionalLightCount != 3U ||
        counters.cascadedDirectionalShadowCount != 1U ||
        submittedCascadedDirectionalShadowCount != 1U ||
        counters.cascadedDirectionalShadowCascadeCount !=
            Tina::Render::Mesh3DCascadedDirectionalShadow::CascadeCount ||
        submittedCascadedDirectionalShadowCascadeCount !=
            Tina::Render::Mesh3DCascadedDirectionalShadow::CascadeCount ||
        counters.authoredSpotLightShadowCount != 1U || submittedSpotLightShadowCount != 1U ||
        counters.authoredPointLightShadowCount != expectedPointLightShadowCount ||
        submittedPointLightShadowCount != expectedPointLightShadowCount ||
        counters.authoredPointLight3DCount != 3U || counters.pointLight3DCount != 2U ||
        counters.culledPointLight3DCount != 1U ||
        counters.authoredSpotLight3DCount != 3U || counters.spotLight3DCount != 2U ||
        counters.culledSpotLight3DCount != 1U ||
        counters.submittedLightingFrames != options.targetFrameCount ||
        counters.submittedDirectionalLightCount != 3U || !counters.lightingCountsStable ||
        counters.windowMetricsEvents == 0 || !counters.cameraAspectMatchesSurface ||
        !counters.bindingRegistryReleased ||
        !ledgerBalanced ||
        !counters.pixelCaptureAttempted || !counters.pixelCaptureOk || counters.pixelCaptureWidth == 0 ||
        counters.pixelCaptureHeight == 0 ||
        counters.pixelCaptureWidth != counters.framebufferPixelWidth ||
        counters.pixelCaptureHeight != counters.framebufferPixelHeight ||
        counters.pixelCaptureBytes == 0 || counters.pixelFingerprint.empty() ||
        counters.sceneRgbPixelCount == 0 || counters.sceneRgbFingerprint.empty() ||
        (sceneRgbOutputRequested && !counters.sceneRgbOutputWritten) ||
        !pixelGoldenMatched)
    {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("sample", "tina_sample_3d");
        writer.member("evidenceSchema", 16);
        writer.member("message", "lifecycle counters did not match");
        writer.member("frames", counters.frameUpdates);
        writer.member("meshesUploaded", counters.meshesUploaded);
        writer.member("tangentMeshesUploaded", tangentMeshesUploaded);
        writer.member("skinnedMeshesUploaded", skinnedMeshesUploaded);
        writer.member("uploadedSkinnedJointCount", capture.uploadedSkinnedJointCount());
        writer.member("animatorJointCount", counters.animatorJointCount);
        writer.member("animatorUpdates", counters.animatorUpdates);
        writer.member("animatorPoseChanges", counters.animatorPoseChanges);
        writer.member("submittedSkinnedMesh3DFrames", submittedSkinnedMesh3DFrames);
        writer.member("submittedSkinnedMesh3DCount", submittedSkinnedMesh3DCount);
        writer.member("visibleSkinnedMesh3DCount", visibleSkinnedMesh3DCount);
        writer.member("submittedSkinnedPaletteJointCount", submittedSkinnedPaletteJointCount);
        writer.member("firstSubmittedSkinnedPoseFingerprint", firstSubmittedSkinnedPoseFingerprint);
        writer.member("submittedSkinnedPoseFingerprint", submittedSkinnedPoseFingerprint);
        writer.member("submittedSkinnedPoseFingerprintChanges", submittedSkinnedPoseFingerprintChanges);
        writer.member("materialsLoaded", counters.materialsLoaded);
        writer.member("meshAssetHandlesPublished", counters.meshAssetHandlesPublished);
        writer.member("materialAssetHandlesPublished", counters.materialAssetHandlesPublished);
        writer.member("meshBindingsRegistered", counters.meshBindingsRegistered);
        writer.member("materialBindingsRegistered", counters.materialBindingsRegistered);
        writer.member("meshBindingsReleased", counters.meshBindingsReleased);
        writer.member("materialBindingsReleased", counters.materialBindingsReleased);
        writer.member("meshRetirementsAccepted", counters.meshRetirementsAccepted);
        writer.member("textureRetirementsAccepted", counters.textureRetirementsAccepted);
        writer.member("meshRetirementRecords", counters.meshRetirementRecords);
        writer.member("textureRetirementRecords", counters.textureRetirementRecords);
        writer.member("meshRetirementReleased", counters.meshRetirementReleased);
        writer.member("textureRetirementReleased", counters.textureRetirementReleased);
        writer.member("retirementRecordsLive", counters.retirementRecordsLive);
        writer.member("meshAssetHandlesInvalidated", counters.meshAssetHandlesInvalidated);
        writer.member("materialAssetHandlesInvalidated", counters.materialAssetHandlesInvalidated);
        writer.member("textureAssetHandlesInvalidated", counters.textureAssetHandlesInvalidated);
        writer.member("animationClipAssetHandlesPublished", counters.animationClipAssetHandlesPublished);
        writer.member("animationClipAssetHandlesInvalidated", counters.animationClipAssetHandlesInvalidated);
        writer.member("skinnedPrefabAssetHandlesPublished", counters.skinnedPrefabAssetHandlesPublished);
        writer.member("skinnedPrefabAssetHandlesInvalidated", counters.skinnedPrefabAssetHandlesInvalidated);
        writer.member("meshFrameResourceResolverHits", counters.meshFrameResourceResolverHits);
        writer.member("skinnedMeshFrameResourceResolverHits", counters.skinnedMeshFrameResourceResolverHits);
        writer.member("skinnedPoseProviderHits", counters.skinnedPoseProviderHits);
        writer.member("materialFrameResourceResolverHits", counters.materialFrameResourceResolverHits);
        writer.member("assetStoreActiveCount", assetStoreActiveCount);
        writer.member("prefabAssetResident", prefabAssetResident);
        writer.member("texturesUploaded", counters.texturesUploaded);
        writer.member("cookedEnvironmentMap", counters.cookedEnvironmentMap);
        writer.member("environmentMapsUploaded", environmentMapsUploaded);
        writer.member("imageBasedLightingMode", imageBasedLightingModeName(options.imageBasedLightingMode));
        writer.member("pointLightShadowMode", pointLightShadowModeName(options.pointLightShadowMode));
        writer.member("skinAnimationMode", skinAnimationModeName(options.skinAnimationMode));
        writer.member("transparencyMode", transparencyModeName(options.transparencyMode));
        writer.member("blendMaterialCount", resources.blendMaterialCount);
        writer.member("authoredTransparentStaticWitnessCount", counters.authoredTransparentStaticWitnessCount);
        writer.member("transparentWitnessMaterialBound", counters.transparentWitnessMaterialBound);
        writer.member("submittedTransparent3DFrames", submittedTransparent3DFrames);
        writer.member("submittedTransparentStaticMesh3DCount", submittedTransparentStaticMesh3DCount);
        writer.member("submittedTransparentSkinnedMesh3DCount", submittedTransparentSkinnedMesh3DCount);
        writer.member("submittedTransparent3DDrawCount", submittedTransparent3DDrawCount);
        writer.member("submittedTransparent3DSortOrderChecksum", submittedTransparent3DSortOrderChecksum);
        writer.member("transparent3DSortOrderStable", transparent3DSortOrderStable);
        writer.member("imageBasedLightingConfigured", counters.imageBasedLightingConfigured);
        writer.member("imageBasedLightingBindings", imageBasedLightingBindings);
        writer.member("imageBasedLightingClears", imageBasedLightingClears);
        writer.member("environmentMapRetirementsAccepted", environmentMapRetirements);
        writer.member("environmentMapDiffuseFaceSize", capture.environmentDiffuseFaceSize());
        writer.member("environmentMapSpecularFaceSize", capture.environmentSpecularFaceSize());
        writer.member("environmentMapSpecularMipCount", capture.environmentSpecularMipCount());
        writer.member("environmentMapBrdfWidth", capture.environmentBrdfWidth());
        writer.member("environmentMapBrdfHeight", capture.environmentBrdfHeight());
        writer.member("meshBound", counters.meshBound);
        writer.member("materialTextureBound", counters.materialTextureBound);
        writer.member("lightingConfigured", counters.lightingConfigured);
        writer.member("directionalLightCount", counters.directionalLightCount);
        writer.member("cascadedDirectionalShadowCount", counters.cascadedDirectionalShadowCount);
        writer.member("submittedCascadedDirectionalShadowCount", submittedCascadedDirectionalShadowCount);
        writer.member("cascadedDirectionalShadowCascadeCount", counters.cascadedDirectionalShadowCascadeCount);
        writer.member("submittedCascadedDirectionalShadowCascadeCount", submittedCascadedDirectionalShadowCascadeCount);
        writer.member("authoredSpotLightShadowCount", counters.authoredSpotLightShadowCount);
        writer.member("submittedSpotLightShadowCount", submittedSpotLightShadowCount);
        writer.member("authoredPointLightShadowCount", counters.authoredPointLightShadowCount);
        writer.member("submittedPointLightShadowCount", submittedPointLightShadowCount);
        writer.member("authoredPointLight3DCount", counters.authoredPointLight3DCount);
        writer.member("pointLight3DCount", counters.pointLight3DCount);
        writer.member("culledPointLight3DCount", counters.culledPointLight3DCount);
        writer.member("authoredSpotLight3DCount", counters.authoredSpotLight3DCount);
        writer.member("spotLight3DCount", counters.spotLight3DCount);
        writer.member("culledSpotLight3DCount", counters.culledSpotLight3DCount);
        writer.member("submittedLightingFrames", counters.submittedLightingFrames);
        writer.member("submittedDirectionalLightCount", counters.submittedDirectionalLightCount);
        writer.member("lightingCountsStable", counters.lightingCountsStable);
        writer.member("windowMetricsEvents", counters.windowMetricsEvents);
        writer.member("logicalPixelWidth", counters.logicalPixelWidth);
        writer.member("logicalPixelHeight", counters.logicalPixelHeight);
        writer.member("framebufferPixelWidth", counters.framebufferPixelWidth);
        writer.member("framebufferPixelHeight", counters.framebufferPixelHeight);
        writer.member("submittedCameraAspectRatio", counters.submittedCameraAspectRatio);
        writer.member("cameraAspectChanges", counters.cameraAspectChanges);
        writer.member("cameraAspectMatchesSurface", counters.cameraAspectMatchesSurface);
        writer.member("gltfCooked", counters.gltfCooked);
        writer.member("prefabInstantiated", counters.prefabInstantiated);
        writer.member("prefabNodes", counters.prefabNodes);
        writer.member("prefabInstances", counters.prefabInstances);
        writer.member("skinnedPrefabInstances", counters.skinnedPrefabInstances);
        writer.member("catalogCooked", counters.catalogCooked);
        writer.member("meshSlotCount", expectedMeshes);
        writer.member("staticMeshSlotCount", expectedStaticMeshes);
        writer.member("skinnedMeshSlotCount", expectedSkinnedMeshes);
        writer.member("externalGltf", resources.externalGltf);
        writer.member("uiRootsCreated", ui.rootsCreated);
        writer.member("uiRootsReleased", ui.rootsReleased);
        writer.member("uiPanelsCreated", ui.panelsCreated);
        writer.member("uiLabelsCreated", ui.labelsCreated);
        writer.member("uiListViewsCreated", ui.listViewsCreated);
        writer.member("uiTreeViewsCreated", ui.treeViewsCreated);
        writer.member("uiThemeDemoRequested", ui.themeDemoRequested);
        writer.member("uiThemeSwitches", ui.themeSwitches);
        writer.member("uiAutomatedThemeSteps", ui.automatedThemeSteps);
        writer.member("uiAutomatedCollectionSteps", ui.automatedCollectionSteps);
        writer.member("uiTreeExpansionChanges", ui.treeExpansionChanges);
        writer.member("uiListSelectionKey", ui.listSelectionKey);
        writer.member("uiTreeSelectionKey", ui.treeSelectionKey);
        writer.member("uiThemeButtonActivations", ui.themeButtonActivations);
        writer.member("uiThemeInitialLight", ui.initialThemeLight);
        writer.member("uiThemeFinalLight", ui.finalThemeLight);
        writer.member("uiInheritedChromeVerified", ui.inheritedChromeVerified);
        writer.member("uiControlsInitialStateVerified", ui.controlsInitialStateVerified);
        writer.member("uiResponsiveLayoutVerified", ui.responsiveLayoutVerified);
        writer.member("uiProgressUpdates", ui.progressUpdates);
        writer.member("uiProgressFinal", ui.finalProgress);
        writer.member("bindingRegistryReleased", counters.bindingRegistryReleased);
        writer.member("ledgerBalanced", ledgerBalanced);
        writer.member("pixelCaptureAttempted", counters.pixelCaptureAttempted);
        writer.member("pixelCaptureOk", counters.pixelCaptureOk);
        writer.member("pixelCaptureWidth", counters.pixelCaptureWidth);
        writer.member("pixelCaptureHeight", counters.pixelCaptureHeight);
        writer.member("pixelCaptureBytes", counters.pixelCaptureBytes);
        writer.member("pixelFingerprint", counters.pixelFingerprint);
        writer.member("sceneRgbPixelCount", counters.sceneRgbPixelCount);
        writer.beginArrayMember("sceneRgbChannelSums");
        for (const Tina::u64 channelSum : counters.sceneRgbChannelSums)
        {
            writer.element(channelSum);
        }
        writer.endArray();
        writer.member("sceneRgbFingerprint", counters.sceneRgbFingerprint);
        writer.member("sceneRgbOutputRequested", sceneRgbOutputRequested);
        writer.member("sceneRgbOutputWritten", counters.sceneRgbOutputWritten);
        writer.member("pixelGoldenChecked", pixelGoldenChecked);
        writer.member("pixelGoldenMatched", pixelGoldenMatched);
        writer.member("expectPixelFingerprint", options.expectPixelFingerprint);
        writer.endObject();
        std::cerr << '\n';
        return 1;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_3d");
        writer.member("evidenceSchema", 16);
        writer.member("frames", counters.frameUpdates);
        writer.member("gltfCooked", true);
        writer.member("cookedStaticMesh", true);
        writer.member("cookedSkinnedMesh", true);
        writer.member("cookedAnimationClip3D", true);
        writer.member("cookedMaterial", true);
        writer.member("cookedPrefab", true);
        writer.member("cookedEnvironmentMap", true);
        writer.member("prefabInstantiated", true);
        writer.member("sceneExtract", true);
        writer.member("multiMesh", multiMesh);
        writer.member("materialTextureBound", counters.materialTextureBound);
        writer.member("texturesUploaded", counters.texturesUploaded);
        writer.member("meshesUploaded", counters.meshesUploaded);
        writer.member("tangentMeshesUploaded", tangentMeshesUploaded);
        writer.member("skinnedMeshesUploaded", skinnedMeshesUploaded);
        writer.member("uploadedSkinnedJointCount", capture.uploadedSkinnedJointCount());
        writer.member("animatorJointCount", counters.animatorJointCount);
        writer.member("animatorUpdates", counters.animatorUpdates);
        writer.member("animatorPoseChanges", counters.animatorPoseChanges);
        writer.member("submittedSkinnedMesh3DFrames", submittedSkinnedMesh3DFrames);
        writer.member("submittedSkinnedMesh3DCount", submittedSkinnedMesh3DCount);
        writer.member("visibleSkinnedMesh3DCount", visibleSkinnedMesh3DCount);
        writer.member("submittedSkinnedPaletteJointCount", submittedSkinnedPaletteJointCount);
        writer.member("firstSubmittedSkinnedPoseFingerprint", firstSubmittedSkinnedPoseFingerprint);
        writer.member("submittedSkinnedPoseFingerprint", submittedSkinnedPoseFingerprint);
        writer.member("submittedSkinnedPoseFingerprintChanges", submittedSkinnedPoseFingerprintChanges);
        writer.member("environmentMapsUploaded", environmentMapsUploaded);
        writer.member("imageBasedLightingMode", imageBasedLightingModeName(options.imageBasedLightingMode));
        writer.member("pointLightShadowMode", pointLightShadowModeName(options.pointLightShadowMode));
        writer.member("skinAnimationMode", skinAnimationModeName(options.skinAnimationMode));
        writer.member("transparencyMode", transparencyModeName(options.transparencyMode));
        writer.member("blendMaterialCount", resources.blendMaterialCount);
        writer.member("authoredTransparentStaticWitnessCount", counters.authoredTransparentStaticWitnessCount);
        writer.member("transparentWitnessMaterialBound", counters.transparentWitnessMaterialBound);
        writer.member("submittedTransparent3DFrames", submittedTransparent3DFrames);
        writer.member("submittedTransparentStaticMesh3DCount", submittedTransparentStaticMesh3DCount);
        writer.member("submittedTransparentSkinnedMesh3DCount", submittedTransparentSkinnedMesh3DCount);
        writer.member("submittedTransparent3DDrawCount", submittedTransparent3DDrawCount);
        writer.member("submittedTransparent3DSortOrderChecksum", submittedTransparent3DSortOrderChecksum);
        writer.member("transparent3DSortOrderStable", transparent3DSortOrderStable);
        writer.member("imageBasedLightingConfigured", counters.imageBasedLightingConfigured);
        writer.member("imageBasedLightingBindings", imageBasedLightingBindings);
        writer.member("imageBasedLightingClears", imageBasedLightingClears);
        writer.member("environmentMapRetirementsAccepted", environmentMapRetirements);
        writer.member("environmentMapDiffuseFaceSize", capture.environmentDiffuseFaceSize());
        writer.member("environmentMapSpecularFaceSize", capture.environmentSpecularFaceSize());
        writer.member("environmentMapSpecularMipCount", capture.environmentSpecularMipCount());
        writer.member("environmentMapBrdfWidth", capture.environmentBrdfWidth());
        writer.member("environmentMapBrdfHeight", capture.environmentBrdfHeight());
        writer.member("materialsLoaded", counters.materialsLoaded);
        writer.member("prefabNodes", counters.prefabNodes);
        writer.member("meshAssetHandlesPublished", counters.meshAssetHandlesPublished);
        writer.member("materialAssetHandlesPublished", counters.materialAssetHandlesPublished);
        writer.member("meshBindingsRegistered", counters.meshBindingsRegistered);
        writer.member("materialBindingsRegistered", counters.materialBindingsRegistered);
        writer.member("meshBindingsReleased", counters.meshBindingsReleased);
        writer.member("materialBindingsReleased", counters.materialBindingsReleased);
        writer.member("meshRetirementsAccepted", counters.meshRetirementsAccepted);
        writer.member("textureRetirementsAccepted", counters.textureRetirementsAccepted);
        writer.member("meshRetirementRecords", counters.meshRetirementRecords);
        writer.member("textureRetirementRecords", counters.textureRetirementRecords);
        writer.member("meshRetirementReleased", counters.meshRetirementReleased);
        writer.member("textureRetirementReleased", counters.textureRetirementReleased);
        writer.member("retirementRecordsLive", counters.retirementRecordsLive);
        writer.member("meshAssetHandlesInvalidated", counters.meshAssetHandlesInvalidated);
        writer.member("materialAssetHandlesInvalidated", counters.materialAssetHandlesInvalidated);
        writer.member("textureAssetHandlesInvalidated", counters.textureAssetHandlesInvalidated);
        writer.member("animationClipAssetHandlesPublished", counters.animationClipAssetHandlesPublished);
        writer.member("animationClipAssetHandlesInvalidated", counters.animationClipAssetHandlesInvalidated);
        writer.member("skinnedPrefabAssetHandlesPublished", counters.skinnedPrefabAssetHandlesPublished);
        writer.member("skinnedPrefabAssetHandlesInvalidated", counters.skinnedPrefabAssetHandlesInvalidated);
        writer.member("meshFrameResourceResolverHits", counters.meshFrameResourceResolverHits);
        writer.member("skinnedMeshFrameResourceResolverHits", counters.skinnedMeshFrameResourceResolverHits);
        writer.member("skinnedPoseProviderHits", counters.skinnedPoseProviderHits);
        writer.member("materialFrameResourceResolverHits", counters.materialFrameResourceResolverHits);
        writer.member("assetStoreActiveCount", assetStoreActiveCount);
        writer.member("prefabAssetResident", prefabAssetResident);
        writer.member("prefabInstances", counters.prefabInstances);
        writer.member("skinnedPrefabInstances", counters.skinnedPrefabInstances);
        writer.member("meshSlotCount", expectedMeshes);
        writer.member("staticMeshSlotCount", expectedStaticMeshes);
        writer.member("skinnedMeshSlotCount", expectedSkinnedMeshes);
        writer.member("externalGltf", resources.externalGltf);
        writer.member("completePbrFixture", resources.completePbrFixture);
        writer.member("materialFactorsBound", counters.materialFactorsBound);
        writer.member("materialMrTextureBound", counters.materialMrTextureBound);
        writer.member("materialNormalTextureBound", counters.materialNormalTextureBound);
        writer.member("lightingConfigured", counters.lightingConfigured);
        writer.member("directionalLightCount", counters.directionalLightCount);
        writer.member("cascadedDirectionalShadowCount", counters.cascadedDirectionalShadowCount);
        writer.member("submittedCascadedDirectionalShadowCount", submittedCascadedDirectionalShadowCount);
        writer.member("cascadedDirectionalShadowCascadeCount", counters.cascadedDirectionalShadowCascadeCount);
        writer.member("submittedCascadedDirectionalShadowCascadeCount", submittedCascadedDirectionalShadowCascadeCount);
        writer.member("authoredSpotLightShadowCount", counters.authoredSpotLightShadowCount);
        writer.member("submittedSpotLightShadowCount", submittedSpotLightShadowCount);
        writer.member("authoredPointLightShadowCount", counters.authoredPointLightShadowCount);
        writer.member("submittedPointLightShadowCount", submittedPointLightShadowCount);
        writer.member("authoredPointLight3DCount", counters.authoredPointLight3DCount);
        writer.member("pointLight3DCount", counters.pointLight3DCount);
        writer.member("culledPointLight3DCount", counters.culledPointLight3DCount);
        writer.member("authoredSpotLight3DCount", counters.authoredSpotLight3DCount);
        writer.member("spotLight3DCount", counters.spotLight3DCount);
        writer.member("culledSpotLight3DCount", counters.culledSpotLight3DCount);
        writer.member("sceneLightingFrames", counters.sceneLightingFrames);
        writer.member("submittedLightingFrames", counters.submittedLightingFrames);
        writer.member("submittedDirectionalLightCount", counters.submittedDirectionalLightCount);
        writer.member("lightingCountsStable", counters.lightingCountsStable);
        writer.member("windowMetricsEvents", counters.windowMetricsEvents);
        writer.member("logicalPixelWidth", counters.logicalPixelWidth);
        writer.member("logicalPixelHeight", counters.logicalPixelHeight);
        writer.member("framebufferPixelWidth", counters.framebufferPixelWidth);
        writer.member("framebufferPixelHeight", counters.framebufferPixelHeight);
        writer.member("submittedCameraAspectRatio", counters.submittedCameraAspectRatio);
        writer.member("cameraAspectChanges", counters.cameraAspectChanges);
        writer.member("cameraAspectMatchesSurface", counters.cameraAspectMatchesSurface);
        writer.member("bindingRegistryReleased", counters.bindingRegistryReleased);
        if (resources.externalGltf || resources.completePbrFixture)
        {
            writer.member("gltfPath", resources.gltfSourcePath);
        }
        writer.member("instanceBatchesPerFrame", expectedStaticMeshes);
        writer.member("catalogCooked", counters.catalogCooked);
        writer.member("stateExits", counters.stateExits);
        writer.member("uiRootsCreated", ui.rootsCreated);
        writer.member("uiRootsReleased", ui.rootsReleased);
        writer.member("uiPanelsCreated", ui.panelsCreated);
        writer.member("uiLabelsCreated", ui.labelsCreated);
        writer.member("uiButtonsCreated", ui.buttonsCreated);
        writer.member("uiCheckboxesCreated", ui.checkboxesCreated);
        writer.member("uiSlidersCreated", ui.slidersCreated);
        writer.member("uiProgressBarsCreated", ui.progressBarsCreated);
        writer.member("uiListViewsCreated", ui.listViewsCreated);
        writer.member("uiTreeViewsCreated", ui.treeViewsCreated);
        writer.member("uiThemeDemoRequested", ui.themeDemoRequested);
        writer.member("uiThemeSwitches", ui.themeSwitches);
        writer.member("uiAutomatedThemeSteps", ui.automatedThemeSteps);
        writer.member("uiAutomatedCollectionSteps", ui.automatedCollectionSteps);
        writer.member("uiTreeExpansionChanges", ui.treeExpansionChanges);
        writer.member("uiListSelectionKey", ui.listSelectionKey);
        writer.member("uiTreeSelectionKey", ui.treeSelectionKey);
        writer.member("uiThemeButtonActivations", ui.themeButtonActivations);
        writer.member("uiCheckboxActivations", ui.checkboxActivations);
        writer.member("uiSliderChanges", ui.sliderChanges);
        writer.member("uiThemeInitialLight", ui.initialThemeLight);
        writer.member("uiThemeFinalLight", ui.finalThemeLight);
        writer.member("uiInheritedChromeVerified", ui.inheritedChromeVerified);
        writer.member("uiControlsInitialStateVerified", ui.controlsInitialStateVerified);
        writer.member("uiResponsiveLayoutVerified", ui.responsiveLayoutVerified);
        writer.member("uiAutoRotateFinal", ui.autoRotate);
        writer.member("uiRotationSpeedFinal", ui.rotationSpeed);
        writer.member("uiProgressUpdates", ui.progressUpdates);
        writer.member("uiProgressFinal", ui.finalProgress);
        writer.member("applicationShutdowns", counters.applicationShutdowns);
        writer.member("engineHostDestroyed", true);
        writer.member("renderResourceLedgerBalanced", true);
        writer.member("pixelCaptureAttempted", counters.pixelCaptureAttempted);
        writer.member("pixelCaptureOk", counters.pixelCaptureOk);
        writer.member("pixelCaptureWidth", counters.pixelCaptureWidth);
        writer.member("pixelCaptureHeight", counters.pixelCaptureHeight);
        writer.member("pixelCaptureBytes", counters.pixelCaptureBytes);
        writer.member("pixelFingerprint", counters.pixelFingerprint);
        writer.member("sceneRgbPixelCount", counters.sceneRgbPixelCount);
        writer.beginArrayMember("sceneRgbChannelSums");
        for (const Tina::u64 channelSum : counters.sceneRgbChannelSums)
        {
            writer.element(channelSum);
        }
        writer.endArray();
        writer.member("sceneRgbFingerprint", counters.sceneRgbFingerprint);
        writer.member("sceneRgbOutputRequested", sceneRgbOutputRequested);
        writer.member("sceneRgbOutputWritten", counters.sceneRgbOutputWritten);
        writer.member("pixelGoldenChecked", pixelGoldenChecked);
        writer.member("pixelGoldenMatched", pixelGoldenMatched);
        writer.member("expectPixelFingerprint", options.expectPixelFingerprint);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    }
    catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory, "The 3D product sample ran out of memory"};
        writeError(error);
        return 1;
    }
    catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 3D product sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    }
    catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the 3D product sample boundary"};
        writeError(error);
        return 1;
    }
}
