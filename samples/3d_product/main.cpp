#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Error.hpp>
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
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include "DeviceCapture.hpp"

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
// Product meshKeys start at 1; key 1 is no longer "only cube fixture".
// Cap matches sample renderSceneCapacities.mesh3DBatchCapacity.
inline constexpr u32 MaxProductMeshSlots = 8;
inline constexpr u32 FirstProductMeshKey = 1;
inline constexpr u32 FirstProductMaterialKey = 1;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    // Empty → in-memory two-mesh fixture. Non-empty → cook external .gltf/.glb path.
    std::string gltfPath{};
    bool help = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 uiRootsCreated = 0;
    u64 uiPanelsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 meshesUploaded = 0;
    u64 materialsLoaded = 0;
    u64 texturesUploaded = 0;
    u64 catalogCooked = 0;
    u64 prefabNodes = 0;
    u64 prefabInstances = 0;
    bool meshBound = false;
    bool materialTextureBound = false;
    bool gltfCooked = false;
    bool prefabInstantiated = false;
};

using DeviceCapture = Tina::Sample3D::DeviceCapture;

struct ProductMeshSlot final {
    Tina::Asset::CookedAssetFile meshFile{};
    Tina::Asset::CookedAssetFile materialFile{};
    Tina::Asset::CookedAssetFile textureFile{};
    Tina::Asset::CookedAssetFile metallicRoughnessTextureFile{};
    Tina::Asset::CookedAssetFile normalTextureFile{};
    Tina::Render::RenderLinearColor materialColor{.red = 0.2F, .green = 0.6F, .blue = 0.9F, .alpha = 1.0F};
    float metallicFactor = 0.0F;
    float roughnessFactor = 1.0F;
    float meshBoundsRadius = 1.75F;
    Tina::Render::GpuMeshId gpuMesh{};
    Tina::Render::GpuTextureId gpuTexture{};
    Tina::Render::GpuTextureId gpuMetallicRoughnessTexture{};
    Tina::Render::GpuTextureId gpuNormalTexture{};
    bool meshUploaded = false;
    bool textureUploaded = false;
    bool metallicRoughnessTextureUploaded = false;
    bool normalTextureUploaded = false;
    Tina::Core::AssetId meshId{};
    Tina::Core::AssetId materialId{};
    Tina::Core::AssetId textureId{};
    Tina::Core::AssetId metallicRoughnessTextureId{};
    Tina::Core::AssetId normalTextureId{};
    u32 meshKey = 0;
    u32 materialKey = 0;
};

struct Product3DResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::filesystem::path workRoot{};
    std::filesystem::path catalogRoot{};
    Tina::Asset::CookedAssetFile prefabFile{};
    Tina::Core::AssetId prefabId{};
    std::array<ProductMeshSlot, MaxProductMeshSlots> meshes{};
    u32 meshSlotCount = 0;
    bool externalGltf = false;
    std::string gltfSourcePath{};
};

[[nodiscard]] Tina::Core::AssetId::Bytes prefabIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0x00U);
    bytes[15] = static_cast<std::byte>(0xF0U);
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

// Two meshes + shared relative-file baseColorTexture (ASSET-001). TEXCOORD optional.
[[nodiscard]] std::string_view productTwoMeshGltfJson() noexcept
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

[[nodiscard]] Tina::UI::UILayoutStyle absolutePanelStyle(Tina::UI::UILayoutLength left, Tina::UI::UILayoutLength top,
                                                         Tina::UI::UILayoutLength width,
                                                         Tina::UI::UILayoutLength height) noexcept
{
    Tina::UI::UILayoutStyle style{};
    style.position = Tina::UI::UILayoutPositionMode::AbsoluteOverlay;
    style.absoluteInset.left = left;
    style.absoluteInset.top = top;
    style.size.width = width;
    style.size.height = height;
    return style;
}

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(Tina::Core::u8 red, Tina::Core::u8 green, Tina::Core::u8 blue,
                                             Tina::Core::u8 alpha) noexcept
{
    return Tina::UI::UIBoxPaint{
        .solidFill =
            Tina::UI::UISolidFill{
                .color =
                    {
                        .red = red,
                        .green = green,
                        .blue = blue,
                        .alpha = alpha,
                    },
            },
    };
}

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
    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasGltf = false;

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
    return options;
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    return path.string();
}

