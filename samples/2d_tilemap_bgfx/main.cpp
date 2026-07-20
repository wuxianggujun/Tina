#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
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
};

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
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d_tilemap_bgfx\",\"message\":";
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
};

[[nodiscard]] Tina::Core::Status prepareCatalog(TileMapResources& resources)
{
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const auto tileMapId = *Tina::Core::AssetId::fromBytes(idBytes(3U));

    // 16x16 atlas: left half floor (orange), right half wall (cyan).
    constexpr u16 Size = 16;
    std::vector<std::byte> pixels(static_cast<std::size_t>(Size) * Size * 4U);
    for (u16 y = 0; y < Size; ++y)
    {
        for (u16 x = 0; x < Size; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * Size + x) * 4U;
            const bool wall = x >= 8;
            const bool stripe = ((x + y) % 4) < 2;
            if (wall)
            {
                pixels[offset + 0] = stripe ? std::byte{40} : std::byte{20};
                pixels[offset + 1] = stripe ? std::byte{200} : std::byte{140};
                pixels[offset + 2] = stripe ? std::byte{220} : std::byte{180};
            } else
            {
                pixels[offset + 0] = stripe ? std::byte{240} : std::byte{180};
                pixels[offset + 1] = stripe ? std::byte{140} : std::byte{90};
                pixels[offset + 2] = stripe ? std::byte{40} : std::byte{20};
            }
            pixels[offset + 3] = std::byte{255};
        }
    }

    auto texPayload = Tina::AssetFormat::writeTexture2DPayloadBytes(Tina::AssetFormat::Texture2DPayloadDesc{
        .width = Size,
        .height = Size,
        .pixels = pixels,
    });
    if (!texPayload)
    {
        return Tina::Core::failure(std::move(texPayload.error()));
    }

    const std::array tiles{
        Tina::AssetFormat::TilesetTileDesc{
            .localId = 1,
            .materialFlags = Tina::AssetFormat::TilesetWire::MaterialSolid,
            .u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 0.5f,
            .v1 = 1.0f,
        },
        Tina::AssetFormat::TilesetTileDesc{
            .localId = 2,
            .materialFlags = Tina::AssetFormat::TilesetWire::MaterialSolid,
            .u0 = 0.5f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 1.0f,
        },
    };
    auto tilesetPayload = Tina::AssetFormat::writeTilesetPayloadBytes(Tina::AssetFormat::TilesetPayloadDesc{
        .tilePixelWidth = 8,
        .tilePixelHeight = 16,
        .tiles = tiles,
        .textureId = textureId,
    });
    if (!tilesetPayload)
    {
        return Tina::Core::failure(std::move(tilesetPayload.error()));
    }

    std::array<u16, 32> cells{};
    for (u32 x = 0; x < 8; ++x)
    {
        cells[x] = 1;
    }
    for (u32 y = 1; y < 4; ++y)
    {
        cells[y * 8 + 6] = 2;
    }
    auto mapPayload = Tina::AssetFormat::writeTileMapPayloadBytes(Tina::AssetFormat::TileMapPayloadDesc{
        .widthCells = 8,
        .heightCells = 4,
        .cellSizeMeters = 1.0f,
        .tiles = cells,
        .tilesetId = tilesetId,
    });
    if (!mapPayload)
    {
        return Tina::Core::failure(std::move(mapPayload.error()));
    }

    Tina::Asset::CatalogCookRequest request{.targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .assetTypeVersion = Tina::AssetFormat::Texture2DWire::SchemaVersion,
        .payload = std::move(*texPayload),
    });
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Tileset,
        .assetId = tilesetId,
        .assetTypeVersion = Tina::AssetFormat::TilesetWire::SchemaVersion,
        .payload = std::move(*tilesetPayload),
        .dependencies =
            {
                Tina::AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = Tina::AssetFormat::AssetKind::Texture2D,
                    .flags = Tina::AssetFormat::DependencyFlags::Required,
                },
            },
    });
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::TileMap,
        .assetId = tileMapId,
        .assetTypeVersion = Tina::AssetFormat::TileMapWire::SchemaVersion,
        .payload = std::move(*mapPayload),
        .dependencies =
            {
                Tina::AssetFormat::CookedAssetWriteDependency{
                    .assetId = tilesetId,
                    .expectedKind = Tina::AssetFormat::AssetKind::Tileset,
                    .flags = Tina::AssetFormat::DependencyFlags::Required,
                },
            },
    });

    resources.catalogRoot = std::filesystem::temp_directory_path() / "tina_sample_2d_tilemap_bgfx_pkg";
    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);
    const auto rootUtf8 = [&] {
        const auto u8 = resources.catalogRoot.u8string();
        return std::string(u8.begin(), u8.end());
    }();
    if (auto cookStatus = Tina::Asset::cookAndPublishCatalogPackage(rootUtf8, request); !cookStatus)
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

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
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
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (auto* device = capture_->get(); device != nullptr && resources_->gpuTexture)
        {
            (void)device->setSprite2DTextureBinding(ProductSpriteKey, {});
            (void)device->destroyTexture2D(resources_->gpuTexture);
            resources_->gpuTexture = {};
        }
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (resources_->controller && resources_->grid)
        {
            if (auto status = resources_->controller->move(
                    *resources_->grid, 1.0f / 60.0f,
                    Tina::Asset::CharacterController2DMoveInput{.wishVelocityX = 0.0f}, resources_->solidScratch);
                !status)
            {
                return status;
            }
            if (resources_->controller->state().grounded)
            {
                ++counters_->controllerGroundedFrames;
            }
        }
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
        counters_->lastTotalSprites = *emitted + 1U;
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    TileMapResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
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

[[nodiscard]] Tina::EngineCompositionFactories createFactories(DeviceCapture& capture)
{
    return Tina::EngineCompositionFactories{
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
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina TileMap Catalog 2D Sample";
    config.primaryWindow.title = "Tina Catalog TileMap + Character";
    config.primaryWindow.initialLogicalExtent = {960, 540};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 64;
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

    TileMapResources resources{};
    if (const auto status = prepareCatalog(resources); !status)
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

    LifecycleCounters counters{};
    TileMapBgfxApplication application{*options, counters, resources, capture};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }

    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);

    const bool ok = counters.texturesUploaded == 1 && counters.lastTileSprites == ExpectedNonEmptyTiles &&
                    counters.lastTotalSprites == ExpectedNonEmptyTiles + 1 && counters.controllerGroundedFrames > 0 &&
                    counters.renderExtractions == counters.frameUpdates && counters.stateExits == 1 &&
                    counters.applicationShutdowns == 1 &&
                    *run == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame;
    if (!ok)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d_tilemap_bgfx\","
                     "\"message\":\"verification failed\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"tileSprites\":" << counters.lastTileSprites
                  << ",\"totalSprites\":" << counters.lastTotalSprites
                  << ",\"grounded\":" << counters.controllerGroundedFrames << "}\n";
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_2d_tilemap_bgfx\""
              << ",\"frames\":" << counters.frameUpdates << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"texturesUploaded\":" << counters.texturesUploaded
              << ",\"tileSpritesPerFrame\":" << ExpectedNonEmptyTiles
              << ",\"spritesPerFrame\":" << (ExpectedNonEmptyTiles + 1)
              << ",\"controllerGroundedFrames\":" << counters.controllerGroundedFrames
              << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns << ",\"exit\":\""
              << "GameRequestedExitAfterCurrentFrame\"}\n";
    return 0;
}
