#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr u32 ProductSpriteKey = 1;
inline constexpr u32 CharacterSpriteKey = 2;
inline constexpr Tina::AssetFormat::TileMapLayerId VisualLayerId = 1;
inline constexpr Tina::AssetFormat::TileMapLayerId CollisionLayerId = 2;
inline constexpr u64 ExpectedNonEmptyTiles = 11; // 8 floor + 3 wall

struct SampleCapture final {
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    u64 totalSpriteItems = 0;
    u64 lastVisibleSpriteCount = 0;
    u64 lastTileSpriteCount = 0;
    u64 lastHadCharacterSprite = 0;
    u64 minVisibleSpriteCount = ~u64{0};
    u64 maxVisibleSpriteCount = 0;
    u64 controllerGroundedFrames = 0;
    u64 fixedSteps = 0;
    u64 renderShutdowns = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    bool lastFrameHadCamera = false;
    bool tileMapReady = false;
};

class RecordingNullRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    explicit RecordingNullRenderDevice(SampleCapture& capture) noexcept : capture_(&capture) {}

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        if (stopped_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::DeviceStopped,
                                       "The 2D tilemap render device is stopped");
        }
        if (frameOpen_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::FrameAlreadyOpen,
                                       "The 2D tilemap render device requires present between submits");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::UnexpectedFrameIndex,
                                       "2D tilemap frame indices must be contiguous");
        }

        const Tina::Render::RenderSceneView scene = frame.primaryWorldScene;
        capture_->lastFrameHadCamera = scene.camera2D().has_value();
        capture_->lastVisibleSpriteCount = scene.sprites2D().size();
        capture_->totalSpriteItems += scene.sprites2D().size();
        if (scene.sprites2D().size() < capture_->minVisibleSpriteCount)
        {
            capture_->minVisibleSpriteCount = scene.sprites2D().size();
        }
        if (scene.sprites2D().size() > capture_->maxVisibleSpriteCount)
        {
            capture_->maxVisibleSpriteCount = scene.sprites2D().size();
        }
        ++capture_->submittedFrames;
        ++nextFrameIndex_;
        frameOpen_ = true;
        return Tina::Render::RenderFrameSubmission::Submitted(capture_->submittedFrames - 1U);
    }

    [[nodiscard]] Tina::Core::Status present() override
    {
        if (stopped_ || !frameOpen_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::NoFrameSubmitted,
                                       "The 2D tilemap render device has no open frame");
        }
        frameOpen_ = false;
        ++capture_->presentedFrames;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Render::RenderStatistics statistics() const noexcept override
    {
        return Tina::Render::RenderStatistics{
            .submitted = capture_->submittedFrames,
            .presented = capture_->presentedFrames,
            .liveResources = 0,
        };
    }

    void shutdown() noexcept override
    {
        if (stopped_)
        {
            return;
        }
        stopped_ = true;
        frameOpen_ = false;
        ++capture_->renderShutdowns;
    }

  private:
    SampleCapture* capture_ = nullptr;
    u64 nextFrameIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

[[nodiscard]] Tina::Core::Result<u64> parseFrameCount(int argumentCount, char** arguments)
{
    constexpr std::string_view prefix = "--frames=";
    if (argumentCount != 2 || !std::string_view{arguments[1]}.starts_with(prefix))
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Expected exactly one --frames=N argument");
    }

    const std::string_view text = std::string_view{arguments[1]}.substr(prefix.size());
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--frames must be an unsigned integer greater than zero");
    }
    return value;
}

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