// Stable product AssetIds for slot i: mesh at 0x10+2i, material at 0x11+2i (mesh < mat).
// Prefab at 0xF0. Order keeps Prefab (mesh,mat)×N deps strictly AssetId-sorted.
// Built-in fixture slots 0/1 match historical 0x10/0x11 and 0x12/0x13 (not the old 0x10/0x20/0x30/0x40
// layout); only ordering constraints matter for catalog validation.
[[nodiscard]] Tina::Core::AssetId productMeshIdForSlot(u32 slot) noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0x00U);
    bytes[15] = static_cast<std::byte>(0x10U + slot * 2U);
    return *Tina::Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Tina::Core::AssetId productMaterialIdForSlot(u32 slot) noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[14] = static_cast<std::byte>(0x00U);
    bytes[15] = static_cast<std::byte>(0x11U + slot * 2U);
    return *Tina::Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Tina::Core::Status writeBuiltInGltfFixture(const std::filesystem::path& workRoot,
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
        out << productTwoMeshGltfJson();
    }
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status prepareCookedProductAssets(Product3DResources& resources,
                                                            LifecycleCounters& counters,
                                                            const SampleOptions& options)
{
    resources.workRoot = std::filesystem::temp_directory_path() / "tina_sample_3d_gltf";
    resources.catalogRoot = resources.workRoot / "catalog";
    resources.externalGltf = !options.gltfPath.empty();
    resources.gltfSourcePath = options.gltfPath;
    std::error_code ec;
    std::filesystem::remove_all(resources.workRoot, ec);
    std::filesystem::create_directories(resources.workRoot, ec);

    std::filesystem::path gltfPath;
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
    else
    {
        if (auto status = writeBuiltInGltfFixture(resources.workRoot, gltfPath); !status)
        {
            return status;
        }
    }

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
    }

    if (auto status = Tina::Asset::cookAndPublishCatalogPackage(toUtf8(resources.catalogRoot), *request); !status)
    {
        Tina::Core::Error error = std::move(status.error());
        error.addContext("cookAndPublishCatalogPackage", toUtf8(resources.catalogRoot));
        return Tina::Core::failure(std::move(error));
    }
    ++counters.catalogCooked;

    const u32 maxCatalogEntries = 8U + slotCount * 4U;
    Tina::Asset::CatalogPackageOpenConfig openConfig{
        .manifest =
            Tina::Asset::CatalogFileLoadConfig{
                .catalog =
                    Tina::Asset::CatalogConfig{
                        .maxEntries = maxCatalogEntries,
                        .maxDependencies = maxCatalogEntries,
                        .maxDependenciesPerAsset = 4,
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
        productMesh.meshKey = FirstProductMeshKey + slot;
        productMesh.materialKey = FirstProductMaterialKey + slot;

        auto meshAsset = Tina::Asset::loadCookedAssetFromCatalog(
            toUtf8(resources.catalogRoot), *catalog, productMesh.meshId,
            Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
        if (!meshAsset)
        {
            return Tina::Core::failure(std::move(meshAsset.error()));
        }
        productMesh.meshFile = std::move(*meshAsset);
        auto meshView = Tina::Asset::parseStaticMeshFromCooked(productMesh.meshFile);
        if (!meshView)
        {
            return Tina::Core::failure(std::move(meshView.error()));
        }
        if (meshView->vertexCount == 0 || meshView->indexCount == 0)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked glTF StaticMesh is empty");
        }
        productMesh.meshBoundsRadius = meshView->boundsRadius > 0.0F ? meshView->boundsRadius : 1.75F;

        auto materialAsset = Tina::Asset::loadCookedAssetFromCatalog(
            toUtf8(resources.catalogRoot), *catalog, productMesh.materialId,
            Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
        if (!materialAsset)
        {
            return Tina::Core::failure(std::move(materialAsset.error()));
        }
        productMesh.materialFile = std::move(*materialAsset);
        auto material = Tina::Asset::parseMaterialFromCooked(productMesh.materialFile);
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
                                        Tina::Asset::CookedAssetFile& outFile,
                                        const char* missingLabel) -> Tina::Core::Status {
            if (!present)
            {
                return Tina::Core::success();
            }
            if (depIndex >= productMesh.materialFile.header().dependencyCount)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, missingLabel);
            }
            auto textureDep = productMesh.materialFile.dependency(depIndex++);
            if (!textureDep || textureDep->expectedKind != Tina::AssetFormat::AssetKind::Texture2D)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "product material texture dependency is not Texture2D");
            }
            outId = textureDep->assetId;
            auto textureAsset = Tina::Asset::loadCookedAssetFromCatalog(
                toUtf8(resources.catalogRoot), *catalog, outId,
                Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
            if (!textureAsset)
            {
                return Tina::Core::failure(std::move(textureAsset.error()));
            }
            outFile = std::move(*textureAsset);
            return Tina::Core::success();
        };
        if (auto status = loadTextureDep(material->hasBaseColorTexture, productMesh.textureId,
                                         productMesh.textureFile, "missing baseColor texture dep");
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
                                         productMesh.metallicRoughnessTextureFile,
                                         "missing metallicRoughness texture dep");
            !status)
        {
            return status;
        }
        if (auto status = loadTextureDep(material->hasNormalTexture, productMesh.normalTextureId,
                                         productMesh.normalTextureFile, "missing normal texture dep");
            !status)
        {
            return status;
        }
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
    resources.prefabFile = std::move(*prefabAsset);
    auto prefab = Tina::Asset::parsePrefabFromCooked(resources.prefabFile);
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
    return Tina::Core::success();
}

