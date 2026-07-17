#include "WindowSurfaceLeaseAccess.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

struct WindowSurfaceRuntimeProbe final {
    std::vector<std::string> events;
    std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl;
    std::vector<u64> engineFrameIndices;
    std::vector<u64> submittedSurfaceRevisions;
    std::vector<u64> submissionIndices;
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    u64 skippedFrames = 0;
    usize leaseCountAtRenderShutdown = 99;
    usize leaseCountAtPlatformShutdown = 99;
    bool lifecycleOrderingViolation = false;
    bool renderFactorySawInitialSurface = false;
    bool switchedWindowWasValid = false;
    bool gameShutdown = false;
};

struct RuntimeWindowRecord final {};
struct RuntimeSurfaceRecord final {};

using RuntimeWindowPool = Core::GenerationPool<RuntimeWindowRecord, Platform::WindowRegistryTag>;
using RuntimeSurfacePool = Core::GenerationPool<RuntimeSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

enum class WindowSurfaceScriptViolation : u8 {
    None,
    SourceWindowChanged,
    SourceMetricsRevisionMovedBackward,
    SurfaceFactsChangedWithoutNewMetricsRevision,
    SurfaceRevisionSkipped,
};

class ScriptedWindowSurfacePlatform final : public Integration::IWindowSurfacePlatformBackend {
  public:
    ScriptedWindowSurfacePlatform(WindowSurfaceRuntimeProbe& probe, Platform::PlatformFrameBuilder frameBuilder,
                                  RuntimeWindowPool windows, Platform::WindowId primaryWindow,
                                  Platform::WindowId alternateWindow, RuntimeSurfacePool surfaces,
                                  Integration::WindowSurfaceId surface,
                                  std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl,
                                  bool failPublication, WindowSurfaceScriptViolation violation) noexcept
        : probe_(&probe), frameBuilder_(std::move(frameBuilder)), windows_(std::move(windows)),
          primaryWindow_(primaryWindow), alternateWindow_(alternateWindow), surfaces_(std::move(surfaces)),
          surface_(surface), leaseControl_(std::move(leaseControl)), failPublication_(failPublication),
          violation_(violation)
    {
        setScriptState(0);
    }