// 8x4: solid floor y=0; solid wall x=6 for y=1..3 (11 non-empty tiles).
[[nodiscard]] Tina::Core::Result<Tina::Asset::TileMapInstance>
makePlatformMap(std::pmr::memory_resource& memory)
{
    const auto tilesetId = *Tina::Core::AssetId::fromBytes(idBytes(31U));
    const auto mapId = *Tina::Core::AssetId::fromBytes(idBytes(32U));
    const std::array tiles{
        Tina::AssetFormat::TilesetTileDesc{
            .localId = 1,
            .materialFlags = Tina::AssetFormat::TilesetWire::MaterialSolid,
            .u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 0.5f,
            .v1 = 0.5f,
        },
        Tina::AssetFormat::TilesetTileDesc{
            .localId = 2,
            .materialFlags = Tina::AssetFormat::TilesetWire::MaterialSolid,
            .u0 = 0.5f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 0.5f,
        },
    };
    auto tilesetBytes = Tina::AssetFormat::writeTilesetPayloadBytes(
        Tina::AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
    if (!tilesetBytes)
    {
        return Tina::Core::failure(std::move(tilesetBytes.error()));
    }
    auto tileset = Tina::AssetFormat::parseTilesetPayload(*tilesetBytes);
    if (!tileset)
    {
        return Tina::Core::failure(std::move(tileset.error()));
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

    const std::array layers{
        Tina::AssetFormat::TileMapLayerDesc{
            .stableLayerId = VisualLayerId,
            .kind = Tina::AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .tiles = cells,
        },
        Tina::AssetFormat::TileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = Tina::AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .tiles = cells,
        },
    };
    auto mapBytes = Tina::AssetFormat::writeTileMapPayloadBytes(Tina::AssetFormat::TileMapPayloadDesc{
        .widthCells = 8,
        .heightCells = 4,
        .cellSizeMeters = 1.0f,
        .layers = layers,
        .tilesetId = tilesetId,
    });
    if (!mapBytes)
    {
        return Tina::Core::failure(std::move(mapBytes.error()));
    }
    auto map = Tina::AssetFormat::parseTileMapPayload(*mapBytes);
    if (!map)
    {
        return Tina::Core::failure(std::move(map.error()));
    }

    return Tina::Asset::TileMapInstance::Create(
        *map, *tileset, mapId, tilesetId,
        Tina::Asset::TileMapInstanceConfig{.chunkSizeCells = 4, .memoryResource = &memory});
}

class TileMap2DState final : public Tina::IGameState {
  public:
    TileMap2DState(u64 targetFrames, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        auto map = makePlatformMap(memory_);
        if (!map)
        {
            return Tina::Core::failure(std::move(map.error()));
        }
        map_.emplace(std::move(*map));
        grid_.emplace(*map_, CollisionLayerId);
        controller_.emplace(Tina::Asset::CharacterController2DConfig{
            .halfWidth = 0.3f,
            .halfHeight = 0.5f,
            .gravity = 40.0f,
            .maxFallSpeed = 50.0f,
            .skin = 0.01f,
        });
        // Drop onto floor from above open space (x≈1).
        controller_->teleport(1.0f, 3.0f, true);
        capture_->tileMapReady = true;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++capture_->stateExits;
        controller_.reset();
        grid_.reset();
        map_.reset();
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    // Headless Null smoke advances little wall-clock time, so fixedUpdate may run 0–1 times.
    // Step the grid controller once per frame with a synthetic fixed dt for deterministic landing.
    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        if (!controller_ || !grid_)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap controller not ready");
        }
        if (auto status = controller_->move(*grid_, 1.0f / 60.0f,
                                            Tina::Asset::CharacterController2DMoveInput{.wishVelocityX = 0.0f},
                                            solidScratch_);
            !status)
        {
            return status;
        }
        ++capture_->fixedSteps;
        if (controller_->state().grounded)
        {
            ++capture_->controllerGroundedFrames;
        }
        if (context.frameTiming().frameIndex + 1U == targetFrames_)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        if (!map_ || !controller_)
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
            .actualPixelsPerMeter = 32.0f,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::CameraTranslation,
        };
        if (auto status = writer.setCamera2D(camera); !status)
        {
            return status;
        }

        std::pmr::vector<Tina::Render::RenderSprite2DInput> tileSprites{&memory_};
        const Tina::Asset::TileChunkCameraQuery query{
            .centerX = camera.centerX,
            .centerY = camera.centerY,
            .halfWidth = camera.worldWidth * 0.5f,
            .halfHeight = camera.worldHeight * 0.5f,
        };
        auto emitted = Tina::Asset::emitVisibleTileMapSprites(
            *map_, VisualLayerId, query, Tina::Asset::TileChunkSpriteEmitParams{.spriteKey = ProductSpriteKey},
            tileSprites);
        if (!emitted)
        {
            return Tina::Core::failure(std::move(emitted.error()));
        }
        capture_->lastTileSpriteCount = *emitted;
        for (const auto& sprite : tileSprites)
        {
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }

        // Character as a second product sprite key (not a tile cell).
        const auto& st = controller_->state();
        const Tina::Render::RenderSprite2DInput character{
            .spriteKey = CharacterSpriteKey,
            .stableEntityKey = 900001,
            .centerX = st.positionX,
            .centerY = st.positionY,
            .widthMeters = controller_->config().halfWidth * 2.0f,
            .heightMeters = controller_->config().halfHeight * 2.0f,
            .sortingLayer = 1,
            .orderInLayer = 0,
            .red = 255,
            .green = 200,
            .blue = 64,
            .alpha = 255,
        };
        if (auto status = writer.addSprite2D(character); !status)
        {
            return status;
        }
        capture_->lastHadCharacterSprite = 1;
        return Tina::Core::success();
    }

  private:
    u64 targetFrames_ = 0;
    SampleCapture* capture_ = nullptr;
    mutable std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Tina::Asset::TileMapInstance> map_;
    std::optional<Tina::Asset::TileMapGridCollision> grid_;
    std::optional<Tina::Asset::CharacterController2D> controller_;
    std::pmr::vector<Tina::Asset::TileMapSolidHit> solidScratch_{&memory_};
};

