#include <tina/core/error/Error.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/FramePin.hpp>
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
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

using Tina::Core::u64;
using Tina::usize;

void releaseFixtureFrameResource(void*) noexcept {}

[[nodiscard]] Tina::Core::Result<Tina::Render::FrameResourceRef>
internFixtureFrameResource(Tina::Render::FrameResourceSink& sink,
                           Tina::Render::FrameResourceKind kind,
                           Tina::Core::u32 bindingKey) noexcept
{
    Tina::Render::FramePin pin{
        Tina::Render::FramePinKind::Custom,
        bindingKey,
        nullptr,
        &releaseFixtureFrameResource,
    };
    return sink.intern(
        Tina::Render::FrameResourceDescriptor{
            .kind = kind,
            .deviceBindingKey = bindingKey,
        },
        std::move(pin));
}

struct SampleCapture final {
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    u64 totalSubmittedMeshInputs = 0;
    u64 totalVisibleMeshItems = 0;
    u64 totalCulledMeshItems = 0;
    u64 totalInstanceBatches = 0;
    u64 lastVisibleMeshCount = 0;
    u64 lastInstanceBatchCount = 0;
    u64 lastSortChecksum = 0;
    std::optional<float> firstAspectRatio;
    std::optional<float> lastAspectRatio;
    u64 aspectChangeCount = 0;
    u64 liveResources = 0;
    u64 renderShutdowns = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
};

using SampleWindowPool = Tina::Core::GenerationPool<int, Tina::Platform::WindowRegistryTag>;

class ResizingWindowPlatformBackend final : public Tina::Platform::IPlatformBackend {
  public:
    ResizingWindowPlatformBackend(Tina::Platform::PlatformFrameBuilder frameBuilder,
                                  std::unique_ptr<SampleWindowPool> windowPool,
                                  Tina::Platform::WindowId primaryWindow,
                                  Tina::Platform::LogicalExtent initialExtent,
                                  u64 resizeFrame) noexcept
        : frameBuilder_(std::move(frameBuilder)), windowPool_(std::move(windowPool)),
          primaryWindow_(primaryWindow), initialExtent_(initialExtent), resizeFrame_(resizeFrame)
    {
    }

    [[nodiscard]] Tina::Core::Result<std::optional<Tina::Platform::WindowMetricsSnapshot>>
    initialPrimaryWindowMetrics() override
    {
        return makeMetrics(initialExtent_, 1U);
    }

