#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
#include <tina/asset/TileMapPhysicsSync.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>
#endif
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/platform/Input.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/InputActionMap.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIText.hpp>
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
#include <tina/ui/UIContext.hpp>
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#endif

#include "render/bgfx/BgfxRenderDevice.hpp"
#include "TileSelection.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
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

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
// bgfx product path currently binds atlas textures per spriteKey; fixture samples
// historically only accepted key 1. Tile + character share the cooked atlas key.
inline constexpr u32 ProductSpriteKey = 1;
inline constexpr u32 ExpectedNonEmptyTiles = 11; // 8 floor + 3 wall
inline constexpr Tina::InputActionId MoveLeftAction{1};
inline constexpr Tina::InputActionId MoveRightAction{2};
inline constexpr Tina::InputActionId SelectTileAction{3};
inline constexpr float DemoWalkSpeedMetersPerSecond = 4.0f;

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
    u64 texturesUploaded = 0;
    u64 lastTileSprites = 0;
    u64 lastTotalSprites = 0;
    u64 controllerGroundedFrames = 0;
    u64 controllerWalkFrames = 0;
    u64 controllerHitRightFrames = 0;
    float maxControllerX = 0.0f;
    u64 uiRootsCreated = 0;
    u64 uiPanelsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 uiTextLabelsCreated = 0;
    u64 uiButtonsCreated = 0;
    u64 uiButtonActionsWired = 0;
    Tina::Sample2D::TileSelectionCounters tileSelection{};
    u16 lastSelectedTileId = 0;
    u64 catalogRecipeAssets = 0;
    bool catalogFromRecipeFile = false;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    u64 physicsSteps = 0;
    u64 physicsStaticBodies = 0;
    u64 physicsDynamicContacts = 0;
    float lastDynamicY = 0.0f;
    bool physicsReady = false;
#endif
};

inline constexpr u32 ExpectedUIPanelCount = 2;
inline constexpr u32 ExpectedUITextLabelCount = 2;
inline constexpr u32 ExpectedUIButtonCount = 1;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
inline constexpr u32 ExpectedPhysicsStaticBodies = ExpectedNonEmptyTiles;
inline constexpr u32 ExpectedSpritesWithPhysics = ExpectedNonEmptyTiles + 2; // tiles + character + crate
#else
inline constexpr u32 ExpectedSpritesWithPhysics = ExpectedNonEmptyTiles + 1;
#endif

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

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
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
        } else if (byte >= 0x20U)
        {
            output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d\",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argc, char** argv)
{
    SampleOptions options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument.starts_with("--frames="))
        {
            const auto text = argument.substr(std::string_view{"--frames="}.size());
            u64 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size() || value == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "invalid --frames");
            }
            options.targetFrameCount = value;
            continue;
        }
        if (argument.starts_with("--frame-delay-ms="))
        {
            const auto text = argument.substr(std::string_view{"--frame-delay-ms="}.size());
            u32 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "invalid --frame-delay-ms");
            }
            options.frameDelayMilliseconds = value;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "unsupported argument");
    }
    return options;
}

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

  private:
    std::unique_ptr<Tina::Render::IRenderDevice> inner_;
    DeviceCapture* capture_ = nullptr;
};

struct TileMapResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::unique_ptr<Tina::Asset::AssetSystem> system{};
    Tina::Asset::AssetHandle textureHandle{};
    Tina::Asset::AssetHandle tilesetHandle{};
    Tina::Asset::AssetHandle tileMapHandle{};
    Tina::Render::GpuTextureId gpuTexture{};
    std::optional<Tina::Asset::TileMapInstance> map{};
    std::optional<Tina::Asset::TileMapGridCollision> grid{};
    std::optional<Tina::Asset::CharacterController2D> controller{};
    std::pmr::vector<Tina::Asset::TileMapSolidHit> solidScratch{&memory};
    std::filesystem::path catalogRoot{};
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    std::optional<Tina::Physics2D::PhysicsWorld2D> physicsWorld{};
    Tina::Physics2D::PhysicsBodyId dynamicBody{};
    Tina::Physics2D::PhysicsBodyId staticBodies[32]{};
    Tina::Physics2D::PhysicsGridSolidCell2D solidCellScratch[32]{};
    float dynamicHalfExtent = 0.25f;
    float lastDynamicX = 3.0f;
    float lastDynamicY = 3.5f;
