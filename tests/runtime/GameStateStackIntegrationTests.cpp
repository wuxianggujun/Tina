#include <gtest/gtest.h>

#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Tina::Tests {
namespace {

using EventLog = std::vector<std::string>;

struct StackProbe final {
    EventLog events;
    u64 fixedCallsBase = 0;
    u64 fixedCallsOverlay = 0;
    u64 frameCallsBase = 0;
    u64 frameCallsOverlay = 0;
    u64 extractCallsBase = 0;
    u64 extractCallsOverlay = 0;
    u64 uiCallsBase = 0;
    u64 uiCallsOverlay = 0;
    u64 overlayEnter = 0;
    u64 overlayExit = 0;
    u64 baseExit = 0;
    Render::IRenderDevice* overlayEnterDevice = nullptr;
    Render::IRenderDevice* overlayExitDevice = nullptr;
    // Non-empty state span is never present under blocksGameplayInputBelow (Host supplies suppressed).
    u64 baseFrameActionStateSpansWhileOverlay = 0;
    u64 baseFrameActionStateSpansWhileTop = 0;
};

class OverlayState final : public IGameState {
public:
    explicit OverlayState(StackProbe& probe) noexcept : probe_(&probe) {}

    Core::Status onEnter(GameStateEnterContext& context) override
    {
        probe_->overlayEnterDevice = &context.renderDevice();
        ++probe_->overlayEnter;
        probe_->events.emplace_back("overlay.enter");
        return Core::success();
    }

    void onExit(GameStateExitContext& context) noexcept override
    {
        probe_->overlayExitDevice = &context.renderDevice();
        ++probe_->overlayExit;
        probe_->events.emplace_back("overlay.exit");
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        // Pause-style overlay: freeze simulation + frame of layers below; still extract/UI self.
        return GameStatePolicy{
            .blocksGameplayInputBelow = true,
            .blocksUIUpdateBelow = false,
            .blocksFixedUpdateBelow = true,
            .blocksFrameUpdateBelow = true,
            .blocksRenderBelow = false,
        };
    }

    Core::Status fixedUpdate(FixedUpdateContext&) override
    {
        ++probe_->fixedCallsOverlay;
        probe_->events.emplace_back("overlay.fixed");
        return Core::success();
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        ++probe_->frameCallsOverlay;
        probe_->events.emplace_back("overlay.frame");
        // Pop after a couple of overlay frames so we exercise exit + base resume.
        if (probe_->frameCallsOverlay >= 2U)
        {
            EXPECT_TRUE(context.requestPop());
        }
        return Core::success();
    }

    Core::Status extractRenderScene(RenderSceneExtractionContext&) const override
    {
        ++probe_->extractCallsOverlay;
        return Core::success();
    }

    Core::Status updateUI(UIUpdateContext&) override
    {
        ++probe_->uiCallsOverlay;
        return Core::success();
    }

private:
    StackProbe* probe_ = nullptr;
};

class BaseState final : public IGameState {
public:
    BaseState(StackProbe& probe, u64 exitAfterFrames) noexcept
        : probe_(&probe), exitAfterFrames_(exitAfterFrames)
    {
    }

    Core::Status onEnter(GameStateEnterContext&) override
    {
        probe_->events.emplace_back("base.enter");
        return Core::success();
    }

    void onExit(GameStateExitContext&) noexcept override
    {
        ++probe_->baseExit;
        probe_->events.emplace_back("base.exit");
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Core::Status fixedUpdate(FixedUpdateContext&) override
    {
        ++probe_->fixedCallsBase;
        probe_->events.emplace_back("base.fixed");
        return Core::success();
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        ++probe_->frameCallsBase;
        probe_->events.emplace_back("base.frame");
        // Host must suppress gameplay actions for layers below an overlay that sets
        // blocksGameplayInputBelow (empty states span even if platform had input).
        if (probe_->overlayEnter > probe_->overlayExit)
        {
            if (!context.frameActions().states.empty())
            {
                ++probe_->baseFrameActionStateSpansWhileOverlay;
            }
        }
        else
        {
            // Record that we observed the snapshot object while top (may still be empty on Null).
            ++probe_->baseFrameActionStateSpansWhileTop;
        }
        // Frame 0: push overlay. After overlay pops, continue until exitAfterFrames base frames total.
        if (probe_->frameCallsBase == 1U && probe_->overlayEnter == 0U)
        {
            auto overlay = std::make_unique<OverlayState>(*probe_);
            EXPECT_TRUE(context.requestPush(std::move(overlay)));
        }
        if (probe_->frameCallsBase >= exitAfterFrames_ && probe_->overlayExit > 0U)
        {
            context.requestExitAfterFrame();
        }
        return Core::success();
    }

    Core::Status extractRenderScene(RenderSceneExtractionContext&) const override
    {
        ++probe_->extractCallsBase;
        return Core::success();
    }

    Core::Status updateUI(UIUpdateContext&) override
    {
        ++probe_->uiCallsBase;
        return Core::success();
    }

private:
    StackProbe* probe_ = nullptr;
    u64 exitAfterFrames_ = 0;
};

class StackDemoApplication final : public IGameApplication {
public:
    explicit StackDemoApplication(StackProbe& probe) noexcept : probe_(&probe) {}

    Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext&) override
    {
        // Base should run several frames after overlay pops (exit after 4 base frames).
        return std::unique_ptr<IGameState>{std::make_unique<BaseState>(*probe_, 4)};
    }

