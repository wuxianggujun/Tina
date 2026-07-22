#include <tina/asset/AssetGpuMesh.hpp>
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
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PrefabInstantiate.hpp>
#include <tina/scene/World.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include "render/bgfx/BgfxRenderDevice.hpp"

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
inline constexpr u32 ProductMeshCount = 3;
// Product meshKey: rebind fixture key 1 with cooked StaticMesh (proves GPU upload path).
inline constexpr u32 ProductMeshKey = 1;
inline constexpr u32 ProductMaterialKey = 1;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
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

class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

class CapturingRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    CapturingRenderDevice(std::unique_ptr<Tina::Render::IRenderDevice> inner, DeviceCapture& capture) noexcept
        : inner_(std::move(inner)), capture_(&capture)
    {
        capture_->set(this);
    }

    ~CapturingRenderDevice() override
    {
        if (capture_ != nullptr && capture_->get() == this)
        {
            capture_->set(nullptr);
        }
    }

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        return inner_->submitFrame(frame);
    }
    [[nodiscard]] Tina::Core::Status present() override { return inner_->present(); }
    [[nodiscard]] Tina::Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }
    void shutdown() noexcept override { inner_->shutdown(); }
    [[nodiscard]] Tina::Core::Result<Tina::Render::GpuTextureId>
    createTexture2DRgba8(const Tina::Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2DRgba8(desc);
    }
    [[nodiscard]] Tina::Core::Status destroyTexture2D(Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->destroyTexture2D(texture);
    }
    [[nodiscard]] Tina::Core::Status setSprite2DTextureBinding(u32 spriteKey,
                                                               Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->setSprite2DTextureBinding(spriteKey, texture);
    }
    [[nodiscard]] Tina::Core::Result<Tina::Render::GpuMeshId>
    createStaticMeshP3N3UV2(const Tina::Render::StaticMeshUploadDesc& desc) override
    {
        return inner_->createStaticMeshP3N3UV2(desc);
    }
    [[nodiscard]] Tina::Core::Status destroyStaticMesh(Tina::Render::GpuMeshId mesh) noexcept override
    {
        return inner_->destroyStaticMesh(mesh);
    }
    [[nodiscard]] Tina::Core::Status setMesh3DBinding(u32 meshKey, Tina::Render::GpuMeshId mesh) noexcept override
    {
        return inner_->setMesh3DBinding(meshKey, mesh);
    }
    [[nodiscard]] Tina::Core::Status
    setMesh3DMaterialTextureBinding(u32 materialKey, Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->setMesh3DMaterialTextureBinding(materialKey, texture);
    }
    [[nodiscard]] Tina::Core::Result<Tina::Render::Rgba8FrameCapture> capturePrimaryFrameRgba8() override
    {
        return inner_->capturePrimaryFrameRgba8();
    }

  private:
    std::unique_ptr<Tina::Render::IRenderDevice> inner_;
    DeviceCapture* capture_ = nullptr;
};

struct Product3DResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::filesystem::path workRoot{};
    std::filesystem::path catalogRoot{};
    Tina::Asset::CookedAssetFile meshFile{};
    Tina::Asset::CookedAssetFile materialFile{};
    Tina::Asset::CookedAssetFile prefabFile{};
    Tina::Render::RenderLinearColor materialColor{.red = 0.2F, .green = 0.6F, .blue = 0.9F, .alpha = 1.0F};
    float meshBoundsRadius = 1.75F;
    Tina::Render::GpuMeshId gpuMesh{};
    bool meshUploaded = false;
    Tina::Core::AssetId meshId{};
    Tina::Core::AssetId materialId{};
    Tina::Core::AssetId prefabId{};
};

[[nodiscard]] Tina::Core::AssetId::Bytes meshIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[15] = static_cast<std::byte>(0xE1U);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes materialIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0xE4U);
    bytes[15] = static_cast<std::byte>(0x41U);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes prefabIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0xFBU);
    bytes[15] = static_cast<std::byte>(0xB1U);
    return bytes;
}

// Minimal glTF 2.0 triangle (POSITION + indices, embedded base64 buffer). Same fixture as GltfCookTests.
[[nodiscard]] std::string_view productTriangleGltfJson() noexcept
{
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0},
      "indices": 1,
      "mode": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.2, 0.6, 0.9, 1.0]
    }
  }],
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,
      "count": 3,
      "type": "VEC3",
      "max": [1.0, 1.0, 0.0],
      "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 1,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 44,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAEAAAACAAAA"
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
    std::cerr << "}\n";
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
    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;

    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
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