    [[nodiscard]] Tina::Core::Result<Tina::Platform::PlatformPollResult> pollFrame() override
    {
        if (stopped_)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "The 3D extraction platform backend is stopped");
        }

        const u64 platformFrame = pollCount_ + 1U;
        if (auto status = frameBuilder_.beginFrame(Tina::Platform::PlatformFrameId{platformFrame}); !status)
        {
            return Tina::Core::failure(std::move(status.error()));
        }

        const bool resized = pollCount_ >= resizeFrame_;
        const Tina::Platform::LogicalExtent logicalExtent =
            resized ? Tina::Platform::LogicalExtent{800U, 800U} : initialExtent_;
        const u64 metricsRevision = resized ? 2U : 1U;
        const Tina::Platform::WindowMetricsSnapshot metrics = makeMetrics(logicalExtent, metricsRevision);
        const Tina::Platform::WindowInputSnapshot input{
            .window = primaryWindow_,
            .sourceMetricsRevision = metricsRevision,
        };
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics, input))
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "The 3D extraction platform snapshot was rejected");
        }

        auto frame = frameBuilder_.finishFrame();
        if (!frame)
        {
            return Tina::Core::failure(std::move(frame.error()));
        }
        ++pollCount_;
        return Tina::Platform::PlatformPollResult::Continue(*frame);
    }

    Tina::Core::Status updateTextInputPlacement(
        std::optional<Tina::Platform::TextInputPlacement> placement) override
    {
        static_cast<void>(placement);
        return stopped_
                   ? Tina::Core::failure(Tina::Platform::PlatformErrorCode::BackendStopped,
                                         "The 3D extraction platform backend is stopped")
                   : Tina::Core::success();
    }

    Tina::Core::Status setPointerCaptureMode(Tina::Platform::PointerCaptureMode mode) override
    {
        static_cast<void>(mode);
        return stopped_
                   ? Tina::Core::failure(Tina::Platform::PlatformErrorCode::BackendStopped,
                                         "The 3D extraction platform backend is stopped")
                   : Tina::Core::success();
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
    }

  private:
    [[nodiscard]] Tina::Platform::WindowMetricsSnapshot
    makeMetrics(Tina::Platform::LogicalExtent logicalExtent, u64 revision) const noexcept
    {
        return Tina::Platform::WindowMetricsSnapshot{
            .window = primaryWindow_,
            .logicalExtent = logicalExtent,
            .framebufferExtent = {logicalExtent.width, logicalExtent.height},
            .contentScale = {1.0F, 1.0F},
            .revision = revision,
            .focused = true,
            .visible = true,
        };
    }

    Tina::Platform::PlatformFrameBuilder frameBuilder_;
    std::unique_ptr<SampleWindowPool> windowPool_;
    Tina::Platform::WindowId primaryWindow_{};
    Tina::Platform::LogicalExtent initialExtent_{};
    u64 resizeFrame_ = 0;
    u64 pollCount_ = 0;
    bool stopped_ = false;
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
                                       "The 3D extraction render device is stopped");
        }
        if (frameOpen_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::FrameAlreadyOpen,
                                       "The 3D extraction render device requires present between submits");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::UnexpectedFrameIndex,
                                       "3D extraction frame indices must be contiguous");
        }

        const Tina::Render::RenderSceneView scene = frame.primaryWorldScene;
        if (!scene.perspectiveCamera().has_value() || scene.meshes3D().size() != 3U ||
            scene.mesh3DBatches().size() != 2U || scene.statistics().submittedMesh3DCount != 4U ||
            scene.statistics().visibleMesh3DCount != 3U || scene.statistics().culledMesh3DCount != 1U ||
            scene.statistics().mesh3DBatchCount != 2U || scene.statistics().mesh3DSortOrderChecksum == 0U)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::InvalidRenderSceneInput,
                                       "The 3D extraction scene did not satisfy its camera/culling/batch contract");
        }
        for (const Tina::Render::RenderMesh3DItem& mesh : scene.meshes3D())
        {
            if (frame.resources.resolve(mesh.mesh, Tina::Render::FrameResourceKind::Mesh3DGeometry) == nullptr ||
                frame.resources.resolve(mesh.material, Tina::Render::FrameResourceKind::Mesh3DMaterial) == nullptr)
            {
                return Tina::Core::failure(Tina::Render::RenderErrorCode::InvalidFrameResource,
                                           "The 3D extraction scene contains an invalid frame resource ref");
            }
        }

        const float aspectRatio = scene.perspectiveCamera()->aspectRatio;
        if (!capture_->firstAspectRatio.has_value())
        {
            capture_->firstAspectRatio = aspectRatio;
        }
        if (capture_->lastAspectRatio.has_value() && *capture_->lastAspectRatio != aspectRatio)
        {
            ++capture_->aspectChangeCount;
        }
        capture_->lastAspectRatio = aspectRatio;
        capture_->totalSubmittedMeshInputs += scene.statistics().submittedMesh3DCount;
        capture_->totalVisibleMeshItems += scene.meshes3D().size();
        capture_->totalCulledMeshItems += scene.statistics().culledMesh3DCount;
        capture_->totalInstanceBatches += scene.mesh3DBatches().size();
        capture_->lastVisibleMeshCount = scene.meshes3D().size();
        capture_->lastInstanceBatchCount = scene.mesh3DBatches().size();
        capture_->lastSortChecksum = scene.statistics().mesh3DSortOrderChecksum;
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
                                       "The 3D extraction render device has no open frame");
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
            .liveResources = capture_->liveResources,
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

class Extraction3DState final : public Tina::IGameState {
  public:
    Extraction3DState(u64 targetFrames, SampleCapture& capture) noexcept
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

        auto cameraResult = world_->createEntity(
            Tina::Scene::LocalTransform{.position = {0.0F, 0.0F, 6.0F}});
        if (!cameraResult)
        {
            return Tina::Core::failure(std::move(cameraResult.error()));
        }
        cameraEntity_ = *cameraResult;