class Product3DState final : public Tina::IGameState {
  public:
    Product3DState(SampleOptions options, LifecycleCounters& counters, Product3DResources& resources,
                   DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        auto* device = capture_->get();
        if (device == nullptr || resources_->meshSlotCount == 0)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or product mesh slots missing");
        }

        for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
        {
            ProductMeshSlot& productMesh = resources_->meshes[slot];
            if (!productMesh.meshFile)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked StaticMesh missing for product slot");
            }
            auto mesh = Tina::Asset::uploadStaticMeshFromCooked(*device, productMesh.meshFile);
            if (!mesh)
            {
                return Tina::Core::failure(std::move(mesh.error()));
            }
            if (auto status = device->setMesh3DBinding(productMesh.meshKey, *mesh); !status)
            {
                (void)device->destroyStaticMesh(*mesh);
                return status;
            }
            productMesh.gpuMesh = *mesh;
            productMesh.meshUploaded = true;
            ++counters_->meshesUploaded;

            if (auto status = device->setMesh3DMaterialFactors(productMesh.materialKey, productMesh.metallicFactor,
                                                               productMesh.roughnessFactor);
                !status)
            {
                return status;
            }
            if (productMesh.textureFile)
            {
                auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, productMesh.textureFile);
                if (!texture)
                {
                    return Tina::Core::failure(std::move(texture.error()));
                }
                if (auto status = device->setMesh3DMaterialTextureBinding(productMesh.materialKey, *texture); !status)
                {
                    (void)device->destroyTexture2D(*texture);
                    return status;
                }
                productMesh.gpuTexture = *texture;
                productMesh.textureUploaded = true;
                ++counters_->texturesUploaded;
            }
            if (productMesh.metallicRoughnessTextureFile)
            {
                auto texture =
                    Tina::Asset::uploadTexture2DFromCooked(*device, productMesh.metallicRoughnessTextureFile);
                if (!texture)
                {
                    return Tina::Core::failure(std::move(texture.error()));
                }
                if (auto status = device->setMesh3DMaterialMetallicRoughnessTextureBinding(productMesh.materialKey,
                                                                                           *texture);
                    !status)
                {
                    (void)device->destroyTexture2D(*texture);
                    return status;
                }
                productMesh.gpuMetallicRoughnessTexture = *texture;
                productMesh.metallicRoughnessTextureUploaded = true;
                ++counters_->texturesUploaded;
            }
            if (productMesh.normalTextureFile)
            {
                auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, productMesh.normalTextureFile);
                if (!texture)
                {
                    return Tina::Core::failure(std::move(texture.error()));
                }
                if (auto status = device->setMesh3DMaterialNormalTextureBinding(productMesh.materialKey, *texture);
                    !status)
                {
                    (void)device->destroyTexture2D(*texture);
                    return status;
                }
                productMesh.gpuNormalTexture = *texture;
                productMesh.normalTextureUploaded = true;
                ++counters_->texturesUploaded;
            }
        }
        counters_->meshBound = true;
        // Built-in fixture binds one texture per mesh; external may omit textures.
        counters_->materialTextureBound =
            resources_->externalGltf ? true : (counters_->texturesUploaded == resources_->meshSlotCount);

        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{32});
        if (!worldResult)
        {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        Tina::Scene::LocalTransform cameraLocal{};
        // Pull back for built-in dual nodes (origin + x=2); scale slightly for larger external bounds.
        float maxRadius = 1.75F;
        for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
        {
            maxRadius = (std::max)(maxRadius, resources_->meshes[slot].meshBoundsRadius);
        }
        const float cameraZ = (std::max)(5.5F, maxRadius * 3.5F);
        cameraLocal.position = {1.0F, 0.45F, cameraZ};
        auto cameraEntity = world_->createEntity(cameraLocal);
        if (!cameraEntity)
        {
            return Tina::Core::failure(std::move(cameraEntity.error()));
        }
        cameraEntity_ = *cameraEntity;
        if (auto status = world_->setPerspectiveCamera3D(
                cameraEntity_,
                Tina::Scene::PerspectiveCamera3D{
                    .verticalFovDegrees = 55.0F,
                    .nearPlaneMeters = 0.1F,
                    .farPlaneMeters = 100.0F,
                    .active = true,
                });
            !status)
        {
            return status;
        }

        auto prefab = Tina::Asset::parsePrefabFromCooked(resources_->prefabFile);
        if (!prefab)
        {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        // Per-node AssetId → distinct meshKey/materialKey (3D-001 product path).
        Product3DResources* productResources = resources_;
        auto instances = Tina::Scene::instantiatePrefab(
            *world_,
            prefab->view,
            Tina::Scene::PrefabMeshBinding{
                .meshKey = FirstProductMeshKey,
                .materialKey = FirstProductMaterialKey,
                .localBounds = {.radius = resources_->meshes[0].meshBoundsRadius},
                .baseColorFactor = resources_->meshes[0].materialColor,
                .resolveMeshKey =
                    [productResources](Tina::Core::AssetId id) -> Tina::u32 {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].meshId == id)
                            {
                                return productResources->meshes[slot].meshKey;
                            }
                        }
                        return 0U;
                    },
                .resolveMaterialKey =
                    [productResources](Tina::Core::AssetId id) -> Tina::u32 {
                        for (u32 slot = 0; slot < productResources->meshSlotCount; ++slot)
                        {
                            if (productResources->meshes[slot].materialId == id)
                            {
                                return productResources->meshes[slot].materialKey;
                            }
                        }
                        return 0U;
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

        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder)
        {
            return Tina::Core::failure(std::move(rootBuilder.error()));
        }
        auto root = rootBuilder->createRoot();
        if (!root)
        {
            return Tina::Core::failure(std::move(root.error()));
        }
        auto tree = rootBuilder->treeUpdater(*root);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }

        Tina::UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = Tina::UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = Tina::UI::UILayoutLength::Percent(100.0F);
        if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status)
        {
            return status;
        }

        struct PanelSpec final {
            Tina::UI::UILayoutStyle layout{};
            Tina::UI::UIBoxPaint paint{};
        };
        const std::array panels{
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(28.0F),
                                             Tina::UI::UILayoutLength::Px(360.0F), Tina::UI::UILayoutLength::Px(52.0F)),
                .paint = solidFill(8, 25, 42, 205),
            },
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(80.0F),
                                             Tina::UI::UILayoutLength::Px(430.0F), Tina::UI::UILayoutLength::Px(8.0F)),
                .paint = solidFill(36, 211, 171, 235),
            },
        };
        for (const PanelSpec& panelSpec : panels)
        {
            auto panel = tree->createPanel(root->rootNodeId());
            if (!panel)
            {
                return Tina::Core::failure(std::move(panel.error()));
            }
            if (auto status = tree->setLayoutStyle(*panel, panelSpec.layout); !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(*panel, panelSpec.paint); !status)
            {
                return status;
            }
        }

        uiRoot_ = std::move(*root);
        ++counters_->uiRootsCreated;
        counters_->uiPanelsCreated += panels.size();
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        world_.reset();
        prefabEntities_.clear();
        if (auto* device = capture_->get(); device != nullptr)
        {
            for (u32 slot = 0; slot < resources_->meshSlotCount; ++slot)
            {
                ProductMeshSlot& productMesh = resources_->meshes[slot];
                if (productMesh.normalTextureUploaded)
                {
                    (void)device->setMesh3DMaterialNormalTextureBinding(productMesh.materialKey, {});
                    (void)device->destroyTexture2D(productMesh.gpuNormalTexture);
                    productMesh.normalTextureUploaded = false;
                    productMesh.gpuNormalTexture = {};
                }
                if (productMesh.metallicRoughnessTextureUploaded)
                {
                    (void)device->setMesh3DMaterialMetallicRoughnessTextureBinding(productMesh.materialKey, {});
                    (void)device->destroyTexture2D(productMesh.gpuMetallicRoughnessTexture);
                    productMesh.metallicRoughnessTextureUploaded = false;
                    productMesh.gpuMetallicRoughnessTexture = {};
                }
                if (productMesh.textureUploaded)
                {
                    (void)device->setMesh3DMaterialTextureBinding(productMesh.materialKey, {});
                    (void)device->destroyTexture2D(productMesh.gpuTexture);
                    productMesh.textureUploaded = false;
                    productMesh.gpuTexture = {};
                }
                if (productMesh.meshUploaded)
                {
                    (void)device->setMesh3DBinding(productMesh.meshKey, {});
                    (void)device->destroyStaticMesh(productMesh.gpuMesh);
                    productMesh.meshUploaded = false;
                    productMesh.gpuMesh = {};
                }
            }
        }
        if (uiRoot_)
        {
            uiRoot_.reset();
            ++counters_->uiRootsReleased;
        }
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (options_.frameDelayMilliseconds != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        if (counters_->frameUpdates >= options_.targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        if (!world_.has_value())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "3D product World was not initialized");
        }

        // Spin root-ish prefab entities for visible motion without hand-built cubes.
        const float halfAngle = static_cast<float>(context.frameTiming().frameIndex) * 0.0125F;
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
                Tina::Scene::ExtractRenderSceneParams{
                    .surfaceViewport =
                        Tina::Render::Camera2DSurfaceViewport{
                            .pixelWidth = 1280,
                            .pixelHeight = 720,
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
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Product3DResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
    mutable std::optional<Tina::Scene::World> world_{};
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
    config.renderSceneCapacities.mesh3DItemCapacity = 16;
    config.renderSceneCapacities.mesh3DBatchCapacity = 8;
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

    const bool ledgerBalanced =
        capture.get() == nullptr || capture.get()->statistics().liveResources == 0;
    hostResult->reset();

    std::error_code ec;
    std::filesystem::remove_all(resources.workRoot, ec);

    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }
    const u32 expectedMeshes = resources.meshSlotCount;
    const bool multiMesh = expectedMeshes >= 2U;
    const bool texturesOk =
        resources.externalGltf ? true : (counters.texturesUploaded == expectedMeshes);
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiPanelsCreated != 2 || counters.uiRootsReleased != 1 ||
        counters.meshesUploaded != expectedMeshes || counters.materialsLoaded != expectedMeshes || !texturesOk ||
        !counters.meshBound || !counters.materialTextureBound || counters.catalogCooked != 1 || !counters.gltfCooked ||
        !counters.prefabInstantiated || counters.prefabNodes == 0 || counters.prefabInstances == 0 || !ledgerBalanced)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d\","
                     "\"message\":\"lifecycle counters did not match\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"meshesUploaded\":" << counters.meshesUploaded
                  << ",\"materialsLoaded\":" << counters.materialsLoaded
                  << ",\"texturesUploaded\":" << counters.texturesUploaded
                  << ",\"meshBound\":" << (counters.meshBound ? "true" : "false")
                  << ",\"materialTextureBound\":" << (counters.materialTextureBound ? "true" : "false")
                  << ",\"gltfCooked\":" << (counters.gltfCooked ? "true" : "false")
                  << ",\"prefabInstantiated\":" << (counters.prefabInstantiated ? "true" : "false")
                  << ",\"prefabNodes\":" << counters.prefabNodes
                  << ",\"prefabInstances\":" << counters.prefabInstances
                  << ",\"catalogCooked\":" << counters.catalogCooked
                  << ",\"meshSlotCount\":" << expectedMeshes
                  << ",\"externalGltf\":" << (resources.externalGltf ? "true" : "false")
                  << ",\"ledgerBalanced\":" << (ledgerBalanced ? "true" : "false") << "}\n";
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_3d\",\"frames\":" << counters.frameUpdates
              << ",\"gltfCooked\":true,\"cookedStaticMesh\":true,\"cookedMaterial\":true,\"cookedPrefab\":true,"
                 "\"prefabInstantiated\":true,\"sceneExtract\":true,\"multiMesh\":"
              << (multiMesh ? "true" : "false") << ",\"materialTextureBound\":"
              << (counters.materialTextureBound ? "true" : "false") << ",\"texturesUploaded\":"
              << counters.texturesUploaded << ",\"meshesUploaded\":" << counters.meshesUploaded
              << ",\"materialsLoaded\":" << counters.materialsLoaded << ",\"prefabNodes\":" << counters.prefabNodes
              << ",\"prefabInstances\":" << counters.prefabInstances << ",\"meshSlotCount\":" << expectedMeshes
              << ",\"externalGltf\":" << (resources.externalGltf ? "true" : "false");
    if (resources.externalGltf)
    {
        std::cout << ",\"gltfPath\":";
        writeJsonString(std::cout, resources.gltfSourcePath);
    }
    std::cout << ",\"meshKeys\":[";
    for (u32 slot = 0; slot < expectedMeshes; ++slot)
    {
        if (slot != 0)
        {
            std::cout << ',';
        }
        std::cout << (FirstProductMeshKey + slot);
    }
    std::cout << "],\"materialKeys\":[";
    for (u32 slot = 0; slot < expectedMeshes; ++slot)
    {
        if (slot != 0)
        {
            std::cout << ',';
        }
        std::cout << (FirstProductMaterialKey + slot);
    }
    std::cout << "],\"instanceBatchesPerFrame\":" << expectedMeshes << ",\"catalogCooked\":" << counters.catalogCooked
              << ",\"stateExits\":" << counters.stateExits << ",\"uiPanelsPerFrame\":2,\"uiRootsReleased\":"
              << counters.uiRootsReleased << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"engineHostDestroyed\":true,\"renderResourceLedgerBalanced\":true}\n";
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