    ~ScriptedWindowSurfacePlatform() noexcept override
    {
        shutdown();
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollFrame() override
    {
        if (stopped_)
        {
            return Core::failure(Platform::PlatformErrorCode::BackendStopped, "The scripted backend is stopped");
        }
        if (scriptIndex_ == 3)
        {
            return Platform::PlatformPollResult::Exit();
        }

        setScriptState(scriptIndex_);
        auto begin = frameBuilder_.beginFrame(Platform::PlatformFrameId{scriptIndex_ + 1});
        if (!begin)
        {
            return Core::failure(std::move(begin.error()));
        }
        Platform::WindowInputSnapshot input{
            .window = activeWindow_,
            .sourceMetricsRevision = metrics_.revision,
        };
        input.pointer.pointer = Platform::PrimaryPointerId;
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics_, input) || !frameBuilder_.setGamepadSnapshots({}))
        {
            return Core::failure(Platform::PlatformErrorCode::InvalidFrameSnapshot,
                                 "The scripted WindowSurface frame could not be committed");
        }
        auto frame = frameBuilder_.finishFrame();
        if (!frame)
        {
            return Core::failure(std::move(frame.error()));
        }
        ++scriptIndex_;
        return Platform::PlatformPollResult::Continue(*frame);
    }

    void shutdown() noexcept override
    {
        if (stopped_)
        {
            return;
        }
        stopped_ = true;
        probe_->leaseCountAtPlatformShutdown = leaseControl_->activeLeaseCount;
        if (leaseControl_->activeLeaseCount != 0)
        {
            probe_->lifecycleOrderingViolation = true;
        }
        leaseControl_->surfaceAlive = false;
        probe_->events.emplace_back("platform.shutdown");
        (void)surfaces_.erase(surface_);
        surface_ = {};
        (void)windows_.erase(primaryWindow_);
        primaryWindow_ = {};
        (void)windows_.erase(alternateWindow_);
        alternateWindow_ = {};
        activeWindow_ = {};
    }

    [[nodiscard]] Core::Result<Integration::NativeWindowSurfaceLease>
    acquirePrimaryWindowSurfaceLease() noexcept override
    {
        probe_->events.emplace_back("platform.acquire-surface-lease");
        if (leaseAcquired_)
        {
            return Core::failure(Platform::PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired,
                                 "The scripted surface lease was already acquired");
        }
        auto lease = Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(
            leaseControl_, surface_,
            Integration::Detail::NativeWindowBinding{
                .kind = Integration::Detail::NativeWindowBindingKind::Win32,
                .nativeDisplay = 0,
                .nativeWindow = 1,
                .bindingRevision = 1,
            });
        if (lease)
        {
            leaseAcquired_ = true;
        }
        return lease;
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot>
    primaryWindowSurfaceSnapshot() const noexcept override
    {
        return snapshot_;
    }

    [[nodiscard]] Core::Status publishPrimaryWindow() noexcept override
    {
        probe_->events.emplace_back("platform.publish-window");
        if (failPublication_)
        {
            return Core::failure(Platform::PlatformErrorCode::WindowPublicationFailed,
                                 "The scripted window publication failed");
        }
        published_ = true;
        return Core::success();
    }

  private:
    void setScriptState(u64 index) noexcept
    {
        const bool switchSourceWindow = violation_ == WindowSurfaceScriptViolation::SourceWindowChanged && index >= 1;
        activeWindow_ = switchSourceWindow ? alternateWindow_ : primaryWindow_;
        if (switchSourceWindow)
        {
            probe_->switchedWindowWasValid = windows_.contains(activeWindow_);
        }

        const bool suspended = index == 1;
        const u32 width = suspended ? 0U : (index == 2 ? 800U : 640U);
        const u32 height = suspended ? 0U : (index == 2 ? 600U : 480U);
        u64 metricsRevision = index + 1;
        if (violation_ == WindowSurfaceScriptViolation::SourceMetricsRevisionMovedBackward)
        {
            metricsRevision = index == 0 ? 2U : index;
        } else if (violation_ == WindowSurfaceScriptViolation::SurfaceFactsChangedWithoutNewMetricsRevision &&
                   index >= 1)
        {
            metricsRevision = index;
        }
        metrics_ = Platform::WindowMetricsSnapshot{
            .window = activeWindow_,
            .logicalExtent = {width == 0 ? 640U : width, height == 0 ? 480U : height},
            .framebufferExtent = {width, height},
            .contentScale = {1.0F, 1.0F},
            .revision = metricsRevision,
            .focused = !suspended,
            .minimized = suspended,
            .visible = published_,
        };
        snapshot_ = Integration::WindowSurfaceSnapshot{
            .surface = surface_,
            .sourceWindow = activeWindow_,
            .framebufferExtent = metrics_.framebufferExtent,
            .contentScale = metrics_.contentScale,
            .sourceMetricsRevision = metrics_.revision,
            .surfaceRevision =
                violation_ == WindowSurfaceScriptViolation::SurfaceRevisionSkipped && index >= 1 ? index + 2
                                                                                                  : index + 1,
            .suspended = suspended,
        };
    }

    WindowSurfaceRuntimeProbe* probe_;
    Platform::PlatformFrameBuilder frameBuilder_;
    RuntimeWindowPool windows_;
    Platform::WindowId primaryWindow_{};
    Platform::WindowId alternateWindow_{};
    Platform::WindowId activeWindow_{};
    RuntimeSurfacePool surfaces_;
    Integration::WindowSurfaceId surface_{};
    std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> leaseControl_;
    Platform::WindowMetricsSnapshot metrics_{};
    Integration::WindowSurfaceSnapshot snapshot_{};
    u64 scriptIndex_ = 0;
    bool failPublication_ = false;
    bool leaseAcquired_ = false;
    bool published_ = false;
    bool stopped_ = false;
    WindowSurfaceScriptViolation violation_ = WindowSurfaceScriptViolation::None;
};

class ScriptedSurfaceRenderDevice final : public Render::IRenderDevice {
  public:
    ScriptedSurfaceRenderDevice(WindowSurfaceRuntimeProbe& probe, Integration::NativeWindowSurfaceLease lease) noexcept
        : probe_(&probe), lease_(std::move(lease))
    {
    }

