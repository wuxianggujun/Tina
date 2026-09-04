#include <tina/core/error/Error.hpp>
#include <tina/core/text/JsonWriter.hpp>
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
#include <tina/scene/World.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "SampleSpriteFrameResource.hpp"

namespace {

using Tina::Core::u64;
using Tina::usize;

struct SampleCapture final {
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    u64 totalSpriteItems = 0;
    u64 lastVisibleSpriteCount = 0;
    u64 liveResources = 0;
    u64 renderShutdowns = 0;
    bool lastFrameHadCamera = false;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
};

class RecordingNullRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    explicit RecordingNullRenderDevice(SampleCapture& capture) noexcept : capture_(&capture)
    {
    }

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        if (stopped_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::DeviceStopped,
                                       "The 2D infrastructure render device is stopped");
        }
        if (frameOpen_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::FrameAlreadyOpen,
                                       "The 2D infrastructure render device requires present between submits");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::UnexpectedFrameIndex,
                                       "2D infrastructure frame indices must be contiguous");
        }

        const Tina::Render::RenderSceneView scene = frame.primaryWorldScene;
        capture_->lastFrameHadCamera = scene.camera2D().has_value();
        capture_->lastVisibleSpriteCount = scene.sprites2D().size();
        capture_->totalSpriteItems += scene.sprites2D().size();
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
                                       "The 2D infrastructure render device has no open frame");
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

class Infrastructure2DState final : public Tina::IGameState {
  public:
    Infrastructure2DState(u64 targetFrames, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{16});
        if (!worldResult)
        {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        auto cameraResult = world_->createEntity();
        if (!cameraResult)
        {
            return Tina::Core::failure(std::move(cameraResult.error()));
        }
        cameraEntity_ = *cameraResult;

        const Tina::Scene::LocalTransform spriteTransforms[] = {
            Tina::Scene::LocalTransform{.position = {-2.0F, 0.0F, 0.0F}},
            Tina::Scene::LocalTransform{.position = {0.0F, 0.0F, 0.0F}},
            Tina::Scene::LocalTransform{.position = {2.0F, 0.0F, 0.0F}},
        };
        for (usize index = 0; index < 3; ++index)
        {
            auto spriteResult = world_->createEntity(spriteTransforms[index]);
            if (!spriteResult)
            {
                return Tina::Core::failure(std::move(spriteResult.error()));
            }
            spriteEntities_[index] = *spriteResult;
        }
        return world_->updateWorldTransforms();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++capture_->stateExits;
        world_.reset();
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        if (context.frameTiming().frameIndex + 1U == targetFrames_)
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
                                       "2D infrastructure World was not initialized");
        }
        const Tina::Scene::WorldTransform* cameraTransform = world_->worldTransform(cameraEntity_);
        if (cameraTransform == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "2D infrastructure camera transform is unavailable");
        }

        auto& writer = context.renderSceneWriter();
        Tina::Render::RenderCamera2DInput camera{
            .stableCameraKey = stableKey(cameraEntity_),
            .centerX = cameraTransform->position.x,
            .centerY = cameraTransform->position.y,
            .worldWidth = 16.0F,
            .worldHeight = 9.0F,
            .actualPixelsPerMeter = 32.0F,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::CameraTranslation,
        };
        if (auto status = writer.setCamera2D(camera); !status)
        {
            return status;
        }

        for (usize index = 0; index < spriteEntities_.size(); ++index)
        {
            const Tina::Scene::WorldTransform* transform = world_->worldTransform(spriteEntities_[index]);
            if (transform == nullptr)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "2D infrastructure sprite transform is unavailable");
            }
            auto texture = spriteFrameResource_.intern(
                context.frameResourceSink(), static_cast<Tina::u64>(index + 1U));
            if (!texture)
            {
                return Tina::Core::failure(std::move(texture.error()));
            }
            Tina::Render::RenderSprite2DInput sprite{
                .texture = *texture,
                .stableEntityKey = stableKey(spriteEntities_[index]),
                .centerX = transform->position.x,
                .centerY = transform->position.y,
                .widthMeters = 1.5F,
                .heightMeters = 1.5F,
                .sortingLayer = 0,
                .orderInLayer = static_cast<Tina::i32>(index),
            };
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }
        return Tina::Core::success();
    }

  private:
    [[nodiscard]] static u64 stableKey(Tina::Scene::EntityId entity) noexcept
    {
        return (static_cast<u64>(entity.index()) << 32U) | entity.generation();
    }

    u64 targetFrames_ = 0;
    SampleCapture* capture_ = nullptr;
    std::optional<Tina::Scene::World> world_;
    mutable Tina::Samples::SampleSpriteFrameResource spriteFrameResource_{};
    Tina::Scene::EntityId cameraEntity_{};
    std::array<Tina::Scene::EntityId, 3> spriteEntities_{};
};

class Infrastructure2DApplication final : public Tina::IGameApplication {
  public:
    Infrastructure2DApplication(u64 targetFrames, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<Infrastructure2DState>(targetFrames_, *capture_)};
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
        .platformRender = Tina::IndependentPlatformRenderFactories{
            .createPlatformBackend = Tina::Platform::createHeadlessPlatformBackend,
            .createRenderDevice = [&capture](const Tina::Render::RenderDeviceCreateParams&)
                -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                return std::unique_ptr<Tina::Render::IRenderDevice>{
                    std::make_unique<RecordingNullRenderDevice>(capture)};
            },
        },
    };
}

void printError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

} // namespace

int runInfrastructure2dSample(int argumentCount, char** arguments)
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
    config.renderSceneCapacities.spriteCapacity = 8;
    auto hostResult = Tina::EngineHost::Create(config, makeFactories(capture));
    if (!hostResult)
    {
        printError(hostResult.error());
        return 1;
    }

    Infrastructure2DApplication application{frameCount, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    if (!runResult || *runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        capture.submittedFrames != frameCount || capture.presentedFrames != frameCount ||
        capture.totalSpriteItems != frameCount * 3U || !capture.lastFrameHadCamera ||
        capture.lastVisibleSpriteCount != 3U || capture.liveResources != 0U || capture.renderShutdowns != 1U ||
        capture.stateExits != 1U || capture.applicationShutdowns != 1U)
    {
        if (!runResult)
        {
            printError(runResult.error());
        }
        else
        {
            Tina::Core::JsonWriter writer(std::cerr);
            writer.beginObject();
            writer.member("status", "error");
            writer.member("message", "2D infrastructure verification failed");
            writer.endObject();
            std::cerr << '\n';
        }
        return 1;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_2d_infrastructure");
        writer.member("frames", frameCount);
        writer.member("spritesPerFrame", 3);
        writer.member("stateExits", capture.stateExits);
        writer.member("applicationShutdowns", capture.applicationShutdowns);
        writer.member("renderShutdowns", capture.renderShutdowns);
        writer.member("liveResources", capture.liveResources);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}