    void onShutdown(GameShutdownContext&) noexcept override { probe_->events.emplace_back("app.shutdown"); }

private:
    StackProbe* probe_ = nullptr;
};

[[nodiscard]] EngineCompositionFactories makeNullFactories()
{
    EngineCompositionFactories factories{};
    factories.createMonotonicClock = []() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        return std::unique_ptr<Core::IMonotonicClock>{std::make_unique<Core::SteadyMonotonicClock>()};
    };
    factories.createTaskSystem = Task::createDisabledTaskSystem;
    factories.platformRender = IndependentPlatformRenderFactories{
        .createPlatformBackend = Platform::createHeadlessPlatformBackend,
        .createRenderDevice = Render::createNullRenderDevice,
    };
    return factories;
}

TEST(GameStateStackIntegrationTest, PushOverlayBlocksBaseFixedAndFrameThenPopResumes)
{
    StackProbe probe{};
    auto hostResult = EngineHost::Create(EngineConfig::Defaults(), makeNullFactories());
    ASSERT_TRUE(hostResult.has_value()) << hostResult.error().message;
    StackDemoApplication application{probe};
    auto runResult = (*hostResult)->run(application);
    ASSERT_TRUE(runResult.has_value()) << runResult.error().message;
    EXPECT_EQ(*runResult, RunExitReason::GameRequestedExitAfterCurrentFrame);

    // Overlay entered once and exited via pop. Null is still a live IRenderDevice.
    EXPECT_EQ(probe.overlayEnter, 1U);
    EXPECT_EQ(probe.overlayExit, 1U);
    EXPECT_EQ(probe.baseExit, 1U);
    EXPECT_NE(probe.overlayEnterDevice, nullptr);
    EXPECT_EQ(probe.overlayEnterDevice, probe.overlayExitDevice);

    // Sequence: base frame0 (queues push) → commit → overlay frames (block base frame) → pop → base resumes.
    EXPECT_GE(probe.frameCallsBase, 3U);
    EXPECT_EQ(probe.frameCallsOverlay, 2U);

    // Overlay ran while base was not receiving frame updates for those two overlay frames:
    // total base frames would be much higher if base kept updating under overlay.
    // With block: base frames = 1 (pre-push) + N after pop; overlay = 2.
    EXPECT_LT(probe.frameCallsBase, probe.frameCallsBase + probe.frameCallsOverlay);

    // Render not blocked below: both may extract (blocksRenderBelow=false on overlay).
    EXPECT_GE(probe.extractCallsBase, 1U);
    EXPECT_GE(probe.extractCallsOverlay, 1U);

    // UI likewise not blocked below.
    EXPECT_GE(probe.uiCallsBase, 1U);
    EXPECT_GE(probe.uiCallsOverlay, 1U);

    // While overlay is active, base must never see a non-empty action state span.
    EXPECT_EQ(probe.baseFrameActionStateSpansWhileOverlay, 0U);
    EXPECT_GT(probe.baseFrameActionStateSpansWhileTop, 0U);

    // Event order fragments.
    auto find = [&](std::string_view token) {
        return std::find(probe.events.begin(), probe.events.end(), token) != probe.events.end();
    };
    EXPECT_TRUE(find("base.enter"));
    EXPECT_TRUE(find("overlay.enter"));
    EXPECT_TRUE(find("overlay.exit"));
    EXPECT_TRUE(find("base.exit"));
    EXPECT_TRUE(find("app.shutdown"));
}

TEST(GameStateStackIntegrationTest, FailedEnterKeepsBaseAndDoesNotCallCandidateExit)
{
    struct FailEnterState final : IGameState {
        StackProbe* probe = nullptr;
        explicit FailEnterState(StackProbe& p) noexcept : probe(&p) {}
        Core::Status onEnter(GameStateEnterContext&) override
        {
            probe->events.emplace_back("fail.enter");
            return Core::failure(RuntimeErrorCode::GameStateTransitionFailed, "forced");
        }
        void onExit(GameStateExitContext&) noexcept override { probe->events.emplace_back("fail.exit"); }
        [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override { return {}; }
    };

    struct BasePushFail final : IGameState {
        StackProbe* probe = nullptr;
        u64 frames = 0;
        explicit BasePushFail(StackProbe& p) noexcept : probe(&p) {}
        Core::Status onEnter(GameStateEnterContext&) override
        {
            probe->events.emplace_back("base.enter");
            return Core::success();
        }
        void onExit(GameStateExitContext&) noexcept override
        {
            ++probe->baseExit;
            probe->events.emplace_back("base.exit");
        }
        [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override { return {}; }
        Core::Status updateFrame(FrameUpdateContext& context) override
        {
            ++frames;
            if (frames == 1U)
            {
                EXPECT_TRUE(context.requestPush(std::make_unique<FailEnterState>(*probe)));
            }
            if (frames >= 3U)
            {
                context.requestExitAfterFrame();
            }
            return Core::success();
        }
    };

    struct App final : IGameApplication {
        StackProbe* probe = nullptr;
        explicit App(StackProbe& p) noexcept : probe(&p) {}
        Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext&) override
        {
            return std::unique_ptr<IGameState>{std::make_unique<BasePushFail>(*probe)};
        }
        void onShutdown(GameShutdownContext&) noexcept override {}
    };

    StackProbe probe{};
    auto hostResult = EngineHost::Create(EngineConfig::Defaults(), makeNullFactories());
    ASSERT_TRUE(hostResult.has_value());
    App application{probe};
    auto runResult = (*hostResult)->run(application);
    ASSERT_TRUE(runResult.has_value()) << runResult.error().message;

    EXPECT_TRUE(std::find(probe.events.begin(), probe.events.end(), "fail.enter") != probe.events.end());
    EXPECT_TRUE(std::find(probe.events.begin(), probe.events.end(), "fail.exit") == probe.events.end());
    EXPECT_EQ(probe.baseExit, 1U);
}

} // namespace
} // namespace Tina::Tests