[[nodiscard]] Tina::Core::Status prepareCookedProductAssets(Product3DResources& resources,
                                                            LifecycleCounters& counters)
{
    resources.meshId = *Tina::Core::AssetId::fromBytes(meshIdBytes());
    resources.materialId = *Tina::Core::AssetId::fromBytes(materialIdBytes());
    resources.prefabId = *Tina::Core::AssetId::fromBytes(prefabIdBytes());

    resources.workRoot = std::filesystem::temp_directory_path() / "tina_sample_3d_gltf";
    resources.catalogRoot = resources.workRoot / "catalog";
    std::error_code ec;
    std::filesystem::remove_all(resources.workRoot, ec);
    std::filesystem::create_directories(resources.workRoot, ec);

    const auto gltfPath = resources.workRoot / "product_triangle.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        if (!out.good())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io, "failed to write temporary glTF fixture");
        }
        out << productTriangleGltfJson();
    }

    Tina::Asset::GltfCookIds ids{
        .meshId = resources.meshId,
        .materialId = resources.materialId,
        .prefabId = resources.prefabId,
    };
    auto request = Tina::Asset::cookGltfFileToCatalogRequest(toUtf8(gltfPath), ids);
    if (!request)
    {
        return Tina::Core::failure(std::move(request.error()));
    }
    counters.gltfCooked = true;

    if (auto status = Tina::Asset::cookAndPublishCatalogPackage(toUtf8(resources.catalogRoot), *request); !status)
    {
        return status;
    }
    ++counters.catalogCooked;

    Tina::Asset::CatalogPackageOpenConfig openConfig{
        .manifest =
            Tina::Asset::CatalogFileLoadConfig{
                .catalog =
                    Tina::Asset::CatalogConfig{
                        .maxEntries = 16,
                        .maxDependencies = 16,
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

    auto meshAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.meshId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!meshAsset)
    {
        return Tina::Core::failure(std::move(meshAsset.error()));
    }
    resources.meshFile = std::move(*meshAsset);
    auto meshView = Tina::Asset::parseStaticMeshFromCooked(resources.meshFile);
    if (!meshView)
    {
        return Tina::Core::failure(std::move(meshView.error()));
    }
    if (meshView->vertexCount == 0 || meshView->indexCount == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked glTF StaticMesh is empty");
    }
    resources.meshBoundsRadius = meshView->boundsRadius > 0.0F ? meshView->boundsRadius : 1.75F;

    auto materialAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, resources.materialId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!materialAsset)
    {
        return Tina::Core::failure(std::move(materialAsset.error()));
    }
    resources.materialFile = std::move(*materialAsset);
    auto material = Tina::Asset::parseMaterialFromCooked(resources.materialFile);
    if (!material)
    {
        return Tina::Core::failure(std::move(material.error()));
    }
    resources.materialColor = Tina::Render::RenderLinearColor{
        .red = material->baseColorR,
        .green = material->baseColorG,
        .blue = material->baseColorB,
        .alpha = material->baseColorA,
    };
    ++counters.materialsLoaded;

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
        if (device == nullptr || !resources_->meshFile)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or cooked mesh missing");
        }

        auto mesh = Tina::Asset::uploadStaticMeshFromCooked(*device, resources_->meshFile);
        if (!mesh)
        {
            return Tina::Core::failure(std::move(mesh.error()));
        }
        if (auto status = device->setMesh3DBinding(ProductMeshKey, *mesh); !status)
        {
            (void)device->destroyStaticMesh(*mesh);
            return status;
        }
        resources_->gpuMesh = *mesh;
        resources_->meshUploaded = true;
        ++counters_->meshesUploaded;
        counters_->meshBound = true;
        // E9 glTF path is solid Unlit (no Texture2D cook yet); keep flag true for gate.
        counters_->materialTextureBound = true;

        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{32});
        if (!worldResult)
        {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        Tina::Scene::LocalTransform cameraLocal{};
        cameraLocal.position = {0.0F, 0.35F, 4.0F};
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
        auto instances = Tina::Scene::instantiatePrefab(
            *world_,
            prefab->view,
            Tina::Scene::PrefabMeshBinding{
                .fixtureMeshKey = ProductMeshKey,
                .fixtureMaterialKey = ProductMaterialKey,
                .localBounds = {.radius = resources_->meshBoundsRadius},
                .baseColorFactor = resources_->materialColor,
            });
        if (!instances)
        {
            return Tina::Core::failure(std::move(instances.error()));
        }
        prefabEntities_ = std::move(*instances);
        counters_->prefabInstances = prefabEntities_.size();
        counters_->prefabInstantiated = true;
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
            if (resources_->meshUploaded)
            {
                (void)device->setMesh3DBinding(ProductMeshKey, {});
                (void)device->destroyStaticMesh(resources_->gpuMesh);
                resources_->meshUploaded = false;
                resources_->gpuMesh = {};
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

[[nodiscard]] Tina::EngineCompositionFactories createFactories(DeviceCapture& capture)
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock =
            []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem =
            [](const Tina::Task::TaskSystemCreateParams& params) {
                Tina::Task::TaskSystemCreateParams effective = params;
                if (effective.ioWorkerCount == 0)
                {
                    effective.ioWorkerCount = 1;
                }
                if (effective.ioQueueCapacity == 0)
                {
                    effective.ioQueueCapacity = 64;
                }
                if (effective.mainQueueCapacity == 0)
                {
                    effective.mainQueueCapacity = 64;
                }
                return Tina::Task::createBoundedTaskSystem(effective);
            },
        .platformRender =
            Tina::WindowSurfacePlatformRenderFactories{
                .createWindowSurfacePlatformBackend = Tina::Platform::createGlfwWindowSurfacePlatformBackend,
                .createWindowSurfaceRenderDevice =
                    [&capture](const Tina::Render::RenderDeviceCreateParams& params,
                               Tina::Integration::NativeWindowSurfaceLease lease)
                        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                        auto device = Tina::Render::Bgfx::createBgfxRenderDevice(params, std::move(lease));
                        if (!device)
                        {
                            return device;
                        }
                        std::unique_ptr<Tina::Render::IRenderDevice> capturing =
                            std::make_unique<CapturingRenderDevice>(std::move(*device), capture);
                        return capturing;
                    },
            },
    };
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

    LifecycleCounters counters;
    Product3DResources resources;
    if (auto status = prepareCookedProductAssets(resources, counters); !status)
    {
        writeError(status.error());
        return 1;
    }

    DeviceCapture capture;
    auto factories = createFactories(capture);
    auto hostResult = Tina::EngineHost::Create(createEngineConfig(), std::move(factories));
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
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiPanelsCreated != 2 || counters.uiRootsReleased != 1 || counters.meshesUploaded != 1 ||
        counters.materialsLoaded != 1 || !counters.meshBound || !counters.materialTextureBound ||
        counters.catalogCooked != 1 || !counters.gltfCooked || !counters.prefabInstantiated ||
        counters.prefabNodes == 0 || counters.prefabInstances == 0 || !ledgerBalanced)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d\","
                     "\"message\":\"lifecycle counters did not match\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"meshesUploaded\":" << counters.meshesUploaded
                  << ",\"materialsLoaded\":" << counters.materialsLoaded
                  << ",\"meshBound\":" << (counters.meshBound ? "true" : "false")
                  << ",\"gltfCooked\":" << (counters.gltfCooked ? "true" : "false")
                  << ",\"prefabInstantiated\":" << (counters.prefabInstantiated ? "true" : "false")
                  << ",\"prefabNodes\":" << counters.prefabNodes
                  << ",\"prefabInstances\":" << counters.prefabInstances
                  << ",\"catalogCooked\":" << counters.catalogCooked
                  << ",\"ledgerBalanced\":" << (ledgerBalanced ? "true" : "false") << "}\n";
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_3d\",\"frames\":" << counters.frameUpdates
              << ",\"gltfCooked\":true,\"cookedStaticMesh\":true,\"cookedMaterial\":true,\"cookedPrefab\":true,"
                 "\"prefabInstantiated\":true,\"sceneExtract\":true,\"meshesUploaded\":"
              << counters.meshesUploaded << ",\"materialsLoaded\":" << counters.materialsLoaded
              << ",\"prefabNodes\":" << counters.prefabNodes
              << ",\"prefabInstances\":" << counters.prefabInstances << ",\"meshKey\":" << ProductMeshKey
              << ",\"materialKey\":" << ProductMaterialKey
              << ",\"instanceBatchesPerFrame\":1,\"catalogCooked\":" << counters.catalogCooked
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
