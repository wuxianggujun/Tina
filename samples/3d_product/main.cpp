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
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PrefabInstantiate.hpp>
#include <tina/scene/World.hpp>

#include "DeviceCapture.hpp"
#include "Product3DUI.hpp"
#include "SampleTempDirectory.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
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
#include <vector>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
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

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    // Empty → in-memory two-mesh fixture. Non-empty → cook external .gltf/.glb path.
    std::string gltfPath{};
    // Empty = capture-only; non-empty = require an exact machine-local pixel match.
    std::string expectPixelFingerprint{};
    Tina::Sample3D::Product3DUITheme initialUiTheme = Tina::Sample3D::Product3DUITheme::Dark;
    bool uiThemeDemo = false;
    bool help = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
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
    bool meshBound = false;
    bool materialTextureBound = false;
    bool materialFactorsBound = false;
    bool materialMrTextureBound = false;
    bool materialNormalTextureBound = false;
    u32 directionalLightCount = 0;
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
};

using DeviceCapture = Tina::Sample3D::DeviceCapture;

void recordPixelCapture(LifecycleCounters& counters, const Tina::Render::Rgba8FrameCapture& capture)
{
    if (capture.empty())
    {
        return;
    }
    auto pixelHash = Tina::Core::digestContentHashV1(capture.rgba8Pixels);
    if (!pixelHash.has_value() || !pixelHash->hasValue())
    {
        return;
    }
    counters.pixelCaptureOk = true;
    counters.pixelCaptureWidth = capture.width;
    counters.pixelCaptureHeight = capture.height;
    counters.pixelCaptureBytes = static_cast<u64>(capture.byteCount());
    counters.pixelFingerprint = contentHashToHex(*pixelHash);
}