#endif
};

[[nodiscard]] Tina::Core::Status prepareCatalog(TileMapResources& resources, LifecycleCounters& counters)
{
    // Stable product ids must match samples/2d_tilemap_bgfx/catalog/sample_2d.recipe.
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const auto tileMapId = *Tina::Core::AssetId::fromBytes(idBytes(3U));

#if !defined(TINA_SAMPLE_2D_RECIPE_PATH)
#error "TINA_SAMPLE_2D_RECIPE_PATH must be defined for tina_sample_2d catalog recipe load"
#endif
    // M10-A38: cook from an on-disk catalog recipe file (product asset path),
    // not in-process payload assembly. Still a hermetic fixture recipe, not
    // the full external cooker CLI pipeline.
    auto request = Tina::Asset::loadCatalogCookRecipeFile(TINA_SAMPLE_2D_RECIPE_PATH);
    if (!request)
    {
        return Tina::Core::failure(std::move(request.error()));
    }
    if (request->assets.size() != 3U)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "sample_2d.recipe must declare Texture2D+Tileset+TileMap");
    }
    counters.catalogRecipeAssets = request->assets.size();
    counters.catalogFromRecipeFile = true;

    resources.catalogRoot = std::filesystem::temp_directory_path() / "tina_sample_2d_pkg";
    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);
    const auto rootUtf8 = [&] {
        const auto u8 = resources.catalogRoot.u8string();
        return std::string(u8.begin(), u8.end());
    }();
    if (auto cookStatus = Tina::Asset::cookAndPublishCatalogPackage(rootUtf8, *request); !cookStatus)
    {
        return cookStatus;
    }

    auto system = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
        .storeCapacity = 16,
        .memoryResource = &resources.memory,
        .batch =
            Tina::Asset::CookedAssetBatchLoadConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory},
                .memoryResource = &resources.memory,
            },
        .requireTyped2dPayloads = true,
    });
    if (!system)
    {
        return Tina::Core::failure(std::move(system.error()));
    }
    if (auto bindStatus = system->openAndBindCatalog(rootUtf8); !bindStatus)
    {
        return bindStatus;
    }
    auto loaded = system->load(std::array{tileMapId});
    if (!loaded)
    {
        return Tina::Core::failure(std::move(loaded.error()));
    }
    resources.tileMapHandle = (*loaded)[0];

    auto tilesetHandle = system->find(tilesetId);
    auto textureHandle = system->find(textureId);
    if (!tilesetHandle || !textureHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle, "tileset/texture not loaded");
    }
    resources.tilesetHandle = *tilesetHandle;
    resources.textureHandle = *textureHandle;

    const auto* tileMapFile = system->tryGet(resources.tileMapHandle);
    const auto* tilesetFile = system->tryGet(resources.tilesetHandle);
    if (tileMapFile == nullptr || tilesetFile == nullptr)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "tilemap/tileset CPU missing");
    }
    auto tileMapView = Tina::Asset::parseTileMapFromCooked(*tileMapFile);
    auto tilesetView = Tina::Asset::parseTilesetFromCooked(*tilesetFile);
    if (!tileMapView || !tilesetView)
    {
        return Tina::Core::failure(tileMapView ? tilesetView.error() : tileMapView.error());
    }
    auto map = Tina::Asset::TileMapInstance::Create(
        *tileMapView, *tilesetView, tileMapId, tilesetId,
        Tina::Asset::TileMapInstanceConfig{.chunkSizeCells = 4, .memoryResource = &resources.memory});
    if (!map)
    {
        return Tina::Core::failure(std::move(map.error()));
    }
    resources.map.emplace(std::move(*map));
    resources.grid.emplace(*resources.map);
    resources.controller.emplace(Tina::Asset::CharacterController2DConfig{
        .halfWidth = 0.3f,
        .halfHeight = 0.5f,
        .gravity = 40.0f,
        .maxFallSpeed = 50.0f,
        .skin = 0.01f,
    });
    resources.controller->teleport(1.0f, 3.0f, true);