class TileMap2DApplication final : public Tina::IGameApplication {
  public:
    TileMap2DApplication(u64 targetFrames, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{std::make_unique<TileMap2DState>(targetFrames_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++capture_->applicationShutdowns;
    }

  private:
    u64 targetFrames_ = 0;
    SampleCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineCompositionFactories makeFactories(SampleCapture& capture)
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{
                std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem = Tina::Task::createDisabledTaskSystem,
        .platformRender =
            Tina::IndependentPlatformRenderFactories{
                .createPlatformBackend = Tina::Platform::createHeadlessPlatformBackend,
                .createRenderDevice =
                    [&capture](const Tina::Render::RenderDeviceCreateParams&)
                        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                        return std::unique_ptr<Tina::Render::IRenderDevice>{
                            std::make_unique<RecordingNullRenderDevice>(capture)};
                    },
            },
    };
}

void printError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d_tilemap\",\"code\":" << error.code.value
              << ",\"message\":\"" << error.message << "\"}\n";
}

} // namespace

int main(int argumentCount, char** arguments)
{
    auto frameCountResult = parseFrameCount(argumentCount, arguments);
    if (!frameCountResult)
    {
        printError(frameCountResult.error());
        return 2;
    }
    const u64 frameCount = *frameCountResult;

    SampleCapture capture;
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    // Tiles (11) + character (1); leave headroom for culling edge cases.
    config.renderSceneCapacities.spriteCapacity = 64;
    auto hostResult = Tina::EngineHost::Create(config, makeFactories(capture));
    if (!hostResult)
    {
        printError(hostResult.error());
        return 1;
    }

    TileMap2DApplication application{frameCount, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();

    const bool ok = runResult && *runResult == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame &&
                    capture.tileMapReady && capture.submittedFrames == frameCount &&
                    capture.presentedFrames == frameCount && capture.lastFrameHadCamera &&
                    capture.lastTileSpriteCount == ExpectedNonEmptyTiles && capture.lastHadCharacterSprite == 1 &&
                    capture.lastVisibleSpriteCount == ExpectedNonEmptyTiles + 1 &&
                    capture.minVisibleSpriteCount == ExpectedNonEmptyTiles + 1 &&
                    capture.maxVisibleSpriteCount == ExpectedNonEmptyTiles + 1 &&
                    capture.controllerGroundedFrames > 0 && capture.fixedSteps > 0 &&
                    capture.renderShutdowns == 1 && capture.stateExits == 1 && capture.applicationShutdowns == 1;

    if (!ok)
    {
        if (!runResult)
        {
            printError(runResult.error());
        }
        else
        {
            std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d_tilemap\","
                         "\"message\":\"2D tilemap verification failed\","
                         "\"submitted\":"
                      << capture.submittedFrames << ",\"presented\":" << capture.presentedFrames
                      << ",\"lastSprites\":" << capture.lastVisibleSpriteCount
                      << ",\"lastTiles\":" << capture.lastTileSpriteCount
                      << ",\"groundedFrames\":" << capture.controllerGroundedFrames
                      << ",\"fixedSteps\":" << capture.fixedSteps << "}\n";
        }
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_2d_tilemap\",\"frames\":" << frameCount
              << ",\"tileSpritesPerFrame\":" << ExpectedNonEmptyTiles
              << ",\"spritesPerFrame\":" << (ExpectedNonEmptyTiles + 1)
              << ",\"controllerGroundedFrames\":" << capture.controllerGroundedFrames
              << ",\"fixedSteps\":" << capture.fixedSteps << ",\"stateExits\":" << capture.stateExits
              << ",\"applicationShutdowns\":" << capture.applicationShutdowns
              << ",\"renderShutdowns\":" << capture.renderShutdowns << "}\n";
    return 0;
}