struct ProductMeshSlot final {
    Tina::Asset::AssetHandle meshAsset{};
    Tina::Asset::AssetHandle materialAsset{};
    Tina::Asset::AssetHandle textureAsset{};
    Tina::Asset::AssetHandle metallicRoughnessTextureAsset{};
    Tina::Asset::AssetHandle normalTextureAsset{};
    Tina::Render::RenderLinearColor materialColor{.red = 0.2F, .green = 0.6F, .blue = 0.9F, .alpha = 1.0F};
    float metallicFactor = 0.0F;
    float roughnessFactor = 1.0F;
    float meshBoundsRadius = 1.75F;
    Tina::Core::AssetId meshId{};
    Tina::Core::AssetId materialId{};
    Tina::Core::AssetId textureId{};
    Tina::Core::AssetId metallicRoughnessTextureId{};
    Tina::Core::AssetId normalTextureId{};
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
    std::array<ProductMeshSlot, MaxProductMeshSlots> meshes{};
    std::array<ProductTextureAsset, MaxProductMeshSlots * 3U> textures{};
    u32 meshSlotCount = 0;
    u32 textureAssetCount = 0;
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

// Fallback only when TINA_COMPLETE_PBR_GLTF_FIXTURE is not compiled in (minimal two-mesh + baseColor).
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

#if defined(TINA_COMPLETE_PBR_GLTF_FIXTURE)
[[nodiscard]] bool tryResolveCompletePbrFixture(std::filesystem::path& outPath) noexcept
{
    std::error_code ec;
    outPath = std::filesystem::path{TINA_COMPLETE_PBR_GLTF_FIXTURE};
    return std::filesystem::exists(outPath, ec) && !ec;
}
#endif

void writeJsonString(std::ostream& output, std::string_view value)
{
    output.put('"');
    for (const unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
        {
            output.put('\\');
            output.put(static_cast<char>(byte));
        }
        else if (byte == '\n')
        {
            output << "\\n";
        }
        else if (byte >= 0x20U)
        {
            output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d\",\"code\":";
    writeJsonString(std::cerr, errorCodeName(error.code));
    std::cerr << ",\"message\":";
    writeJsonString(std::cerr, error.message);
    if (!error.context.empty())
    {
        std::cerr << ",\"context\":[";
        for (std::size_t index = 0; index < error.context.size(); ++index)
        {
            if (index != 0)
            {
                std::cerr << ',';
            }
            std::cerr << "{\"operation\":";
            writeJsonString(std::cerr, error.context[index].operation);
            std::cerr << ",\"detail\":";
            writeJsonString(std::cerr, error.context[index].detail);
            std::cerr << '}';
        }
        std::cerr << ']';
    }
    std::cerr << "}\n";
}

void printUsage()
{
    std::cerr
        << "tina_sample_3d [options]\n"
        << "  Product 3D gate: glTF/GLB cook -> Catalog -> GPU mesh/material bind -> Prefab/Scene/bgfx.\n"
        << "\n"
        << "  --frames=N              exit after N frames (default " << DefaultFrameCount << ")\n"
        << "  --frame-delay-ms=N      sleep N ms per frame (default 0)\n"
        << "  --gltf=<path>           cook external .gltf/.glb from disk (omit = built-in two-mesh fixture)\n"
        << "  --gltf <path>           same as --gltf=<path>\n"
        << "  --ui-theme=dark|light   select the initial retained UI theme (default dark)\n"
        << "  --ui-theme-demo         exercise initial -> alternate -> initial theme in UI phase\n"
        << "  --expect-pixel-fingerprint=<32 lowercase hex chars>\n"
        << "                           require an exact machine-local RGBA8 frame match\n"
        << "  --help, -h              print this help\n"
        << "\n"
        << "External path is opt-in. Runtime never parses glTF; only the cooker (cgltf) does.\n"
        << "Unsupported glTF features (multi-primitive mesh, Draco, skin, morph, ...) fail with\n"
        << "structured JSON on stderr (status=error, code, message, optional context).\n";
}

template <typename Value> [[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";
    constexpr std::string_view GltfPrefix = "--gltf=";
    constexpr std::string_view UiThemePrefix = "--ui-theme=";
    constexpr std::string_view PixelFingerprintPrefix = "--expect-pixel-fingerprint=";
    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasGltf = false;
    bool hasUiTheme = false;
    bool hasPixelFingerprint = false;

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
            if (hasFrames || !parseUnsigned(argument.substr(FramesPrefix.size()), options.targetFrameCount) ||
                options.targetFrameCount == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must appear once and be greater than zero");
            }
            hasFrames = true;
        }
        else if (argument.starts_with(DelayPrefix))
        {
            if (hasDelay || !parseUnsigned(argument.substr(DelayPrefix.size()), options.frameDelayMilliseconds))
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must appear once and be unsigned");
            }
            hasDelay = true;
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

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    return path.string();
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
    auto workRoot = Tina::Sample::createUniqueTempDirectory("tina_sample_3d_gltf");
    if (!workRoot)
    {
        return Tina::Core::failure(std::move(workRoot.error()));
    }
    resources.workRoot = std::move(*workRoot);
    resources.catalogRoot = resources.workRoot / "catalog";
    resources.externalGltf = !options.gltfPath.empty();
    resources.completePbrFixture = false;
    resources.gltfSourcePath = options.gltfPath;
    auto workRootCleanup = Tina::Core::makeScopeExit([&resources]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(resources.workRoot, cleanupError);
    });

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
#if defined(TINA_COMPLETE_PBR_GLTF_FIXTURE)
    else if (tryResolveCompletePbrFixture(gltfPath))
    {
        resources.completePbrFixture = true;
        resources.gltfSourcePath = toUtf8(gltfPath);
    }
#endif
    else
    {
        if (auto status = writeFallbackGltfFixture(resources.workRoot, gltfPath); !status)
        {
            return status;
        }
    }
    counters.completePbrFixture = resources.completePbrFixture;

    // Cook via GltfCook (cgltf PRIVATE). Unsupported features fail with structured Asset errors.
    auto request = Tina::Asset::cookGltfFileToCatalogRequest(toUtf8(gltfPath), Tina::Asset::GltfCookIds{});
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

    std::array<Tina::Core::AssetId, MaxProductMeshSlots> cookedMeshIds{};
    std::array<Tina::Core::AssetId, MaxProductMeshSlots> cookedMaterialIds{};
    u32 meshIdCount = 0;
    u32 materialIdCount = 0;
    Tina::Core::AssetId cookedPrefabId{};
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == Tina::AssetFormat::AssetKind::StaticMesh)
        {
            if (meshIdCount >= MaxProductMeshSlots)
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::CapacityExceeded,
                                        "glTF cook produced more StaticMesh assets than product slot cap"};
                error.addContext("maxProductMeshSlots", std::to_string(MaxProductMeshSlots));
                return Tina::Core::failure(std::move(error));
            }
            cookedMeshIds[meshIdCount++] = asset.assetId;
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
        else if (asset.assetKind == Tina::AssetFormat::AssetKind::Prefab)
        {
            cookedPrefabId = asset.assetId;
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
    // Built-in fixture is the multi-mesh product gate (exactly two meshes).
    if (!resources.externalGltf && meshIdCount != 2U)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "built-in fixture must cook exactly two meshes");
    }

    const u32 slotCount = meshIdCount;
    auto assetSystem = Tina::Asset::AssetSystem::Create({
        .storeCapacity = static_cast<Tina::usize>(slotCount) * 5U + 1U,
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
        return id;
    };

    for (auto& asset : request->assets)
    {
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

    for (u32 slot = 0; slot < slotCount; ++slot)
    {
        ProductMeshSlot& productMesh = resources.meshes[slot];
        productMesh.meshId = productMeshIds[slot];
        productMesh.materialId = productMaterialIds[slot];

        auto meshAsset = Tina::Asset::loadCookedAssetFromCatalog(
            toUtf8(resources.catalogRoot), *catalog, productMesh.meshId,
            Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
        if (!meshAsset)
        {
            return Tina::Core::failure(std::move(meshAsset.error()));
        }
        auto meshView = Tina::Asset::parseStaticMeshFromCooked(*meshAsset);
        if (!meshView)
        {
            return Tina::Core::failure(std::move(meshView.error()));
        }
        if (meshView->vertexCount == 0 || meshView->indexCount == 0)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked glTF StaticMesh is empty");
        }
        productMesh.meshBoundsRadius = meshView->boundsRadius > 0.0F ? meshView->boundsRadius : 1.75F;
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
        if (!resources.externalGltf && !material->hasBaseColorTexture)
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
    workRootCleanup.release();
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
        world_.reset();
        prefabEntities_.clear();
        releaseProductGpuResources();
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        auto* device = capture_->get();
        if (device == nullptr || resources_->meshSlotCount == 0 || !resources_->assetSystem.has_value())
        {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "render device, AssetSystem, or product mesh slots missing");
        }
        auto registry = Tina::Asset::Mesh3DBindingRegistry::Create(
            *resources_->assetSystem,
            *device,
            Tina::Asset::Mesh3DBindingRegistryConfig{
                .meshCapacity = resources_->meshSlotCount,
                .materialCapacity = resources_->meshSlotCount,
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
            world_.reset();
            prefabEntities_.clear();
            releaseProductGpuResources();
        });

        for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
        {
            ProductMeshSlot& productMesh = resources_->meshes[slot];
            const Tina::Asset::CookedAssetFile* meshFile =
                resources_->assetSystem->tryGet(productMesh.meshAsset);
            if (meshFile == nullptr)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked StaticMesh missing for product slot");
            }
            auto mesh = Tina::Asset::uploadStaticMeshFromCooked(*device, *meshFile);
            if (!mesh)
            {
                return Tina::Core::failure(std::move(mesh.error()));
            }
            Tina::Render::GpuMeshId gpuMesh = *mesh;
            auto meshCleanup = Tina::Core::makeScopeExit([device, &gpuMesh]() noexcept {
                if (gpuMesh)
                {
                    (void)device->destroyStaticMesh(gpuMesh);
                }
            });
            auto meshBinding = mesh3DBindings_->registerMeshBinding(productMesh.meshAsset, gpuMesh);
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
        counters_->meshBound = true;
        bool everyBaseTextureBound = true;
        bool everyCompletePbrTextureBound = true;
        for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
        {
            const ProductMeshSlot& productMesh = resources_->meshes[slot];
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

        // Bounded N-light submission for experimental MR (sphere grid readability).
        if (auto* device = capture_->get(); device != nullptr)
        {
            constexpr std::array<Tina::Render::Mesh3DDirectionalLight, 3> ProductLights{
                Tina::Render::Mesh3DDirectionalLight{
                    .directionTowardLightX = 0.35F,
                    .directionTowardLightY = 0.9F,
                    .directionTowardLightZ = 0.4F,
                    .colorR = 1.0F,
                    .colorG = 0.98F,
                    .colorB = 0.92F,
                },
                Tina::Render::Mesh3DDirectionalLight{
                    .directionTowardLightX = -0.55F,
                    .directionTowardLightY = 0.25F,
                    .directionTowardLightZ = -0.35F,
                    .colorR = 0.28F,
                    .colorG = 0.34F,
                    .colorB = 0.45F,
                },
                Tina::Render::Mesh3DDirectionalLight{
                    .directionTowardLightX = 0.15F,
                    .directionTowardLightY = 0.45F,
                    .directionTowardLightZ = -0.9F,
                    .colorR = 0.14F,
                    .colorG = 0.18F,
                    .colorB = 0.30F,
                },
            };
            if (auto status = device->setMesh3DLighting(Tina::Render::Mesh3DLightingDesc{
                    .directionalLights = ProductLights,
                    .ambientScale = 0.16F,
                });
                !status)
            {
                return status;
            }
            counters_->directionalLightCount = static_cast<u32>(ProductLights.size());
            counters_->lightingConfigured = true;
        }

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
        auto instances = Tina::Scene::instantiatePrefab(
            *world_,
            prefab->view,
            Tina::Scene::PrefabMeshBinding{
                .mesh = resources_->meshes[0].meshAsset,
                .material = resources_->meshes[0].materialAsset,
                .localBounds = {.radius = resources_->meshes[0].meshBoundsRadius},
                .baseColorFactor = resources_->meshes[0].materialColor,
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
            });
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
        for (const auto entity : prefabEntities_)
        {
            const auto* mesh = world_->meshRenderer3D(entity);
            const auto* worldXf = world_->worldTransform(entity);
            if (mesh == nullptr || worldXf == nullptr)
            {
                continue;
            }
            const float r = mesh->localBounds.radius > 0.0F ? mesh->localBounds.radius : 1.0F;
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
        world_.reset();
        prefabEntities_.clear();
        releaseProductGpuResources();
        ui_.release();
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
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
                            .pixelWidth = 1280,
                            .pixelHeight = 720,
                        },
                    .mesh3DBindingResolver = Tina::Asset::AssetFrameResourceResolver{
                        .userData = const_cast<Product3DState*>(this),
                        .resolve = &resolveProductMeshBinding,
                    },
                    .material3DBindingResolver = Tina::Asset::AssetFrameResourceResolver{
                        .userData = const_cast<Product3DState*>(this),
                        .resolve = &resolveProductMaterialBinding,
                    },
                });
            !status)
        {
            return status;
        }
        ++counters_->renderExtractions;
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

    void releaseProductGpuResources() noexcept
    {
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
    DeviceCapture* capture_ = nullptr;
    Tina::Sample3D::Product3DUI ui_;
    std::optional<Tina::Asset::Mesh3DBindingRegistry> mesh3DBindings_{};
    mutable std::optional<Tina::Scene::World> world_{};
    float rotationHalfAngle_ = 0.0F;
    Tina::Scene::EntityId cameraEntity_{};
    std::vector<Tina::Scene::EntityId> prefabEntities_{};
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

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina vNext 3D Product";
    config.primaryWindow.title = "Tina vNext - glTF Prefab Product 3D";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    // MetalRoughSpheres-scale external models: one item/batch per mesh instance.
    config.renderSceneCapacities.mesh3DItemCapacity = MaxProductMeshSlots;
    config.renderSceneCapacities.mesh3DBatchCapacity = MaxProductMeshSlots;
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
    auto workRootCleanup = Tina::Core::makeScopeExit([&resources]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(resources.workRoot, cleanupError);
    });

    DeviceCapture capture;
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&capture](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
            return Tina::Sample3D::wrapCapturingRenderDevice(std::move(device), capture);
        };
    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(), std::move(desktopOptions));
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
        recordPixelCapture(counters, *capture.lastCapture());
    }
    else if (Tina::Render::IRenderDevice* device = capture.get(); device != nullptr)
    {
        auto captured = device->capturePrimaryFrameRgba8();
        if (captured.has_value())
        {
            recordPixelCapture(counters, *captured);
        }
    }

    const bool ledgerBalanced =
        capture.get() == nullptr || capture.get()->statistics().liveResources == 0;
    hostResult->reset();

    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }
    const u32 expectedMeshes = resources.meshSlotCount;
    auto& assetSystem = *resources.assetSystem;
    for (const Tina::Asset::AssetRetirementRecord& record : assetSystem.retirement().records())
    {
        if (record.kind == Tina::Asset::AssetRetirementKind::GpuStaticMesh)
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
    for (u32 index = 0; index < resources.textureAssetCount; ++index)
    {
        const Tina::Asset::AssetHandle texture = resources.textures[index].handle;
        if (assetSystem.state(texture) == Tina::Asset::AssetLogicalState::Unloaded &&
            assetSystem.tryGet(texture) == nullptr)
        {
            ++counters.textureAssetHandlesInvalidated;
        }
    }
    const Tina::usize assetStoreActiveCount =
        assetSystem.store().activeCount();
    const bool prefabAssetResident = assetSystem.tryGet(resources.prefabAsset) != nullptr;
    const bool multiMesh = expectedMeshes >= 2U;
    const bool texturesOk = resources.externalGltf
                                ? true
                                : (resources.completePbrFixture
                                       ? (counters.texturesUploaded == resources.textureAssetCount &&
                                          resources.textureAssetCount >= 3U &&
                                          counters.materialMrTextureBound && counters.materialNormalTextureBound &&
                                          counters.materialFactorsBound)
                                       : (counters.texturesUploaded >= expectedMeshes));
    const bool pixelGoldenChecked = !options.expectPixelFingerprint.empty();
    const bool pixelGoldenMatched =
        !pixelGoldenChecked || counters.pixelFingerprint == options.expectPixelFingerprint;
    const auto& ui = counters.ui;
    const bool expectedInitialThemeLight = options.initialUiTheme == Tina::Sample3D::Product3DUITheme::Light;
    const bool unattendedThemeValid =
        ui.themeButtonActivations != 0 ||
        (ui.themeSwitches == (options.uiThemeDemo ? 2U : 0U) &&
         ui.finalThemeLight == expectedInitialThemeLight);
    const bool uiValid =
        ui.rootsCreated == 1 && ui.rootsReleased == 1 && !ui.rootAlive && ui.panelsCreated == 5 &&
        ui.labelsCreated == 9 && ui.buttonsCreated == 1 && ui.checkboxesCreated == 1 &&
        ui.slidersCreated == 1 && ui.progressBarsCreated == 1 && ui.themeDemoRequested == options.uiThemeDemo &&
        ui.initialThemeLight == expectedInitialThemeLight &&
        ui.automatedThemeSteps == (options.uiThemeDemo ? 2U : 0U) && unattendedThemeValid &&
        ui.inheritedChromeVerified && ui.controlsInitialStateVerified &&
        ui.progressUpdates >= options.targetFrameCount && ui.finalProgress >= 99.9F;
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || !uiValid ||
        counters.meshesUploaded != expectedMeshes || counters.materialsLoaded != expectedMeshes || !texturesOk ||
        counters.meshAssetHandlesPublished != expectedMeshes
        || counters.materialAssetHandlesPublished != expectedMeshes
        || counters.meshBindingsRegistered != expectedMeshes
        || counters.materialBindingsRegistered != expectedMeshes
        || counters.meshBindingsReleased != expectedMeshes
        || counters.materialBindingsReleased != expectedMeshes
        || counters.meshRetirementsAccepted != counters.meshesUploaded
        || counters.textureRetirementsAccepted != counters.texturesUploaded
        || counters.meshRetirementRecords != counters.meshesUploaded
        || counters.textureRetirementRecords != counters.texturesUploaded
        || counters.meshRetirementReleased != counters.meshesUploaded
        || counters.textureRetirementReleased != counters.texturesUploaded
        || counters.retirementRecordsLive != 0
        || counters.meshAssetHandlesInvalidated != expectedMeshes
        || counters.materialAssetHandlesInvalidated != expectedMeshes
        || counters.textureAssetHandlesInvalidated != counters.texturesUploaded
        || counters.meshFrameResourceResolverHits == 0
        || counters.materialFrameResourceResolverHits == 0
        || assetStoreActiveCount != 1U || !prefabAssetResident ||
        !counters.meshBound || !counters.materialTextureBound || counters.catalogCooked != 1 || !counters.gltfCooked ||
        !counters.prefabInstantiated || counters.prefabNodes == 0 || counters.prefabInstances == 0 ||
        !counters.lightingConfigured || counters.directionalLightCount != 3U || !counters.bindingRegistryReleased ||
        !ledgerBalanced ||
        !counters.pixelCaptureAttempted || !counters.pixelCaptureOk || counters.pixelCaptureWidth == 0 ||
        counters.pixelCaptureHeight == 0 || counters.pixelCaptureBytes == 0 || counters.pixelFingerprint.empty() ||
        !pixelGoldenMatched)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d\","
                     "\"message\":\"lifecycle counters did not match\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"meshesUploaded\":" << counters.meshesUploaded
                  << ",\"materialsLoaded\":" << counters.materialsLoaded
                  << ",\"meshAssetHandlesPublished\":" << counters.meshAssetHandlesPublished
                  << ",\"materialAssetHandlesPublished\":" << counters.materialAssetHandlesPublished
                  << ",\"meshBindingsRegistered\":" << counters.meshBindingsRegistered
                  << ",\"materialBindingsRegistered\":" << counters.materialBindingsRegistered
                  << ",\"meshBindingsReleased\":" << counters.meshBindingsReleased
                  << ",\"materialBindingsReleased\":" << counters.materialBindingsReleased
                  << ",\"meshRetirementsAccepted\":" << counters.meshRetirementsAccepted
                  << ",\"textureRetirementsAccepted\":" << counters.textureRetirementsAccepted
                  << ",\"meshRetirementRecords\":" << counters.meshRetirementRecords
                  << ",\"textureRetirementRecords\":" << counters.textureRetirementRecords
                  << ",\"meshRetirementReleased\":" << counters.meshRetirementReleased
                  << ",\"textureRetirementReleased\":" << counters.textureRetirementReleased
                  << ",\"retirementRecordsLive\":" << counters.retirementRecordsLive
                  << ",\"meshAssetHandlesInvalidated\":" << counters.meshAssetHandlesInvalidated
                  << ",\"materialAssetHandlesInvalidated\":" << counters.materialAssetHandlesInvalidated
                  << ",\"textureAssetHandlesInvalidated\":" << counters.textureAssetHandlesInvalidated
                  << ",\"meshFrameResourceResolverHits\":" << counters.meshFrameResourceResolverHits
                  << ",\"materialFrameResourceResolverHits\":" << counters.materialFrameResourceResolverHits
                  << ",\"assetStoreActiveCount\":" << assetStoreActiveCount
                  << ",\"prefabAssetResident\":" << (prefabAssetResident ? "true" : "false")
                  << ",\"texturesUploaded\":" << counters.texturesUploaded
                  << ",\"meshBound\":" << (counters.meshBound ? "true" : "false")
                  << ",\"materialTextureBound\":" << (counters.materialTextureBound ? "true" : "false")
                  << ",\"lightingConfigured\":" << (counters.lightingConfigured ? "true" : "false")
                  << ",\"directionalLightCount\":" << counters.directionalLightCount
                  << ",\"gltfCooked\":" << (counters.gltfCooked ? "true" : "false")
                  << ",\"prefabInstantiated\":" << (counters.prefabInstantiated ? "true" : "false")
                  << ",\"prefabNodes\":" << counters.prefabNodes
                  << ",\"prefabInstances\":" << counters.prefabInstances
                  << ",\"catalogCooked\":" << counters.catalogCooked
                  << ",\"meshSlotCount\":" << expectedMeshes
                  << ",\"externalGltf\":" << (resources.externalGltf ? "true" : "false")
                  << ",\"uiRootsCreated\":" << ui.rootsCreated
                  << ",\"uiRootsReleased\":" << ui.rootsReleased
                  << ",\"uiPanelsCreated\":" << ui.panelsCreated
                  << ",\"uiLabelsCreated\":" << ui.labelsCreated
                  << ",\"uiThemeDemoRequested\":" << (ui.themeDemoRequested ? "true" : "false")
                  << ",\"uiThemeSwitches\":" << ui.themeSwitches
                  << ",\"uiAutomatedThemeSteps\":" << ui.automatedThemeSteps
                  << ",\"uiThemeButtonActivations\":" << ui.themeButtonActivations
                  << ",\"uiThemeInitialLight\":" << (ui.initialThemeLight ? "true" : "false")
                  << ",\"uiThemeFinalLight\":" << (ui.finalThemeLight ? "true" : "false")
                  << ",\"uiInheritedChromeVerified\":"
                  << (ui.inheritedChromeVerified ? "true" : "false")
                  << ",\"uiControlsInitialStateVerified\":"
                  << (ui.controlsInitialStateVerified ? "true" : "false")
                  << ",\"uiProgressUpdates\":" << ui.progressUpdates
                  << ",\"uiProgressFinal\":" << ui.finalProgress
                  << ",\"bindingRegistryReleased\":"
                  << (counters.bindingRegistryReleased ? "true" : "false")
                  << ",\"ledgerBalanced\":" << (ledgerBalanced ? "true" : "false")
                  << ",\"pixelCaptureAttempted\":" << (counters.pixelCaptureAttempted ? "true" : "false")
                  << ",\"pixelCaptureOk\":" << (counters.pixelCaptureOk ? "true" : "false")
                  << ",\"pixelCaptureWidth\":" << counters.pixelCaptureWidth
                  << ",\"pixelCaptureHeight\":" << counters.pixelCaptureHeight
                  << ",\"pixelCaptureBytes\":" << counters.pixelCaptureBytes
                  << ",\"pixelFingerprint\":\"" << counters.pixelFingerprint << "\""
                  << ",\"pixelGoldenChecked\":" << (pixelGoldenChecked ? "true" : "false")
                  << ",\"pixelGoldenMatched\":" << (pixelGoldenMatched ? "true" : "false")
                  << ",\"expectPixelFingerprint\":\"" << options.expectPixelFingerprint << "\"}\n";
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_3d\",\"evidenceSchema\":4,\"frames\":"
              << counters.frameUpdates
              << ",\"gltfCooked\":true,\"cookedStaticMesh\":true,\"cookedMaterial\":true,\"cookedPrefab\":true,"
                 "\"prefabInstantiated\":true,\"sceneExtract\":true,\"multiMesh\":"
              << (multiMesh ? "true" : "false") << ",\"materialTextureBound\":"
              << (counters.materialTextureBound ? "true" : "false") << ",\"texturesUploaded\":"
              << counters.texturesUploaded << ",\"meshesUploaded\":" << counters.meshesUploaded
              << ",\"materialsLoaded\":" << counters.materialsLoaded << ",\"prefabNodes\":" << counters.prefabNodes
              << ",\"meshAssetHandlesPublished\":" << counters.meshAssetHandlesPublished
              << ",\"materialAssetHandlesPublished\":" << counters.materialAssetHandlesPublished
              << ",\"meshBindingsRegistered\":" << counters.meshBindingsRegistered
              << ",\"materialBindingsRegistered\":" << counters.materialBindingsRegistered
              << ",\"meshBindingsReleased\":" << counters.meshBindingsReleased
              << ",\"materialBindingsReleased\":" << counters.materialBindingsReleased
              << ",\"meshRetirementsAccepted\":" << counters.meshRetirementsAccepted
              << ",\"textureRetirementsAccepted\":" << counters.textureRetirementsAccepted
              << ",\"meshRetirementRecords\":" << counters.meshRetirementRecords
              << ",\"textureRetirementRecords\":" << counters.textureRetirementRecords
              << ",\"meshRetirementReleased\":" << counters.meshRetirementReleased
              << ",\"textureRetirementReleased\":" << counters.textureRetirementReleased
              << ",\"retirementRecordsLive\":" << counters.retirementRecordsLive
              << ",\"meshAssetHandlesInvalidated\":" << counters.meshAssetHandlesInvalidated
              << ",\"materialAssetHandlesInvalidated\":" << counters.materialAssetHandlesInvalidated
              << ",\"textureAssetHandlesInvalidated\":" << counters.textureAssetHandlesInvalidated
              << ",\"meshFrameResourceResolverHits\":" << counters.meshFrameResourceResolverHits
              << ",\"materialFrameResourceResolverHits\":" << counters.materialFrameResourceResolverHits
              << ",\"assetStoreActiveCount\":" << assetStoreActiveCount
              << ",\"prefabAssetResident\":" << (prefabAssetResident ? "true" : "false")
              << ",\"prefabInstances\":" << counters.prefabInstances << ",\"meshSlotCount\":" << expectedMeshes
              << ",\"externalGltf\":" << (resources.externalGltf ? "true" : "false")
              << ",\"completePbrFixture\":" << (resources.completePbrFixture ? "true" : "false")
              << ",\"materialFactorsBound\":" << (counters.materialFactorsBound ? "true" : "false")
              << ",\"materialMrTextureBound\":" << (counters.materialMrTextureBound ? "true" : "false")
              << ",\"materialNormalTextureBound\":" << (counters.materialNormalTextureBound ? "true" : "false")
              << ",\"lightingConfigured\":" << (counters.lightingConfigured ? "true" : "false")
              << ",\"directionalLightCount\":" << counters.directionalLightCount
              << ",\"bindingRegistryReleased\":"
              << (counters.bindingRegistryReleased ? "true" : "false");
    if (resources.externalGltf || resources.completePbrFixture)
    {
        std::cout << ",\"gltfPath\":";
        writeJsonString(std::cout, resources.gltfSourcePath);
    }
    std::cout << ",\"instanceBatchesPerFrame\":" << expectedMeshes << ",\"catalogCooked\":" << counters.catalogCooked
              << ",\"stateExits\":" << counters.stateExits
              << ",\"uiRootsCreated\":" << ui.rootsCreated
              << ",\"uiRootsReleased\":" << ui.rootsReleased
              << ",\"uiPanelsCreated\":" << ui.panelsCreated
              << ",\"uiLabelsCreated\":" << ui.labelsCreated
              << ",\"uiButtonsCreated\":" << ui.buttonsCreated
              << ",\"uiCheckboxesCreated\":" << ui.checkboxesCreated
              << ",\"uiSlidersCreated\":" << ui.slidersCreated
              << ",\"uiProgressBarsCreated\":" << ui.progressBarsCreated
              << ",\"uiThemeDemoRequested\":" << (ui.themeDemoRequested ? "true" : "false")
              << ",\"uiThemeSwitches\":" << ui.themeSwitches
              << ",\"uiAutomatedThemeSteps\":" << ui.automatedThemeSteps
              << ",\"uiThemeButtonActivations\":" << ui.themeButtonActivations
              << ",\"uiCheckboxActivations\":" << ui.checkboxActivations
              << ",\"uiSliderChanges\":" << ui.sliderChanges
              << ",\"uiThemeInitialLight\":" << (ui.initialThemeLight ? "true" : "false")
              << ",\"uiThemeFinalLight\":" << (ui.finalThemeLight ? "true" : "false")
              << ",\"uiInheritedChromeVerified\":" << (ui.inheritedChromeVerified ? "true" : "false")
              << ",\"uiControlsInitialStateVerified\":"
              << (ui.controlsInitialStateVerified ? "true" : "false")
              << ",\"uiAutoRotateFinal\":" << (ui.autoRotate ? "true" : "false")
              << ",\"uiRotationSpeedFinal\":" << ui.rotationSpeed
              << ",\"uiProgressUpdates\":" << ui.progressUpdates
              << ",\"uiProgressFinal\":" << ui.finalProgress
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"engineHostDestroyed\":true,\"renderResourceLedgerBalanced\":true"
              << ",\"pixelCaptureAttempted\":" << (counters.pixelCaptureAttempted ? "true" : "false")
              << ",\"pixelCaptureOk\":" << (counters.pixelCaptureOk ? "true" : "false")
              << ",\"pixelCaptureWidth\":" << counters.pixelCaptureWidth
              << ",\"pixelCaptureHeight\":" << counters.pixelCaptureHeight
              << ",\"pixelCaptureBytes\":" << counters.pixelCaptureBytes
              << ",\"pixelFingerprint\":\"" << counters.pixelFingerprint << "\""
              << ",\"pixelGoldenChecked\":" << (pixelGoldenChecked ? "true" : "false")
              << ",\"pixelGoldenMatched\":" << (pixelGoldenMatched ? "true" : "false")
              << ",\"expectPixelFingerprint\":\"" << options.expectPixelFingerprint << "\"}\n";
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