#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    Tina::Physics2D::PhysicsWorld2DConfig worldConfig;
    worldConfig.bodyCapacity = 64;
    worldConfig.shapeCapacity = 64;
    worldConfig.contactBeginCapacity = 32;
    worldConfig.contactEndCapacity = 32;
    worldConfig.contactHitCapacity = 8;
    worldConfig.commandCapacity = 16;
    worldConfig.solverSubStepCount = 1;
    worldConfig.gravityMetersPerSecondSquared = {0.0F, -20.0F};
    auto worldResult = Tina::Physics2D::PhysicsWorld2D::Create(worldConfig, resources.memory);
    if (!worldResult)
    {
        return Tina::Core::failure(std::move(worldResult.error()));
    }
    resources.physicsWorld.emplace(std::move(*worldResult));

    Tina::Physics2D::PhysicsGridBodySyncConfig2D syncConfig;
    syncConfig.cellSizeMeters = 0.0F;
    syncConfig.enableContactEvents = true;
    auto synced = Tina::Asset::syncTileMapSolidsToStaticBodies(
        *resources.grid, *resources.physicsWorld, syncConfig, resources.staticBodies, resources.solidCellScratch);
    if (!synced)
    {
        return Tina::Core::failure(std::move(synced.error()));
    }
    if (synced->written != ExpectedPhysicsStaticBodies)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "unexpected static tile body count");
    }

    Tina::Physics2D::PhysicsBody2DDesc dynamicDesc;
    dynamicDesc.type = Tina::Physics2D::PhysicsBodyType2D::Dynamic;
    dynamicDesc.positionMeters = {3.0F, 3.5F};
    Tina::Physics2D::PhysicsBoxShape2DDesc box;
    box.halfExtentsMeters = {resources.dynamicHalfExtent, resources.dynamicHalfExtent};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamic = resources.physicsWorld->createBoxBody(dynamicDesc, box);
    if (!dynamic)
    {
        return Tina::Core::failure(std::move(dynamic.error()));
    }
    resources.dynamicBody = dynamic->body;
    resources.lastDynamicX = dynamicDesc.positionMeters.x;
    resources.lastDynamicY = dynamicDesc.positionMeters.y;
#endif

    resources.system = std::make_unique<Tina::Asset::AssetSystem>(std::move(*system));
    return Tina::Core::success();
}

class TileMapBgfxState final : public Tina::IGameState {
  public:
    TileMapBgfxState(SampleOptions options, LifecycleCounters& counters, TileMapResources& resources,
                     DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        auto* device = capture_->get();
        if (device == nullptr || resources_->system == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or catalog missing");
        }
        const auto* textureFile = resources_->system->tryGet(resources_->textureHandle);
        if (textureFile == nullptr)
        {
            return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "texture CPU payload missing");
        }
        auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, *textureFile);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        if (const auto status = device->setSprite2DTextureBinding(ProductSpriteKey, *texture); !status)
        {
            (void)device->destroyTexture2D(*texture);
            return status;
        }
        resources_->gpuTexture = *texture;
        ++counters_->texturesUploaded;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        if (resources_->physicsWorld)
        {
            counters_->physicsStaticBodies = ExpectedPhysicsStaticBodies;
            counters_->physicsReady = true;
        }
