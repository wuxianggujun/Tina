#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
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
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

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
    bool meshBound = false;
    bool materialTextureBound = false;
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
    std::filesystem::path catalogRoot{};
    Tina::Asset::CookedAssetFile meshFile{};
    Tina::Asset::CookedAssetFile textureFile{};
    std::array<Tina::Asset::CookedAssetFile, ProductMeshCount> materialFiles{};
    std::array<Tina::Render::RenderLinearColor, ProductMeshCount> materialColors{};
    Tina::Render::GpuMeshId gpuMesh{};
    Tina::Render::GpuTextureId gpuTexture{};
    bool meshUploaded = false;
    bool textureUploaded = false;
};

[[nodiscard]] Tina::Core::AssetId::Bytes meshIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[15] = static_cast<std::byte>(0xE1U);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes textureIdBytes() noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x7EU);
    bytes[15] = static_cast<std::byte>(0xA5U);
    return bytes;
}

[[nodiscard]] Tina::Core::AssetId::Bytes materialIdBytes(u32 index) noexcept
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0xE4U);
    bytes[1] = static_cast<std::byte>(index + 1U);
    bytes[15] = static_cast<std::byte>(0x41U ^ static_cast<Tina::Core::u8>(index));
    return bytes;
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
    const auto meshId = *Tina::Core::AssetId::fromBytes(meshIdBytes());
    const auto textureId = *Tina::Core::AssetId::fromBytes(textureIdBytes());
    const auto meshHex = meshId.canonicalText();
    const auto textureHex = textureId.canonicalText();
    constexpr std::array<std::array<float, 4>, ProductMeshCount> MaterialRgba{{
        {0.95F, 0.24F, 0.30F, 1.0F},
        {0.12F, 0.72F, 0.92F, 1.0F},
        {0.20F, 0.84F, 0.48F, 1.0F},
    }};

    std::string recipe;
    recipe += "platform WindowsX64\n";
    // 2x2 checker: white / magenta for visible UV modulation (M11-E5).
    recipe += "texture2d ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 2 2 FFFFFFFF FF00FFFF 00FFFFFF FFFFFFFF\n";
    recipe += "staticmesh ";
    recipe.append(meshHex.data(), meshHex.size());
    recipe += " cube\n";
    for (u32 index = 0; index < ProductMeshCount; ++index)
    {
        const auto materialId = *Tina::Core::AssetId::fromBytes(materialIdBytes(index));
        const auto materialHex = materialId.canonicalText();
        recipe += "material ";
        recipe.append(materialHex.data(), materialHex.size());
        recipe += " unlit ";
        recipe += std::to_string(MaterialRgba[index][0]);
        recipe += ' ';
        recipe += std::to_string(MaterialRgba[index][1]);
        recipe += ' ';
        recipe += std::to_string(MaterialRgba[index][2]);
        recipe += ' ';
        recipe += std::to_string(MaterialRgba[index][3]);
        recipe += ' ';
        recipe.append(textureHex.data(), textureHex.size());
        recipe += '\n';
    }

    auto request = Tina::Asset::parseCatalogCookRecipe(recipe, ".");
    if (!request)
    {
        return Tina::Core::failure(std::move(request.error()));
    }

    resources.catalogRoot = std::filesystem::temp_directory_path() / "tina_sample_3d_catalog";
    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);
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

    auto asset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, meshId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!asset)
    {
        return Tina::Core::failure(std::move(asset.error()));
    }
    resources.meshFile = std::move(*asset);

    auto view = Tina::Asset::parseStaticMeshFromCooked(resources.meshFile);
    if (!view)
    {
        return Tina::Core::failure(std::move(view.error()));
    }
    if (view->vertexCount == 0 || view->indexCount == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "cooked StaticMesh is empty");
    }

    auto textureAsset = Tina::Asset::loadCookedAssetFromCatalog(
        toUtf8(resources.catalogRoot), *catalog, textureId,
        Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
    if (!textureAsset)
    {
        return Tina::Core::failure(std::move(textureAsset.error()));
    }
    resources.textureFile = std::move(*textureAsset);

    for (u32 index = 0; index < ProductMeshCount; ++index)
    {
        const auto materialId = *Tina::Core::AssetId::fromBytes(materialIdBytes(index));
        auto materialAsset = Tina::Asset::loadCookedAssetFromCatalog(
            toUtf8(resources.catalogRoot), *catalog, materialId,
            Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory});
        if (!materialAsset)
        {
            return Tina::Core::failure(std::move(materialAsset.error()));
        }
        resources.materialFiles[index] = std::move(*materialAsset);
        auto material = Tina::Asset::parseMaterialFromCooked(resources.materialFiles[index]);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }
        if (!material->hasBaseColorTexture)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "product material must declare baseColor texture flag");
        }
        resources.materialColors[index] = Tina::Render::RenderLinearColor{
            .red = material->baseColorR,
            .green = material->baseColorG,
            .blue = material->baseColorB,
            .alpha = material->baseColorA,
        };
        ++counters.materialsLoaded;
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

        auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, resources_->textureFile);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        if (auto status = device->setMesh3DMaterialTextureBinding(ProductMaterialKey, *texture); !status)
        {
            (void)device->destroyTexture2D(*texture);
            return status;
        }
        resources_->gpuTexture = *texture;
        resources_->textureUploaded = true;
        ++counters_->texturesUploaded;
        counters_->materialTextureBound = true;

        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{16});
        if (!worldResult)
        {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        Tina::Scene::LocalTransform cameraLocal{};
        cameraLocal.position = {0.0F, 0.35F, 8.0F};
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

        constexpr std::array<float, ProductMeshCount> PositionsX{-2.3F, 0.0F, 2.3F};
        constexpr std::array<float, ProductMeshCount> PositionsZ{-0.4F, -1.0F, -1.6F};
        constexpr std::array<float, ProductMeshCount> Scales{0.9F, 1.15F, 0.8F};
        for (u32 index = 0; index < ProductMeshCount; ++index)
        {
            Tina::Scene::LocalTransform cubeLocal{};
            cubeLocal.position = {PositionsX[index], 0.0F, PositionsZ[index]};
            cubeLocal.scale = {Scales[index], Scales[index], Scales[index]};
            auto cubeEntity = world_->createEntity(cubeLocal);
            if (!cubeEntity)
            {
                return Tina::Core::failure(std::move(cubeEntity.error()));
            }
            cubeEntities_[index] = *cubeEntity;
            if (auto status = world_->setMeshRenderer3D(
                    cubeEntities_[index],
                    Tina::Scene::MeshRenderer3D{
                        .fixtureMeshKey = ProductMeshKey,
                        .fixtureMaterialKey = ProductMaterialKey,
                        .submeshIndex = 0,
                        .localBounds = {.radius = 1.75F},
                        // M11-E4: baseColor from cooked Unlit Material assets.
                        .baseColorFactor = resources_->materialColors[index],
                        .visible = true,
                    });
                !status)
            {
                return status;
            }
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
        if (auto* device = capture_->get(); device != nullptr)
        {
            if (resources_->textureUploaded)
            {
                (void)device->setMesh3DMaterialTextureBinding(ProductMaterialKey, {});
                (void)device->destroyTexture2D(resources_->gpuTexture);
                resources_->textureUploaded = false;
                resources_->gpuTexture = {};
            }
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

        constexpr std::array<float, ProductMeshCount> PositionsX{-2.3F, 0.0F, 2.3F};
        constexpr std::array<float, ProductMeshCount> PositionsZ{-0.4F, -1.0F, -1.6F};
        constexpr std::array<float, ProductMeshCount> Scales{0.9F, 1.15F, 0.8F};

        const float halfAngle = static_cast<float>(context.frameTiming().frameIndex) * 0.0125F;
        for (u32 index = 0; index < ProductMeshCount; ++index)
        {
            const float phase = halfAngle + static_cast<float>(index) * 0.45F;
            Tina::Scene::LocalTransform cubeLocal{};
            cubeLocal.position = {PositionsX[index], 0.0F, PositionsZ[index]};
            cubeLocal.rotation = {0.0F, std::sin(phase), 0.0F, std::cos(phase)};
            cubeLocal.scale = {Scales[index], Scales[index], Scales[index]};
            if (auto status = world_->setLocalTransform(cubeEntities_[index], cubeLocal); !status)
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
    std::array<Tina::Scene::EntityId, ProductMeshCount> cubeEntities_{};
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
    config.primaryWindow.title = "Tina vNext - Cooked StaticMesh Cube";
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
    std::filesystem::remove_all(resources.catalogRoot, ec);

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
        counters.materialsLoaded != ProductMeshCount || counters.texturesUploaded != 1 || !counters.meshBound ||
        !counters.materialTextureBound || counters.catalogCooked != 1 || !ledgerBalanced)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d\","
                     "\"message\":\"lifecycle counters did not match\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"meshesUploaded\":" << counters.meshesUploaded
                  << ",\"materialsLoaded\":" << counters.materialsLoaded
                  << ",\"texturesUploaded\":" << counters.texturesUploaded
                  << ",\"meshBound\":" << (counters.meshBound ? "true" : "false")
                  << ",\"catalogCooked\":" << counters.catalogCooked
                  << ",\"ledgerBalanced\":" << (ledgerBalanced ? "true" : "false") << "}\n";
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_3d\",\"frames\":" << counters.frameUpdates
              << ",\"cookedStaticMesh\":true,\"cookedMaterial\":true,\"texturedUnlit\":true,\"sceneExtract\":true,"
                 "\"meshesUploaded\":"
              << counters.meshesUploaded << ",\"materialsLoaded\":" << counters.materialsLoaded
              << ",\"texturesUploaded\":" << counters.texturesUploaded << ",\"meshKey\":" << ProductMeshKey
              << ",\"materialKey\":" << ProductMaterialKey << ",\"productCubesPerFrame\":" << ProductMeshCount
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