    ~ScriptedSurfaceRenderDevice() noexcept override
    {
        shutdown();
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame& frame) override
    {
        if (stopped_)
        {
            return Core::failure(Render::RenderErrorCode::DeviceStopped, "The scripted render device is stopped");
        }
        probe_->engineFrameIndices.push_back(frame.frameIndex);
        if (!frame.primaryWindowSurface.has_value())
        {
            return Core::failure(Render::RenderErrorCode::InvalidSurfaceState,
                                 "The scripted render device requires a WindowSurface");
        }
        if (frame.primaryWindowSurface->availability == Render::RenderSurfaceAvailability::Suspended)
        {
            ++probe_->skippedFrames;
            return Render::RenderFrameSubmission::SkippedSuspendedSurface();
        }

        const u64 submissionIndex = probe_->submittedFrames++;
        probe_->submittedSurfaceRevisions.push_back(frame.primaryWindowSurface->surfaceRevision);
        probe_->submissionIndices.push_back(submissionIndex);
        frameOpen_ = true;
        return Render::RenderFrameSubmission::Submitted(submissionIndex);
    }

    [[nodiscard]] Core::Status present() override
    {
        if (stopped_ || !frameOpen_)
        {
            return Core::failure(Render::RenderErrorCode::NoFrameSubmitted,
                                 "The scripted render device has no frame to present");
        }
        frameOpen_ = false;
        ++probe_->presentedFrames;
        return Core::success();
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return Render::RenderStatistics{
            .submitted = probe_->submittedFrames,
            .presented = probe_->presentedFrames,
            .skippedSuspendedSurfaceFrames = probe_->skippedFrames,
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
        lease_ = {};
        probe_->leaseCountAtRenderShutdown = probe_->leaseControl->activeLeaseCount;
        probe_->events.emplace_back("render.shutdown");
    }

  private:
    WindowSurfaceRuntimeProbe* probe_;
    Integration::NativeWindowSurfaceLease lease_;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

class PassiveGameState final : public IGameState {
  public:
    [[nodiscard]] Core::Status onEnter(GameStateEnterContext&) override
    {
        return Core::success();
    }

    void onExit(GameStateExitContext&) noexcept override
    {
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }
};

class PassiveGameApplication final : public IGameApplication {
  public:
    explicit PassiveGameApplication(WindowSurfaceRuntimeProbe& probe) noexcept : probe_(&probe)
    {
    }

    [[nodiscard]] Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext&) override
    {
        std::unique_ptr<IGameState> state = std::make_unique<PassiveGameState>();
        return state;
    }

    void onShutdown(GameShutdownContext&) noexcept override
    {
        probe_->gameShutdown = true;
    }

  private:
    WindowSurfaceRuntimeProbe* probe_;
};

enum class WindowSurfaceFactoryFailure : u8 {
    None,
    RenderFactory,
    WindowPublication,
};

[[nodiscard]] EngineCompositionFactories makeWindowSurfaceFactories(WindowSurfaceRuntimeProbe& probe,
                                                                    WindowSurfaceFactoryFailure failure,
                                                                    WindowSurfaceScriptViolation violation =
                                                                        WindowSurfaceScriptViolation::None)
{
    EngineCompositionFactories factories;
    factories.createMonotonicClock = [&probe]() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        probe.events.emplace_back("factory.clock");
        std::unique_ptr<Core::IMonotonicClock> clock = std::make_unique<Core::SteadyMonotonicClock>();
        return clock;
    };
    factories.createTaskSystem = [&probe](const Task::TaskSystemCreateParams& params) {
        probe.events.emplace_back("factory.task");
        return Task::createDisabledTaskSystem(params);
    };
    factories.platformRender = WindowSurfacePlatformRenderFactories{
        .createWindowSurfacePlatformBackend = [&probe, failure,
                                               violation](const Platform::PlatformBackendCreateParams& params)
            -> Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>> {
            probe.events.emplace_back("factory.platform");
            auto builder = Platform::PlatformFrameBuilder::Create(params.frameCapacities);
            if (!builder)
            {
                return Core::failure(std::move(builder.error()));
            }
            auto windows = RuntimeWindowPool::Create(2);
            auto surfaces = RuntimeSurfacePool::Create(1);
            if (!windows || !surfaces)
            {
                return Core::failure(Core::CoreErrorCode::OutOfMemory, "The scripted registries could not be created");
            }
            auto window = windows->tryEmplace();
            auto alternateWindow = windows->tryEmplace();
            auto surface = surfaces->tryEmplace();
            if (!window || !alternateWindow || !surface)
            {
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "The scripted registries could not allocate their primary slots");
            }
            auto control = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
            control->ownerThread = std::this_thread::get_id();
            probe.leaseControl = control;
            std::unique_ptr<Integration::IWindowSurfacePlatformBackend> platform =
                std::make_unique<ScriptedWindowSurfacePlatform>(
                    probe, std::move(*builder), std::move(*windows), *window, *alternateWindow, std::move(*surfaces),
                    *surface, std::move(control), failure == WindowSurfaceFactoryFailure::WindowPublication,
                    violation);
            return platform;
        },
        .createWindowSurfaceRenderDevice = [&probe, failure](const Render::RenderDeviceCreateParams& params,
                                                             Integration::NativeWindowSurfaceLease lease)
            -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
            probe.events.emplace_back("factory.render");
            probe.renderFactorySawInitialSurface = params.initialPrimaryWindowSurface.has_value();
            if (failure == WindowSurfaceFactoryFailure::RenderFactory)
            {
                return Core::failure(Core::CoreErrorCode::Internal, "The scripted WindowSurface render factory failed");
            }
            std::unique_ptr<Render::IRenderDevice> render =
                std::make_unique<ScriptedSurfaceRenderDevice>(probe, std::move(lease));
            return render;
        },
    };
    return factories;
}