#endif

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
        // HUD-style overlays: top-left status bar + bottom accent strip (SolidQuad only, no FreeType).
        const std::array panels{
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(16.0F), Tina::UI::UILayoutLength::Px(12.0F),
                                             Tina::UI::UILayoutLength::Px(280.0F), Tina::UI::UILayoutLength::Px(48.0F)),
                .paint = solidFill(8, 16, 28, 220),
            },
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(16.0F), Tina::UI::UILayoutLength::Px(480.0F),
                                             Tina::UI::UILayoutLength::Px(320.0F), Tina::UI::UILayoutLength::Px(10.0F)),
                .paint = solidFill(255, 196, 64, 230),
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

        // HUD labels: English + Chinese. Without FreeType these paint as SolidQuad
        // placeholder bars; with FreeType (TINA_SAMPLE_TILEMAP_FREETYPE) Desktop-style
        // SourceHan injection yields real CJK glyphs.
        struct LabelSpec final {
            Tina::UI::UILayoutStyle layout{};
            std::string_view text{};
            Tina::UI::UITextStyle style{};
        };
        const std::array labels{
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(20.0F),
                                             Tina::UI::UILayoutLength::Px(240.0F), Tina::UI::UILayoutLength::Px(28.0F)),
                .text = "TileMap 2D",
                .style =
                    Tina::UI::UITextStyle{
                        .logicalSize = 22.0F,
                        .advanceScale = 0.65F,
                        .lineHeightScale = 1.15F,
                        .color = {.red = 120, .green = 240, .blue = 255, .alpha = 255},
                    },
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(44.0F),
                                             Tina::UI::UILayoutLength::Px(240.0F), Tina::UI::UILayoutLength::Px(28.0F)),
                .text = "中文地图",
                .style =
                    Tina::UI::UITextStyle{
                        .logicalSize = 22.0F,
                        .advanceScale = 0.65F,
                        .lineHeightScale = 1.15F,
                        .color = {.red = 255, .green = 210, .blue = 80, .alpha = 255},
                    },
            },
        };
        for (const LabelSpec& labelSpec : labels)
        {
            auto label = tree->createLabel(root->rootNodeId());
            if (!label)
            {
                return Tina::Core::failure(std::move(label.error()));
            }
            if (auto status = tree->setLayoutStyle(*label, labelSpec.layout); !status)
            {
                return status;
            }
            if (auto status = tree->setTextStyle(*label, labelSpec.style); !status)
            {
                return status;
            }
            if (auto status = tree->setText(*label, labelSpec.text); !status)
            {
                return status;
            }
        }

        // HUD Button is product UI surface for pointer/default-action path. Automated
        // smoke does not synthesize clicks; wiring + create counts are gated. Interactive
        // runs can click "Demo" (no world side-effect required for the JSON gate).
        {
            auto button = tree->createButton(root->rootNodeId());
            if (!button)
            {
                return Tina::Core::failure(std::move(button.error()));
            }
            if (auto status = tree->setLayoutStyle(
                    *button, absolutePanelStyle(Tina::UI::UILayoutLength::Px(700.0F),
                                                Tina::UI::UILayoutLength::Px(12.0F),
                                                Tina::UI::UILayoutLength::Px(120.0F),
                                                Tina::UI::UILayoutLength::Px(40.0F)));
                !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(*button, solidFill(40, 120, 80, 230)); !status)
            {
                return status;
            }
            if (auto status = tree->setButtonAction(
                    *button, Tina::UI::UIButtonActionCallback{[](const Tina::UI::UIButtonActionEvent&) noexcept {
                        // Intentionally empty: proves action slot wiring without side effects.
                    }});
                !status)
            {
                return status;
            }
            ++counters_->uiButtonsCreated;
            ++counters_->uiButtonActionsWired;
        }

        uiRoot_ = std::move(*root);
        ++counters_->uiRootsCreated;
        counters_->uiPanelsCreated += panels.size();
        counters_->uiTextLabelsCreated += labels.size();
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (uiRoot_)
        {
            uiRoot_.reset();
            ++counters_->uiRootsReleased;
        }
        if (auto* device = capture_->get(); device != nullptr && resources_->gpuTexture)
        {
            (void)device->setSprite2DTextureBinding(ProductSpriteKey, {});
            (void)device->destroyTexture2D(resources_->gpuTexture);
            resources_->gpuTexture = {};
        }
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status fixedUpdate(Tina::FixedUpdateContext& context) override
    {
        if (!resources_->map)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap selection map missing");
        }

        const Tina::Asset::TileMapInstance& map = *resources_->map;
        const u64 previousSelectionHits = counters_->tileSelection.selectionHits;
        Tina::Sample2D::consumeTileSelectionTransitions(
            context.simulationActions().transitions, SelectTileAction,
            Tina::Sample2D::TileSelectionGrid{
                .widthCells = map.widthCells(),
                .heightCells = map.heightCells(),
                .cellSizeMeters = map.cellSizeMeters(),
            },
            counters_->tileSelection);

        if (counters_->tileSelection.selectionHits != previousSelectionHits &&
            counters_->tileSelection.lastSelection.has_value())
        {
            const Tina::Sample2D::SelectedTile& selection = *counters_->tileSelection.lastSelection;
            counters_->lastSelectedTileId = map.tileIdAt(selection.cellX, selection.cellY);
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (resources_->controller && resources_->grid)
        {
            // Hermetic product demo: after first ground contact, walk right until wall.
            // Keyboard MoveLeft/MoveRight bindings remain available for interactive runs
            // and override the scripted walk when held.
            float wishX = 0.0f;
            if (context.frameActions().isHeld(MoveLeftAction))
            {
                wishX -= DemoWalkSpeedMetersPerSecond;
            }
            if (context.frameActions().isHeld(MoveRightAction))
            {
                wishX += DemoWalkSpeedMetersPerSecond;
            }
            if (wishX == 0.0f && counters_->controllerGroundedFrames > 0)
            {
                wishX = DemoWalkSpeedMetersPerSecond;
            }

            if (auto status = resources_->controller->move(
                    *resources_->grid, 1.0f / 60.0f,
                    Tina::Asset::CharacterController2DMoveInput{.wishVelocityX = wishX}, resources_->solidScratch);
                !status)
            {
                return status;
            }
            const auto& st = resources_->controller->state();
            if (st.grounded)
            {
                ++counters_->controllerGroundedFrames;
            }
            if (wishX > 0.0f)
            {
                ++counters_->controllerWalkFrames;
            }
            if (st.hitRight)
            {
                ++counters_->controllerHitRightFrames;
            }
            if (st.positionX > counters_->maxControllerX)
            {
                counters_->maxControllerX = st.positionX;
            }
        }
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        if (resources_->physicsWorld)
        {
            if (auto status = resources_->physicsWorld->step(); !status)
            {
                return status;
            }
            ++counters_->physicsSteps;
            auto contacts = resources_->physicsWorld->contactEvents();
            if (!contacts)
            {
                return Tina::Core::failure(std::move(contacts.error()));
            }
            for (const auto& begin : contacts->beginEvents)
            {
                if (begin.bodyA == resources_->dynamicBody || begin.bodyB == resources_->dynamicBody)
                {
                    ++counters_->physicsDynamicContacts;
                }
            }
            if (auto state = resources_->physicsWorld->bodyState(resources_->dynamicBody); state)
            {
                resources_->lastDynamicX = state->positionMeters.x;
                resources_->lastDynamicY = state->positionMeters.y;
                counters_->lastDynamicY = state->positionMeters.y;
            }
        }
#endif
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
        if (!resources_->map || !resources_->controller)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap state not ready");
        }

        auto& writer = context.renderSceneWriter();
        const Tina::Render::RenderCamera2DInput camera{
            .stableCameraKey = 1,
            .centerX = 4.0f,
            .centerY = 2.0f,
            .worldWidth = 10.0f,
            .worldHeight = 6.0f,
            .actualPixelsPerMeter = 64.0f,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::CameraTranslation,
        };
        if (auto status = writer.setCamera2D(camera); !status)
        {
            return status;
        }

        std::pmr::vector<Tina::Render::RenderSprite2DInput> tileSprites{&resources_->memory};
        const Tina::Asset::TileChunkCameraQuery query{
            .centerX = camera.centerX,
            .centerY = camera.centerY,
            .halfWidth = camera.worldWidth * 0.5f,
            .halfHeight = camera.worldHeight * 0.5f,
        };
        auto emitted = Tina::Asset::emitVisibleTileMapSprites(
            *resources_->map, query, Tina::Asset::TileChunkSpriteEmitParams{.spriteKey = ProductSpriteKey},
            tileSprites);
        if (!emitted)
        {
            return Tina::Core::failure(std::move(emitted.error()));
        }
        counters_->lastTileSprites = *emitted;
        for (const auto& sprite : tileSprites)
        {
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }

        const auto& st = resources_->controller->state();
        const Tina::Render::RenderSprite2DInput character{
            .spriteKey = ProductSpriteKey,
            .stableEntityKey = 900001,
            .centerX = st.positionX,
            .centerY = st.positionY,
            .widthMeters = resources_->controller->config().halfWidth * 2.0f,
            .heightMeters = resources_->controller->config().halfHeight * 2.0f,
            .u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 0.5f,
            .v1 = 1.0f,
            .sortingLayer = 1,
            .orderInLayer = 0,
            .red = 255,
            .green = 220,
            .blue = 80,
            .alpha = 255,
        };
        if (auto status = writer.addSprite2D(character); !status)
        {
            return status;
        }
        u64 totalSprites = *emitted + 1U;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        // Product crate sprite follows the dynamic Box2D body (same atlas key as tiles).
        const Tina::Render::RenderSprite2DInput crate{
            .spriteKey = ProductSpriteKey,
            .stableEntityKey = 900002,
            .centerX = resources_->lastDynamicX,
            .centerY = resources_->lastDynamicY,
            .widthMeters = resources_->dynamicHalfExtent * 2.0f,
            .heightMeters = resources_->dynamicHalfExtent * 2.0f,
            .u0 = 0.5f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 1.0f,
            .sortingLayer = 1,
            .orderInLayer = 1,
            .red = 120,
            .green = 220,
            .blue = 255,
            .alpha = 255,
        };
        if (auto status = writer.addSprite2D(crate); !status)
        {
            return status;
        }
        ++totalSprites;