        constexpr std::array<Tina::Math::Vec3, 4> positions{{
            {-1.5F, 0.0F, 0.0F},
            {1.5F, 0.0F, -1.0F},
            {0.0F, 0.0F, -3.0F},
            {100.0F, 0.0F, 0.0F},
        }};
        for (usize index = 0; index < positions.size(); ++index)
        {
            auto entityResult = world_->createEntity(
                Tina::Scene::LocalTransform{.position = positions[index]});
            if (!entityResult)
            {
                return Tina::Core::failure(std::move(entityResult.error()));
            }
            meshEntities_[index] = *entityResult;
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
                                       "3D extraction World was not initialized");
        }
        const Tina::Scene::WorldTransform* cameraTransform = world_->worldTransform(cameraEntity_);
        if (cameraTransform == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "3D extraction camera transform is unavailable");
        }

        auto& writer = context.renderSceneWriter();
        const Tina::Render::RenderPerspectiveCameraInput camera{
            .stableCameraKey = stableKey(cameraEntity_),
            .worldPose = makePose(*cameraTransform),
            .verticalFovDegrees = 60.0F,
            .nearPlaneMeters = 0.1F,
            .farPlaneMeters = 100.0F,
        };
        if (auto status = writer.setPerspectiveCamera(camera); !status)
        {
            return status;
        }

        constexpr std::array<Tina::u32, 4> meshKeys{1U, 1U, 2U, 3U};
        constexpr std::array<Tina::u32, 4> materialKeys{1U, 1U, 2U, 3U};
        constexpr std::array<Tina::Render::RenderLinearColor, 4> colors{{
            {0.10F, 0.45F, 0.95F, 1.0F},
            {0.10F, 0.75F, 0.55F, 1.0F},
            {0.95F, 0.35F, 0.20F, 1.0F},
            {1.0F, 1.0F, 1.0F, 1.0F},
        }};
        for (usize index = 0; index < meshEntities_.size(); ++index)
        {
            const Tina::Scene::WorldTransform* transform = world_->worldTransform(meshEntities_[index]);
            if (transform == nullptr)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "3D extraction mesh transform is unavailable");
            }
            auto meshResource = internFixtureFrameResource(
                context.frameResourceSink(), Tina::Render::FrameResourceKind::Mesh3DGeometry,
                meshKeys[index]);
            if (!meshResource)
            {
                return Tina::Core::failure(std::move(meshResource.error()));
            }
            auto materialResource = internFixtureFrameResource(
                context.frameResourceSink(), Tina::Render::FrameResourceKind::Mesh3DMaterial,
                materialKeys[index]);
            if (!materialResource)
            {
                return Tina::Core::failure(std::move(materialResource.error()));
            }
            const Tina::Render::RenderMesh3DInput mesh{
                .mesh = *meshResource,
                .material = *materialResource,
                .stableEntityKey = stableKey(meshEntities_[index]),
                .worldTransform = makeTransform(*transform),
                .localBounds = {.radius = 0.9F},
                .baseColorFactor = colors[index],
            };
            if (auto status = writer.addMesh3D(mesh); !status)
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

    [[nodiscard]] static Tina::Render::RenderPose3DInput
    makePose(const Tina::Scene::WorldTransform& transform) noexcept
    {
        return {
            .positionX = transform.position.x,
            .positionY = transform.position.y,
            .positionZ = transform.position.z,
            .rotationX = transform.rotation.x,
            .rotationY = transform.rotation.y,
            .rotationZ = transform.rotation.z,
            .rotationW = transform.rotation.w,
        };
    }

    [[nodiscard]] static Tina::Render::RenderTransform3DInput
    makeTransform(const Tina::Scene::WorldTransform& transform) noexcept
    {
        return {
            .pose = makePose(transform),
            .scaleX = transform.scale.x,
            .scaleY = transform.scale.y,
            .scaleZ = transform.scale.z,
        };
    }

    u64 targetFrames_ = 0;
    SampleCapture* capture_ = nullptr;
    std::optional<Tina::Scene::World> world_;
    Tina::Scene::EntityId cameraEntity_{};
    std::array<Tina::Scene::EntityId, 4> meshEntities_{};
};