[[nodiscard]] usize eventPosition(const WindowSurfaceRuntimeProbe& probe, const std::string& event)
{
    const auto iterator = std::find(probe.events.begin(), probe.events.end(), event);
    return iterator == probe.events.end() ? probe.events.size()
                                          : static_cast<usize>(std::distance(probe.events.begin(), iterator));
}

} // namespace

TEST(WindowSurfaceRuntimeTest, PublishesAfterRenderCreationAndSeparatesEngineFramesFromSubmissions)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(EngineConfig::Defaults(),
                                   makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::None));
    ASSERT_TRUE(host.has_value());
    ASSERT_NE(*host, nullptr);
    EXPECT_TRUE(probe.renderFactorySawInitialSurface);
    EXPECT_LT(eventPosition(probe, "factory.render"), eventPosition(probe, "platform.publish-window"));

    PassiveGameApplication gameApplication{probe};
    auto run = (*host)->run(gameApplication);
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(*run, RunExitReason::PrimaryWindowRequestedClose);
    EXPECT_TRUE(probe.gameShutdown);
    EXPECT_EQ(probe.engineFrameIndices, (std::vector<u64>{0, 1, 2}));
    EXPECT_EQ(probe.submittedSurfaceRevisions, (std::vector<u64>{1, 3}));
    EXPECT_EQ(probe.submissionIndices, (std::vector<u64>{0, 1}));
    EXPECT_EQ(probe.submittedFrames, 2U);
    EXPECT_EQ(probe.presentedFrames, 2U);
    EXPECT_EQ(probe.skippedFrames, 1U);
    EXPECT_EQ(probe.leaseCountAtRenderShutdown, 0U);
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_LT(eventPosition(probe, "render.shutdown"), eventPosition(probe, "platform.shutdown"));
}

TEST(WindowSurfaceRuntimeTest, RenderFactoryFailureReleasesLeaseBeforePlatformRollback)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(EngineConfig::Defaults(),
                                   makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::RenderFactory));
    ASSERT_FALSE(host.has_value());
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_EQ(eventPosition(probe, "platform.publish-window"), probe.events.size());
}

TEST(WindowSurfaceRuntimeTest, PublicationFailureShutsRenderAndLeaseBeforePlatformRollback)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(EngineConfig::Defaults(),
                                   makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::WindowPublication));
    ASSERT_FALSE(host.has_value());
    EXPECT_EQ(probe.leaseCountAtRenderShutdown, 0U);
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_LT(eventPosition(probe, "render.shutdown"), eventPosition(probe, "platform.shutdown"));
}

TEST(WindowSurfaceRuntimeTest, ValidatesWindowSurfaceBranchBeforeInvokingAnyFactory)
{
    WindowSurfaceRuntimeProbe probe;
    auto factories = makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::None);
    std::get<WindowSurfacePlatformRenderFactories>(factories.platformRender).createWindowSurfaceRenderDevice = {};

    auto host = EngineHost::Create(EngineConfig::Defaults(), std::move(factories));
    ASSERT_FALSE(host.has_value());
    EXPECT_EQ(host.error().code, ConfigurationErrorCode::IncompleteEngineFactoryBundle);
    EXPECT_TRUE(probe.events.empty());
}