#endif
        counters_->lastTotalSprites = totalSprites;
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    TileMapResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
};

class TileMapBgfxApplication final : public Tina::IGameApplication {
  public:
    TileMapBgfxApplication(SampleOptions options, LifecycleCounters& counters, TileMapResources& resources,
                           DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<TileMapBgfxState>(options_, *counters_, *resources_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override { ++counters_->applicationShutdowns; }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    TileMapResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
};

#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
[[nodiscard]] std::shared_ptr<std::vector<std::byte>> loadFontFixtureBytes(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<std::byte>>(size);
    if (size > 0)
    {
        input.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size));
    }
    if (!input)
    {
        return {};
    }
    return bytes;
}
#endif

[[nodiscard]] Tina::EngineCompositionFactories createFactories(DeviceCapture& capture)
{
    Tina::EngineCompositionFactories factories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
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

#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
#if defined(TINA_SAMPLE_TILEMAP_FONT_PATH)
    auto fontBytes = loadFontFixtureBytes(TINA_SAMPLE_TILEMAP_FONT_PATH);
#else
    std::shared_ptr<std::vector<std::byte>> fontBytes{};
#endif
    if (fontBytes && !fontBytes->empty())
    {
        factories.createPrimaryWindowUIContext =
            [fontBytes](Tina::Platform::WindowId ownerWindow, const Tina::UI::UIContextCapacityConfig& capacities,
                        std::pmr::memory_resource& resource) -> Tina::Core::Result<std::unique_ptr<Tina::UI::UIContext>> {
                auto rasterizer = Tina::UI::createFreeTypeTextRasterizer({}, resource);
                if (!rasterizer)
                {
                    return Tina::Core::failure(std::move(rasterizer.error()));
                }
                auto context =
                    Tina::UI::UIContext::Create(ownerWindow, capacities, std::move(*rasterizer), resource);
                if (!context)
                {
                    return Tina::Core::failure(std::move(context.error()));
                }
                const auto open =
                    (*context)->openTextFont(std::span<const std::byte>(fontBytes->data(), fontBytes->size()));
                if (!open)
                {
                    return Tina::Core::failure(std::move(open.error()));
                }
                return std::move(*context);
            };
    }
#endif

    return factories;
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Sample 2D";
    config.primaryWindow.title = "Tina Sample 2D — TileMap + Character + UI";
    config.primaryWindow.initialLogicalExtent = {960, 540};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 64;
    // A/D + arrows for interactive walk; automated smoke uses scripted walk after land.
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::A},
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Left},
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::D},
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Right},
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryPointerButtonBinding{
            .pointer = Tina::Platform::PrimaryPointerId,
            .button = Tina::Platform::PointerButton::Primary,
        },
        .action = SelectTileAction,
        .domain = Tina::InputActionDomain::Simulation,
    });
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    auto options = parseOptions(argc, argv);
    if (!options)
    {
        writeError(options.error());
        return 2;
    }

    LifecycleCounters counters{};
    TileMapResources resources{};
    if (const auto status = prepareCatalog(resources, counters); !status)
    {
        writeError(status.error());
        return 1;
    }

    DeviceCapture capture{};
    auto host = Tina::EngineHost::Create(createEngineConfig(), createFactories(capture));
    if (!host)
    {
        writeError(host.error());
        return 1;
    }

    TileMapBgfxApplication application{*options, counters, resources, capture};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }

    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);

    const Tina::Sample2D::SelectedTile* lastSelection =
        counters.tileSelection.lastSelection.has_value() ? &*counters.tileSelection.lastSelection : nullptr;
    const u64 classifiedPointerPresses = counters.tileSelection.missingWorldPointerSamples +
                                         counters.tileSelection.viewportMisses + counters.tileSelection.mapMisses +
                                         counters.tileSelection.selectionHits;
    const bool selectionCountersValid = counters.tileSelection.pointerPresses == classifiedPointerPresses;
    const bool selectionLatchValid =
        (counters.tileSelection.selectionHits == 0 && lastSelection == nullptr) ||
        (counters.tileSelection.selectionHits > 0 && lastSelection != nullptr && resources.map.has_value() &&
         lastSelection->cellX < resources.map->widthCells() && lastSelection->cellY < resources.map->heightCells());
    const bool selectionStateValid = selectionCountersValid && selectionLatchValid;

    bool ok = selectionStateValid && counters.catalogFromRecipeFile && counters.catalogRecipeAssets == 3 &&
              counters.texturesUploaded == 1 &&
              counters.lastTileSprites == ExpectedNonEmptyTiles &&
              counters.lastTotalSprites == ExpectedSpritesWithPhysics && counters.controllerGroundedFrames > 0 &&
              counters.controllerWalkFrames > 0 && counters.controllerHitRightFrames > 0 &&
              counters.maxControllerX > 1.5f && counters.renderExtractions == counters.frameUpdates &&
              counters.stateExits == 1 && counters.applicationShutdowns == 1 && counters.uiRootsCreated == 1 &&
              counters.uiPanelsCreated == ExpectedUIPanelCount &&
              counters.uiTextLabelsCreated == ExpectedUITextLabelCount &&
              counters.uiButtonsCreated == ExpectedUIButtonCount && counters.uiButtonActionsWired == 1 &&
              counters.uiRootsReleased == 1 && *run == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    ok = ok && counters.physicsReady && counters.physicsStaticBodies == ExpectedPhysicsStaticBodies &&
         counters.physicsSteps == counters.frameUpdates && counters.physicsDynamicContacts > 0 &&
         counters.lastDynamicY < 3.5f && counters.lastDynamicY > 0.5f;