class Extraction3DApplication final : public Tina::IGameApplication {
  public:
    Extraction3DApplication(u64 targetFrames, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<Extraction3DState>(targetFrames_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++capture_->applicationShutdowns;
    }

  private:
    u64 targetFrames_ = 0;
    SampleCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineCompositionFactories makeFactories(SampleCapture& capture, u64 targetFrames)
{
    Tina::EngineCompositionFactories factories;
    factories.createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
        return std::unique_ptr<Tina::Core::IMonotonicClock>{
            std::make_unique<Tina::Core::SteadyMonotonicClock>()};
    };
    factories.createTaskSystem = Tina::Task::createDisabledTaskSystem;

    auto& platformRender = std::get<Tina::IndependentPlatformRenderFactories>(factories.platformRender);
    platformRender.createPlatformBackend =
        [targetFrames](const Tina::Platform::PlatformBackendCreateParams& params)
        -> Tina::Core::Result<std::unique_ptr<Tina::Platform::IPlatformBackend>> {
        auto frameBuilder = Tina::Platform::PlatformFrameBuilder::Create(params.frameCapacities);
        if (!frameBuilder)
        {
            return Tina::Core::failure(std::move(frameBuilder.error()));
        }
        auto windowPoolResult = SampleWindowPool::Create(1);
        if (!windowPoolResult)
        {
            return Tina::Core::failure(std::move(windowPoolResult.error()));
        }
        auto windowPool = std::make_unique<SampleWindowPool>(std::move(*windowPoolResult));
        auto windowResult = windowPool->tryEmplace(0);
        if (!windowResult)
        {
            return Tina::Core::failure(std::move(windowResult.error()));
        }
        const u64 resizeFrame = targetFrames > 1U ? targetFrames / 2U : 1U;
        return std::unique_ptr<Tina::Platform::IPlatformBackend>{
            std::make_unique<ResizingWindowPlatformBackend>(
                std::move(*frameBuilder), std::move(windowPool), *windowResult,
                params.primaryWindow.initialLogicalExtent, resizeFrame)};
    };
    platformRender.createRenderDevice =
        [&capture](const Tina::Render::RenderDeviceCreateParams&)
        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
        return std::unique_ptr<Tina::Render::IRenderDevice>{
            std::make_unique<RecordingNullRenderDevice>(capture)};
    };
    return factories;
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
    config.renderSceneCapacities.mesh3DItemCapacity = 8;
    config.renderSceneCapacities.mesh3DBatchCapacity = 4;
    auto hostResult = Tina::EngineHost::Create(config, makeFactories(capture, frameCount));
    if (!hostResult)
    {
        printError(hostResult.error());
        return 1;
    }

    Extraction3DApplication application{frameCount, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();

    const u64 expectedAspectChanges = frameCount > 1U ? 1U : 0U;
    const float expectedFinalAspect = frameCount > 1U ? 1.0F : 16.0F / 9.0F;
    if (!runResult || *runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        capture.submittedFrames != frameCount || capture.presentedFrames != frameCount ||
        capture.totalSubmittedMeshInputs != frameCount * 4U ||
        capture.totalVisibleMeshItems != frameCount * 3U ||
        capture.totalCulledMeshItems != frameCount || capture.totalInstanceBatches != frameCount * 2U ||
        capture.lastVisibleMeshCount != 3U || capture.lastInstanceBatchCount != 2U ||
        capture.lastSortChecksum == 0U || !capture.firstAspectRatio.has_value() ||
        std::abs(*capture.firstAspectRatio - 16.0F / 9.0F) > 1.0e-5F ||
        !capture.lastAspectRatio.has_value() ||
        std::abs(*capture.lastAspectRatio - expectedFinalAspect) > 1.0e-5F ||
        capture.aspectChangeCount != expectedAspectChanges || capture.liveResources != 0U ||
        capture.renderShutdowns != 1U || capture.stateExits != 1U || capture.applicationShutdowns != 1U)
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
            writer.member("message", "3D extraction verification failed");
            writer.endObject();
            std::cerr << '\n';
        }
        return 1;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_3d_extraction");
        writer.member("frames", frameCount);
        writer.member("submittedMeshesPerFrame", 4);
        writer.member("visibleMeshesPerFrame", 3);
        writer.member("culledMeshesPerFrame", 1);
        writer.member("instanceBatchesPerFrame", 2);
        writer.member("aspectChanges", capture.aspectChangeCount);
        writer.member("stateExits", capture.stateExits);
        writer.member("applicationShutdowns", capture.applicationShutdowns);
        writer.member("renderShutdowns", capture.renderShutdowns);
        writer.member("liveResources", capture.liveResources);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}