TEST(WindowSurfaceRuntimeTest, RejectsSourceWindowChangeWhenPlatformAndProviderReuseTheSurface)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(
        EngineConfig::Defaults(), makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::None,
                                                             WindowSurfaceScriptViolation::SourceWindowChanged));
    ASSERT_TRUE(host.has_value());

    PassiveGameApplication gameApplication{probe};
    auto run = (*host)->run(gameApplication);
    ASSERT_FALSE(run.has_value());
    EXPECT_EQ(run.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(run.error().message, "WindowSurface source window identity changed");
    EXPECT_TRUE(probe.switchedWindowWasValid);
    EXPECT_TRUE(probe.gameShutdown);
    EXPECT_EQ(probe.engineFrameIndices, (std::vector<u64>{0}));
    EXPECT_EQ(probe.leaseCountAtRenderShutdown, 0U);
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_LT(eventPosition(probe, "render.shutdown"), eventPosition(probe, "platform.shutdown"));
}

TEST(WindowSurfaceRuntimeTest, RejectsSourceMetricsRevisionMovingBackwardAcrossFrames)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(
        EngineConfig::Defaults(),
        makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::None,
                                   WindowSurfaceScriptViolation::SourceMetricsRevisionMovedBackward));
    ASSERT_TRUE(host.has_value());

    PassiveGameApplication gameApplication{probe};
    auto run = (*host)->run(gameApplication);
    ASSERT_FALSE(run.has_value());
    EXPECT_EQ(run.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(run.error().message, "WindowSurface source metrics revision moved backward");
    EXPECT_TRUE(probe.gameShutdown);
    EXPECT_EQ(probe.engineFrameIndices, (std::vector<u64>{0}));
    EXPECT_EQ(probe.leaseCountAtRenderShutdown, 0U);
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_LT(eventPosition(probe, "render.shutdown"), eventPosition(probe, "platform.shutdown"));
}

TEST(WindowSurfaceRuntimeTest, RejectsSurfaceFactsWithoutANewSourceMetricsRevision)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(
        EngineConfig::Defaults(),
        makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::None,
                                   WindowSurfaceScriptViolation::SurfaceFactsChangedWithoutNewMetricsRevision));
    ASSERT_TRUE(host.has_value());

    PassiveGameApplication gameApplication{probe};
    auto run = (*host)->run(gameApplication);
    ASSERT_FALSE(run.has_value());
    EXPECT_EQ(run.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(run.error().message, "WindowSurface facts changed without a new source metrics revision");
    EXPECT_TRUE(probe.gameShutdown);
    EXPECT_EQ(probe.engineFrameIndices, (std::vector<u64>{0}));
    EXPECT_EQ(probe.leaseCountAtRenderShutdown, 0U);
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_LT(eventPosition(probe, "render.shutdown"), eventPosition(probe, "platform.shutdown"));
}

TEST(WindowSurfaceRuntimeTest, RejectsSkippedSurfaceRevision)
{
    WindowSurfaceRuntimeProbe probe;
    auto host = EngineHost::Create(
        EngineConfig::Defaults(),
        makeWindowSurfaceFactories(probe, WindowSurfaceFactoryFailure::None,
                                   WindowSurfaceScriptViolation::SurfaceRevisionSkipped));
    ASSERT_TRUE(host.has_value());

    PassiveGameApplication gameApplication{probe};
    auto run = (*host)->run(gameApplication);
    ASSERT_FALSE(run.has_value());
    EXPECT_EQ(run.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(run.error().message,
              "WindowSurface revision must advance exactly once for each committed state change");
    EXPECT_TRUE(probe.gameShutdown);
    EXPECT_EQ(probe.engineFrameIndices, (std::vector<u64>{0}));
    EXPECT_EQ(probe.leaseCountAtRenderShutdown, 0U);
    EXPECT_EQ(probe.leaseCountAtPlatformShutdown, 0U);
    EXPECT_FALSE(probe.lifecycleOrderingViolation);
    EXPECT_LT(eventPosition(probe, "render.shutdown"), eventPosition(probe, "platform.shutdown"));
}

} // namespace Tina::Tests