#endif
    if (!ok)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d\","
                     "\"message\":\"verification failed\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"tileSprites\":" << counters.lastTileSprites
                  << ",\"totalSprites\":" << counters.lastTotalSprites
                  << ",\"grounded\":" << counters.controllerGroundedFrames
                  << ",\"walkFrames\":" << counters.controllerWalkFrames
                  << ",\"hitRight\":" << counters.controllerHitRightFrames
                  << ",\"maxX\":" << counters.maxControllerX << ",\"uiRoots\":" << counters.uiRootsCreated
                  << ",\"uiPanels\":" << counters.uiPanelsCreated << ",\"uiLabels\":" << counters.uiTextLabelsCreated
                  << ",\"uiButtons\":" << counters.uiButtonsCreated << ",\"uiReleased\":" << counters.uiRootsReleased
                  << ",\"worldPointerPresses\":" << counters.tileSelection.pointerPresses
                  << ",\"worldPointerMissingSamples\":" << counters.tileSelection.missingWorldPointerSamples
                  << ",\"worldPointerViewportMisses\":" << counters.tileSelection.viewportMisses
                  << ",\"worldPointerMapMisses\":" << counters.tileSelection.mapMisses
                  << ",\"tileSelectionHits\":" << counters.tileSelection.selectionHits
                  << ",\"hasTileSelection\":" << (lastSelection != nullptr ? "true" : "false")
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
                  << ",\"physicsSteps\":" << counters.physicsSteps
                  << ",\"physicsStatics\":" << counters.physicsStaticBodies
                  << ",\"physicsContacts\":" << counters.physicsDynamicContacts
                  << ",\"dynamicY\":" << counters.lastDynamicY
#endif
                  << "}\n";
        return 1;
    }

    // Formal product sample name is tina_sample_2d; feature flags report which product
    // slices were compiled (Physics2D / FreeType). M10-A43 consumes the A42
    // locked world-pointer payload in fixedUpdate; A39 non-penetration remains
    // gated by the synthetic Runtime/UI test.
    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_2d\""
              << ",\"frames\":" << counters.frameUpdates << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"catalogFromRecipeFile\":" << (counters.catalogFromRecipeFile ? "true" : "false")
              << ",\"catalogRecipeAssets\":" << counters.catalogRecipeAssets
              << ",\"texturesUploaded\":" << counters.texturesUploaded
              << ",\"tileSpritesPerFrame\":" << ExpectedNonEmptyTiles
              << ",\"spritesPerFrame\":" << ExpectedSpritesWithPhysics
              << ",\"controllerGroundedFrames\":" << counters.controllerGroundedFrames
              << ",\"controllerWalkFrames\":" << counters.controllerWalkFrames
              << ",\"controllerHitRightFrames\":" << counters.controllerHitRightFrames
              << ",\"maxControllerX\":" << counters.maxControllerX
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
              << ",\"uiPanelsCreated\":" << counters.uiPanelsCreated
              << ",\"uiTextLabelsCreated\":" << counters.uiTextLabelsCreated
              << ",\"uiButtonsCreated\":" << counters.uiButtonsCreated
              << ",\"uiButtonActionsWired\":" << counters.uiButtonActionsWired
              << ",\"uiRootsReleased\":" << counters.uiRootsReleased
              << ",\"worldPointerPresses\":" << counters.tileSelection.pointerPresses
              << ",\"worldPointerMissingSamples\":" << counters.tileSelection.missingWorldPointerSamples
              << ",\"worldPointerViewportMisses\":" << counters.tileSelection.viewportMisses
              << ",\"worldPointerMapMisses\":" << counters.tileSelection.mapMisses
              << ",\"tileSelectionHits\":" << counters.tileSelection.selectionHits
              << ",\"hasTileSelection\":" << (lastSelection != nullptr ? "true" : "false")
              << ",\"lastSelectedCellX\":" << (lastSelection != nullptr ? lastSelection->cellX : 0U)
              << ",\"lastSelectedCellY\":" << (lastSelection != nullptr ? lastSelection->cellY : 0U)
              << ",\"lastSelectedTileId\":" << counters.lastSelectedTileId
              << ",\"lastSelectionWorldX\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.worldX : 0.0F)
              << ",\"lastSelectionWorldY\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.worldY : 0.0F)
              << ",\"lastSelectionInputSequence\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.inputSequence : 0U)
              << ",\"lastSelectionCameraRevision\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.cameraRevision : 0U)
              << ",\"lastSelectionSurfaceRevision\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.surfaceRevision : 0U)
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
              << ",\"physicsEnabled\":true"
              << ",\"physicsSteps\":" << counters.physicsSteps
              << ",\"physicsStaticBodies\":" << counters.physicsStaticBodies
              << ",\"physicsDynamicContacts\":" << counters.physicsDynamicContacts
              << ",\"lastDynamicY\":" << counters.lastDynamicY
#else
              << ",\"physicsEnabled\":false"
#endif
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
              << ",\"freetypeEnabled\":true"
#else
              << ",\"freetypeEnabled\":false"
#endif
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D) && defined(TINA_SAMPLE_TILEMAP_FREETYPE)
              << ",\"productGate\":\"bgfx-physics-freetype\""
#elif defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
              << ",\"productGate\":\"bgfx-physics\""
#elif defined(TINA_SAMPLE_TILEMAP_FREETYPE)
              << ",\"productGate\":\"bgfx-freetype\""
#else
              << ",\"productGate\":\"bgfx\""
#endif
              << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns << ",\"exit\":\""
              << "GameRequestedExitAfterCurrentFrame\"}\n";
    return 0;
}
