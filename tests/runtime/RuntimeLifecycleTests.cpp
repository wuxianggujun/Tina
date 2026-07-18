#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/TaskSystem.hpp>

#include "support/ManualMonotonicClock.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace Tina::Tests {
namespace {

using EventLog = std::vector<std::string>;

[[nodiscard]] bool containsEvent(const EventLog& events, std::string_view expected)
{
    return std::ranges::find(events, expected) != events.end();
}

[[nodiscard]] bool containsEventPrefix(const EventLog& events, std::string_view prefix)
{
    return std::ranges::any_of(events, [prefix](const std::string& event) { return event.starts_with(prefix); });
}

void expectEventSuffix(const EventLog& events, const EventLog& expectedSuffix)
{
    ASSERT_GE(events.size(), expectedSuffix.size());
    EXPECT_TRUE(std::ranges::equal(events.end() - static_cast<std::ptrdiff_t>(expectedSuffix.size()), events.end(),
                                   expectedSuffix.begin(), expectedSuffix.end()));
}

class LoggingClock final : public Core::IMonotonicClock {
  public:
    explicit LoggingClock(EventLog& events) noexcept : events_(&events)
    {
    }

    ~LoggingClock() override
    {
        events_->emplace_back("clock.destroy");
    }

    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return {};
    }

  private:
    EventLog* events_;
};

class LoggingPlatform final : public Platform::IPlatformBackend {
  public:
    explicit LoggingPlatform(EventLog& events) noexcept : events_(&events)
    {
    }

    ~LoggingPlatform() override
    {
        events_->emplace_back("platform.destroy");
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollFrame() override
    {
        return Platform::PlatformPollResult::Exit();
    }

    void shutdown() noexcept override
    {
        if (!stopped_)
        {
            events_->emplace_back("platform.shutdown");
            stopped_ = true;
        }
    }

  private:
    EventLog* events_;
    bool stopped_ = false;
};

class LoggingTaskSystem final : public Task::ITaskSystem {
  public:
    explicit LoggingTaskSystem(EventLog& events) noexcept : events_(&events)
    {
    }

    ~LoggingTaskSystem() override
    {
        events_->emplace_back("task.destroy");
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        return true;
    }

    void shutdownAndJoin() noexcept override
    {
        if (!stopped_)
        {
            events_->emplace_back("task.shutdown");
            stopped_ = true;
        }
    }

  private:
    EventLog* events_;
    bool stopped_ = false;
};

class LoggingRenderDevice final : public Render::IRenderDevice {
  public:
    explicit LoggingRenderDevice(EventLog& events) noexcept : events_(&events)
    {
    }

    ~LoggingRenderDevice() override
    {
        events_->emplace_back("render.destroy");
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame&) override
    {
        const u64 submissionIndex = statistics_.submitted;
        ++statistics_.submitted;
        return Render::RenderFrameSubmission::Submitted(submissionIndex);
    }

    [[nodiscard]] Core::Status present() override
    {
        ++statistics_.presented;
        return Core::success();
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return statistics_;
    }

    void shutdown() noexcept override
    {
        if (!stopped_)
        {
            events_->emplace_back("render.shutdown");
            stopped_ = true;
        }
    }

  private:
    EventLog* events_;
    Render::RenderStatistics statistics_{};
    bool stopped_ = false;
};

enum class FactoryStage {
    Clock,
    Platform,
    Task,
    Render,
};

enum class FactoryMode {
    Failure,
    SuccessNull,
    Throw,
};

template <typename Interface, typename CreateProduct>
Core::Result<std::unique_ptr<Interface>> injectedFactoryResult(EventLog& events, std::string_view event,
                                                               FactoryStage currentStage, FactoryStage injectedStage,
                                                               FactoryMode mode, CreateProduct&& createProduct)
{
    events.emplace_back(event);
    if (currentStage != injectedStage)
    {
        return std::forward<CreateProduct>(createProduct)();
    }

    switch (mode)
    {
    case FactoryMode::Failure:
        return Core::failure(Core::CoreErrorCode::Internal, "injected factory failure");
    case FactoryMode::SuccessNull:
        return std::unique_ptr<Interface>{};
    case FactoryMode::Throw:
        throw std::runtime_error("injected factory exception");
    }
    return Core::failure(Core::CoreErrorCode::Internal, "unreachable factory mode");
}

EngineCompositionFactories makeInjectedFactories(EventLog& events, FactoryStage injectedStage, FactoryMode mode)
{
    EngineCompositionFactories factories;
    auto& platformRender = std::get<IndependentPlatformRenderFactories>(factories.platformRender);
    factories.createMonotonicClock = [&events, injectedStage,
                                      mode]() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        return injectedFactoryResult<Core::IMonotonicClock>(
            events, "factory.clock", FactoryStage::Clock, injectedStage, mode, [&events] {
                std::unique_ptr<Core::IMonotonicClock> clock = std::make_unique<LoggingClock>(events);
                return clock;
            });
    };
    platformRender.createPlatformBackend =
        [&events, injectedStage, mode](
            const Platform::PlatformBackendCreateParams&) -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        return injectedFactoryResult<Platform::IPlatformBackend>(
            events, "factory.platform", FactoryStage::Platform, injectedStage, mode, [&events] {
                std::unique_ptr<Platform::IPlatformBackend> platform = std::make_unique<LoggingPlatform>(events);
                return platform;
            });
    };
    factories.createTaskSystem =
        [&events, injectedStage,
         mode](const Task::TaskSystemCreateParams&) -> Core::Result<std::unique_ptr<Task::ITaskSystem>> {
        return injectedFactoryResult<Task::ITaskSystem>(
            events, "factory.task", FactoryStage::Task, injectedStage, mode, [&events] {
                std::unique_ptr<Task::ITaskSystem> taskSystem = std::make_unique<LoggingTaskSystem>(events);
                return taskSystem;
            });
    };
    platformRender.createRenderDevice =
        [&events, injectedStage,
         mode](const Render::RenderDeviceCreateParams&) -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
        return injectedFactoryResult<Render::IRenderDevice>(
            events, "factory.render", FactoryStage::Render, injectedStage, mode, [&events] {
                std::unique_ptr<Render::IRenderDevice> renderDevice = std::make_unique<LoggingRenderDevice>(events);
                return renderDevice;
            });
    };
    return factories;
}

void removeFactory(EngineCompositionFactories& factories, FactoryStage stage)
{
    auto& platformRender = std::get<IndependentPlatformRenderFactories>(factories.platformRender);
    switch (stage)
    {
    case FactoryStage::Clock:
        factories.createMonotonicClock = {};
        break;
    case FactoryStage::Platform:
        platformRender.createPlatformBackend = {};
        break;
    case FactoryStage::Task:
        factories.createTaskSystem = {};
        break;
    case FactoryStage::Render:
        platformRender.createRenderDevice = {};
        break;
    }
}

[[nodiscard]] EventLog expectedRollbackEvents(FactoryStage failedStage)
{
    switch (failedStage)
    {
    case FactoryStage::Clock:
        return {"factory.clock"};
    case FactoryStage::Platform:
        return {"factory.clock", "factory.platform", "clock.destroy"};
    case FactoryStage::Task:
        return {
            "factory.clock",     "factory.platform", "factory.task",
            "platform.shutdown", "platform.destroy", "clock.destroy",
        };
    case FactoryStage::Render:
        return {
            "factory.clock", "factory.platform",  "factory.task",     "factory.render", "task.shutdown",
            "task.destroy",  "platform.shutdown", "platform.destroy", "clock.destroy",
        };
    }
    return {};
}

struct InjectedFactoryCase final {
    FactoryStage stage;
    FactoryMode mode;
};

class EngineFactoryRollbackTest : public testing::TestWithParam<InjectedFactoryCase> {};

TEST_P(EngineFactoryRollbackTest, ConvertsFactoryOutcomeAndRollsBackInReverseOrder)
{
    EventLog events;
    const InjectedFactoryCase testCase = GetParam();
    auto hostResult =
        EngineHost::Create(EngineConfig::Defaults(), makeInjectedFactories(events, testCase.stage, testCase.mode));

    ASSERT_FALSE(hostResult.has_value());
    switch (testCase.mode)
    {
    case FactoryMode::Failure:
        EXPECT_EQ(hostResult.error().code, Core::CoreErrorCode::Internal);
        break;
    case FactoryMode::SuccessNull:
        EXPECT_EQ(hostResult.error().code, RuntimeErrorCode::EngineFactoryReturnedNull);
        break;
    case FactoryMode::Throw:
        EXPECT_EQ(hostResult.error().code, RuntimeErrorCode::EngineFactoryThrewException);
        break;
    }
    EXPECT_EQ(events, expectedRollbackEvents(testCase.stage));
}

INSTANTIATE_TEST_SUITE_P(EveryFactoryStageAndOutcome, EngineFactoryRollbackTest,
                         testing::Values(InjectedFactoryCase{FactoryStage::Clock, FactoryMode::Failure},
                                         InjectedFactoryCase{FactoryStage::Clock, FactoryMode::SuccessNull},
                                         InjectedFactoryCase{FactoryStage::Clock, FactoryMode::Throw},
                                         InjectedFactoryCase{FactoryStage::Platform, FactoryMode::Failure},
                                         InjectedFactoryCase{FactoryStage::Platform, FactoryMode::SuccessNull},
                                         InjectedFactoryCase{FactoryStage::Platform, FactoryMode::Throw},
                                         InjectedFactoryCase{FactoryStage::Task, FactoryMode::Failure},
                                         InjectedFactoryCase{FactoryStage::Task, FactoryMode::SuccessNull},
                                         InjectedFactoryCase{FactoryStage::Task, FactoryMode::Throw},
                                         InjectedFactoryCase{FactoryStage::Render, FactoryMode::Failure},
                                         InjectedFactoryCase{FactoryStage::Render, FactoryMode::SuccessNull},
                                         InjectedFactoryCase{FactoryStage::Render, FactoryMode::Throw}));

class MissingEngineFactoryTest : public testing::TestWithParam<FactoryStage> {};

TEST_P(MissingEngineFactoryTest, ValidatesCompleteBundleBeforeInvokingAnyFactory)
{
    EventLog events;
    auto factories = makeInjectedFactories(events, FactoryStage::Clock, FactoryMode::Failure);
    removeFactory(factories, GetParam());

    auto hostResult = EngineHost::Create(EngineConfig::Defaults(), std::move(factories));

    ASSERT_FALSE(hostResult.has_value());
    EXPECT_EQ(hostResult.error().code, ConfigurationErrorCode::IncompleteEngineFactoryBundle);
    EXPECT_TRUE(events.empty());
}

INSTANTIATE_TEST_SUITE_P(EveryFactorySlot, MissingEngineFactoryTest,
                         testing::Values(FactoryStage::Clock, FactoryStage::Platform, FactoryStage::Task,
                                         FactoryStage::Render));

enum class CommittedFailurePoint {
    None,
    PlatformPoll,
    FixedUpdate,
    UpdateFrame,
    ExtractRenderScene,
    UpdateUI,
    RenderSubmit,
    RenderPresent,
};

enum class InjectedOutcome {
    ReturnError,
    Throw,
};

using RuntimeWindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using RuntimeGamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;

struct ScriptedKeyTransition final {
    Platform::Key key = Platform::Key::Unknown;
    Platform::DigitalTransition state = Platform::DigitalTransition::Down;
    bool repeat = false;
};

struct RuntimeProbe final {
    EventLog events;
    ManualMonotonicClock* clock = nullptr;
    std::vector<Core::Duration> frameDeltas;
    CommittedFailurePoint failurePoint = CommittedFailurePoint::None;
    InjectedOutcome failureOutcome = InjectedOutcome::ReturnError;
    bool platformExitRequested = false;
    bool emitPlatformEvent = false;
    bool emitPlatformEventOnEveryFrame = false;
    bool emitUnrepresentableUiPointerMove = false;
    std::optional<std::size_t> replacePrimaryWindowOnFrame;
    std::vector<u64> platformFrameIds;
    std::vector<std::vector<Platform::Key>> heldKeysByFrame;
    std::vector<std::vector<ScriptedKeyTransition>> keyTransitionsByFrame;
    std::size_t pollCount = 0;
    u64 submitCalls = 0;
    u64 presentCalls = 0;
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    bool lastSubmittedHadPrimaryWindowSurface = false;
};

class AdvancingPlatform final : public Platform::IPlatformBackend {
  public:
    AdvancingPlatform(RuntimeProbe& probe, Platform::PlatformFrameBuilder frameBuilder,
                      std::unique_ptr<RuntimeWindowPool> windowPool, Platform::WindowId primaryWindow) noexcept
        : probe_(&probe), frameBuilder_(std::move(frameBuilder)), windowPool_(std::move(windowPool)),
          primaryWindow_(primaryWindow)
    {
    }

    ~AdvancingPlatform() override
    {
        probe_->events.emplace_back("platform.destroy");
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollFrame() override
    {
        const std::size_t frameIndex = probe_->pollCount++;
        probe_->events.emplace_back("platform.poll." + std::to_string(frameIndex));
        if (probe_->failurePoint == CommittedFailurePoint::PlatformPoll)
        {
            if (probe_->failureOutcome == InjectedOutcome::Throw)
            {
                throw std::runtime_error("platform poll exception");
            }
            return Core::failure(Core::CoreErrorCode::Internal, "platform poll failure");
        }
        if (probe_->platformExitRequested)
        {
            return Platform::PlatformPollResult::Exit();
        }
        if (frameIndex < probe_->frameDeltas.size())
        {
            probe_->clock->advance(probe_->frameDeltas[frameIndex]);
        }
        if (probe_->replacePrimaryWindowOnFrame == frameIndex)
        {
            if (windowPool_->erase(primaryWindow_) != Core::GenerationEraseResult::Erased)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "scripted primary Window generation could not be retired");
            }
            auto replacementWindow = windowPool_->tryEmplace(0);
            if (!replacementWindow)
            {
                return Core::failure(std::move(replacementWindow.error()));
            }
            primaryWindow_ = *replacementWindow;
        }
        const u64 platformFrameId = frameIndex < probe_->platformFrameIds.size() ? probe_->platformFrameIds[frameIndex]
                                                                                 : static_cast<u64>(frameIndex) + 1U;
        auto beginStatus = frameBuilder_.beginFrame(Platform::PlatformFrameId{platformFrameId});
        if (!beginStatus)
        {
            return Core::failure(std::move(beginStatus.error()));
        }
        Platform::WindowMetricsSnapshot metrics{
            .window = primaryWindow_,
            .logicalExtent = {1280, 720},
            .framebufferExtent = {1280, 720},
            .contentScale = {1.0F, 1.0F},
            .revision = platformFrameId,
            .focused = true,
            .visible = true,
        };
        Platform::WindowInputSnapshot input{
            .window = primaryWindow_,
            .sourceMetricsRevision = platformFrameId,
        };
        if (frameIndex < probe_->heldKeysByFrame.size())
        {
            for (Platform::Key key : probe_->heldKeysByFrame[frameIndex])
            {
                input.heldKeys.set(static_cast<usize>(key));
            }
        }
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics, input))
        {
            return Core::failure(Core::CoreErrorCode::Internal, "scripted primary Window snapshot was rejected");
        }
        if ((probe_->emitPlatformEvent && frameIndex == 0) || probe_->emitPlatformEventOnEveryFrame)
        {
            const auto appendResult = frameBuilder_.appendPlatformEvent(Platform::PlatformEventStreamReset{
                .reason = Platform::PlatformEventResetReason::BackendRecovery,
            });
            if (appendResult != Platform::FrameBatchAppendResult::ResetInserted)
            {
                return Core::failure(Core::CoreErrorCode::Internal, "scripted platform event was not appended");
            }
        }
        if (probe_->emitUnrepresentableUiPointerMove && frameIndex == 0)
        {
            const auto appendResult = frameBuilder_.appendInputTransition(Platform::PointerMoveTransition{
                .window = primaryWindow_,
                .pointer = Platform::PrimaryPointerId,
                .logicalX = (std::numeric_limits<double>::max)(),
                .logicalY = 0.0,
                .deltaX = 0.0,
                .deltaY = 0.0,
            });
            if (appendResult != Platform::FrameBatchAppendResult::Appended)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "scripted UI pointer Input transition was not appended");
            }
        }
        if (frameIndex < probe_->keyTransitionsByFrame.size())
        {
            for (const ScriptedKeyTransition& transition : probe_->keyTransitionsByFrame[frameIndex])
            {
                const auto appendResult = frameBuilder_.appendInputTransition(Platform::KeyTransition{
                    .window = primaryWindow_,
                    .key = transition.key,
                    .state = transition.state,
                    .repeat = transition.repeat,
                });
                if (appendResult != Platform::FrameBatchAppendResult::Appended)
                {
                    return Core::failure(Core::CoreErrorCode::Internal,
                                         "scripted key Input transition was not appended");
                }
            }
        }
        auto frame = frameBuilder_.finishFrame();
        if (!frame)
        {
            return Core::failure(std::move(frame.error()));
        }
        return Platform::PlatformPollResult::Continue(*frame);
    }

    void shutdown() noexcept override
    {
        if (!stopped_)
        {
            probe_->events.emplace_back("platform.shutdown");
            stopped_ = true;
        }
    }

  private:
    RuntimeProbe* probe_;
    Platform::PlatformFrameBuilder frameBuilder_;
    std::unique_ptr<RuntimeWindowPool> windowPool_;
    Platform::WindowId primaryWindow_{};
    bool stopped_ = false;
};

enum class OversizedPlatformFrameKind {
    RawInputBatch,
    TextByteBatch,
    PlatformEventBatch,
};

class OversizedPlatformFrameBackend final : public Platform::IPlatformBackend {
  public:
    OversizedPlatformFrameBackend(RuntimeProbe& probe, Platform::PlatformFrameBuilder frameBuilder,
                                  std::unique_ptr<RuntimeWindowPool> windowPool,
                                  std::unique_ptr<RuntimeGamepadPool> gamepadPool, Platform::WindowId primaryWindow,
                                  Platform::GamepadId gamepad, OversizedPlatformFrameKind frameKind) noexcept
        : probe_(&probe), frameBuilder_(std::move(frameBuilder)), windowPool_(std::move(windowPool)),
          gamepadPool_(std::move(gamepadPool)), primaryWindow_(primaryWindow), gamepad_(gamepad), frameKind_(frameKind)
    {
    }

    ~OversizedPlatformFrameBackend() override
    {
        probe_->events.emplace_back("platform.destroy");
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollFrame() override
    {
        const std::size_t frameIndex = probe_->pollCount++;
        probe_->events.emplace_back("platform.poll." + std::to_string(frameIndex));

        if (auto beginStatus = frameBuilder_.beginFrame(Platform::PlatformFrameId{static_cast<u64>(frameIndex) + 1U});
            !beginStatus)
        {
            return Core::failure(std::move(beginStatus.error()));
        }

        constexpr u64 metricsRevision = 1;
        const Platform::WindowMetricsSnapshot metrics{
            .window = primaryWindow_,
            .logicalExtent = {1280, 720},
            .framebufferExtent = {1280, 720},
            .contentScale = {1.0F, 1.0F},
            .revision = metricsRevision,
            .focused = true,
            .visible = true,
        };
        const Platform::WindowInputSnapshot input{
            .window = primaryWindow_,
            .sourceMetricsRevision = metricsRevision,
        };
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics, input))
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "oversized backend primary Window snapshot was rejected");
        }
        if (frameKind_ != OversizedPlatformFrameKind::PlatformEventBatch &&
            frameBuilder_.appendPlatformEvent(Platform::WindowMetricsChangedEvent{
                .window = primaryWindow_,
                .metricsRevision = metricsRevision,
            }) != Platform::FrameBatchAppendResult::Appended)
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "oversized backend callback probe event could not be constructed");
        }

        switch (frameKind_)
        {
        case OversizedPlatformFrameKind::RawInputBatch:
            if (frameBuilder_.appendInputTransition(Platform::PointerWheelTransition{
                    .window = primaryWindow_,
                    .deltaY = 1.0,
                }) != Platform::FrameBatchAppendResult::Appended ||
                frameBuilder_.appendInputTransition(Platform::PointerWheelTransition{
                    .window = primaryWindow_,
                    .deltaY = -1.0,
                }) != Platform::FrameBatchAppendResult::Appended)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "oversized raw input batch could not be constructed");
            }
            break;
        case OversizedPlatformFrameKind::TextByteBatch:
            if (frameBuilder_.appendInputTransition(Platform::TextInputTransition{
                    .window = primaryWindow_,
                    .committedUtf8 = "ab",
                }) != Platform::FrameBatchAppendResult::Appended ||
                frameBuilder_.appendInputTransition(Platform::TextCompositionTransition{
                    .window = primaryWindow_,
                    .preeditUtf8 = "cd",
                    .cursorCodepoint = 2,
                    .stage = Platform::TextCompositionStage::Updated,
                }) != Platform::FrameBatchAppendResult::Appended)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "oversized text byte batch could not be constructed");
            }
            break;
        case OversizedPlatformFrameKind::PlatformEventBatch: {
            const std::vector gamepads{
                Platform::GamepadSnapshot{
                    .gamepad = gamepad_,
                    .revision = metricsRevision,
                },
            };
            if (!frameBuilder_.setGamepadSnapshots(gamepads))
            {
                return Core::failure(Core::CoreErrorCode::Internal, "oversized backend Gamepad snapshot was rejected");
            }
            if (frameBuilder_.appendPlatformEvent(Platform::WindowMetricsChangedEvent{
                    .window = primaryWindow_,
                    .metricsRevision = metricsRevision,
                }) != Platform::FrameBatchAppendResult::Appended ||
                frameBuilder_.appendPlatformEvent(Platform::GamepadConnectedEvent{.gamepad = gamepad_}) !=
                    Platform::FrameBatchAppendResult::Appended)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "oversized Platform event batch could not be constructed");
            }
            break;
        }
        }

        auto frame = frameBuilder_.finishFrame();
        if (!frame)
        {
            return Core::failure(std::move(frame.error()));
        }
        return Platform::PlatformPollResult::Continue(*frame);
    }

    void shutdown() noexcept override
    {
        if (!stopped_)
        {
            probe_->events.emplace_back("platform.shutdown");
            stopped_ = true;
        }
    }

  private:
    RuntimeProbe* probe_;
    Platform::PlatformFrameBuilder frameBuilder_;
    std::unique_ptr<RuntimeWindowPool> windowPool_;
    std::unique_ptr<RuntimeGamepadPool> gamepadPool_;
    Platform::WindowId primaryWindow_{};
    Platform::GamepadId gamepad_{};
    OversizedPlatformFrameKind frameKind_;
    bool stopped_ = false;
};

class ProbeTaskSystem final : public Task::ITaskSystem {
  public:
    explicit ProbeTaskSystem(RuntimeProbe& probe) noexcept : probe_(&probe)
    {
    }

    ~ProbeTaskSystem() override
    {
        probe_->events.emplace_back("task.destroy");
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        return true;
    }

    void shutdownAndJoin() noexcept override
    {
        if (!stopped_)
        {
            probe_->events.emplace_back("task.shutdown");
            stopped_ = true;
        }
    }

  private:
    RuntimeProbe* probe_;
    bool stopped_ = false;
};

class ProbeRenderDevice final : public Render::IRenderDevice {
  public:
    explicit ProbeRenderDevice(RuntimeProbe& probe) noexcept : probe_(&probe)
    {
    }

    ~ProbeRenderDevice() override
    {
        probe_->events.emplace_back("render.destroy");
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame& frame) override
    {
        probe_->events.emplace_back("render.submit." + std::to_string(frame.frameIndex));
        probe_->lastSubmittedHadPrimaryWindowSurface = frame.primaryWindowSurface.has_value();
        ++probe_->submitCalls;
        if (probe_->failurePoint == CommittedFailurePoint::RenderSubmit)
        {
            if (probe_->failureOutcome == InjectedOutcome::Throw)
            {
                throw std::runtime_error("render submit exception");
            }
            return Core::failure(Core::CoreErrorCode::Internal, "render submit failure");
        }
        const u64 submissionIndex = probe_->submittedFrames;
        ++probe_->submittedFrames;
        return Render::RenderFrameSubmission::Submitted(submissionIndex);
    }

    [[nodiscard]] Core::Status present() override
    {
        probe_->events.emplace_back("render.present");
        ++probe_->presentCalls;
        if (probe_->failurePoint == CommittedFailurePoint::RenderPresent)
        {
            if (probe_->failureOutcome == InjectedOutcome::Throw)
            {
                throw std::runtime_error("render present exception");
            }
            return Core::failure(Core::CoreErrorCode::Internal, "render present failure");
        }
        ++probe_->presentedFrames;
        return Core::success();
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return Render::RenderStatistics{
            .submitted = probe_->submittedFrames,
            .presented = probe_->presentedFrames,
            .liveResources = 0,
        };
    }

    void shutdown() noexcept override
    {
        if (!stopped_)
        {
            probe_->events.emplace_back("render.shutdown");
            stopped_ = true;
        }
    }

  private:
    RuntimeProbe* probe_;
    bool stopped_ = false;
};

EngineCompositionFactories makeRuntimeFactories(RuntimeProbe& probe)
{
    EngineCompositionFactories factories;
    auto& platformRender = std::get<IndependentPlatformRenderFactories>(factories.platformRender);
    factories.createMonotonicClock = [&probe]() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        auto clock = std::make_unique<ManualMonotonicClock>();
        probe.clock = clock.get();
        std::unique_ptr<Core::IMonotonicClock> result = std::move(clock);
        return result;
    };
    platformRender.createPlatformBackend = [&probe](const Platform::PlatformBackendCreateParams& params)
        -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        auto frameBuilder = Platform::PlatformFrameBuilder::Create(params.frameCapacities);
        if (!frameBuilder)
        {
            return Core::failure(std::move(frameBuilder.error()));
        }
        auto windowPoolResult = RuntimeWindowPool::Create(1);
        if (!windowPoolResult)
        {
            return Core::failure(std::move(windowPoolResult.error()));
        }
        auto windowPool = std::make_unique<RuntimeWindowPool>(std::move(*windowPoolResult));
        auto windowResult = windowPool->tryEmplace(0);
        if (!windowResult)
        {
            return Core::failure(std::move(windowResult.error()));
        }
        std::unique_ptr<Platform::IPlatformBackend> platform =
            std::make_unique<AdvancingPlatform>(probe, std::move(*frameBuilder), std::move(windowPool), *windowResult);
        return platform;
    };
    factories.createTaskSystem =
        [&probe](const Task::TaskSystemCreateParams&) -> Core::Result<std::unique_ptr<Task::ITaskSystem>> {
        std::unique_ptr<Task::ITaskSystem> taskSystem = std::make_unique<ProbeTaskSystem>(probe);
        return taskSystem;
    };
    platformRender.createRenderDevice =
        [&probe](const Render::RenderDeviceCreateParams&) -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
        std::unique_ptr<Render::IRenderDevice> renderDevice = std::make_unique<ProbeRenderDevice>(probe);
        return renderDevice;
    };
    return factories;
}

EngineCompositionFactories makeOversizedPlatformFrameFactories(RuntimeProbe& probe,
                                                               OversizedPlatformFrameKind frameKind,
                                                               Platform::PlatformFrameCapacityConfig builderCapacities)
{
    EngineCompositionFactories factories = makeRuntimeFactories(probe);
    auto& platformRender = std::get<IndependentPlatformRenderFactories>(factories.platformRender);
    platformRender.createPlatformBackend =
        [&probe, frameKind, builderCapacities](
            const Platform::PlatformBackendCreateParams&) -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        auto frameBuilder = Platform::PlatformFrameBuilder::Create(builderCapacities);
        if (!frameBuilder)
        {
            return Core::failure(std::move(frameBuilder.error()));
        }

        auto windowPoolResult = RuntimeWindowPool::Create(1);
        if (!windowPoolResult)
        {
            return Core::failure(std::move(windowPoolResult.error()));
        }
        auto windowPool = std::make_unique<RuntimeWindowPool>(std::move(*windowPoolResult));
        auto windowResult = windowPool->tryEmplace(0);
        if (!windowResult)
        {
            return Core::failure(std::move(windowResult.error()));
        }

        auto gamepadPoolResult = RuntimeGamepadPool::Create(1);
        if (!gamepadPoolResult)
        {
            return Core::failure(std::move(gamepadPoolResult.error()));
        }
        auto gamepadPool = std::make_unique<RuntimeGamepadPool>(std::move(*gamepadPoolResult));
        auto gamepadResult = gamepadPool->tryEmplace(0);
        if (!gamepadResult)
        {
            return Core::failure(std::move(gamepadResult.error()));
        }

        std::unique_ptr<Platform::IPlatformBackend> platform = std::make_unique<OversizedPlatformFrameBackend>(
            probe, std::move(*frameBuilder), std::move(windowPool), std::move(gamepadPool), *windowResult,
            *gamepadResult, frameKind);
        return platform;
    };
    return factories;
}

struct OversizedPlatformFrameCase final {
    OversizedPlatformFrameKind frameKind;
    std::string_view testName;
    std::string_view expectedMessage;
    std::string_view expectedBatchDetail;
};

[[nodiscard]] EngineConfig undersizedPlatformFrameConfig(OversizedPlatformFrameKind frameKind)
{
    auto config = EngineConfig::Defaults();
    switch (frameKind)
    {
    case OversizedPlatformFrameKind::RawInputBatch:
        config.platformFrameCapacities.inputTransitionCapacity = 1;
        break;
    case OversizedPlatformFrameKind::TextByteBatch:
        config.platformFrameCapacities.inputTransitionCapacity = 2;
        config.platformFrameCapacities.inputTextByteCapacity = 3;
        break;
    case OversizedPlatformFrameKind::PlatformEventBatch:
        config.platformFrameCapacities.platformEventCapacity = 1;
        break;
    }
    return config;
}

[[nodiscard]] Platform::PlatformFrameCapacityConfig oversizedBuilderCapacities(const EngineConfig& config)
{
    auto capacities = config.platformFrameCapacities;
    ++capacities.inputTransitionCapacity;
    ++capacities.inputTextByteCapacity;
    ++capacities.platformEventCapacity;
    return capacities;
}

enum class StartupMode {
    Normal,
    ReturnNull,
    ReturnError,
    Throw,
};

enum class EnterMode {
    Normal,
    ReturnError,
    Throw,
};

struct FixedObservation final {
    u64 frameIndex = 0;
    u64 simulationTickIndex = 0;
    u32 fixedStepIndexInFrame = 0;
    u32 fixedStepCountInFrame = 0;
};

struct ActionWiringProbe final {
    InputActionId simulationAction{};
    InputActionId frameAction{};
    bool enabled = false;
    bool fixedObserved = false;
    bool frameObserved = false;
};

struct GameProbe final {
    RuntimeProbe* runtime = nullptr;
    StartupMode startupMode = StartupMode::Normal;
    EnterMode enterMode = EnterMode::Normal;
    Core::Duration advanceClockDuringEnter{};
    u64 exitOnFrame = 0;
    bool subscribeToPlatformEvents = false;
    bool throwFromPlatformEvent = false;
    u32 platformEventCount = 0;
    std::optional<PlatformEventSubscription> platformEventSubscription;
    std::vector<FixedObservation> fixedObservations;
    std::vector<u32> fixedCountsByFrame;
    u32 exitCount = 0;
    u32 shutdownCount = 0;
    std::optional<RunStopCause> exitStopCause;
    std::optional<RunStopCause> shutdownStopCause;
    std::optional<Core::ErrorCode> exitFailureCode;
    std::optional<Core::ErrorCode> shutdownFailureCode;
    ActionWiringProbe actionWiring;
};

[[nodiscard]] Core::Status injectedGameFailure(std::string_view phase)
{
    return Core::failure(Core::CoreErrorCode::Internal, phase);
}

[[nodiscard]] Core::Status maybeInjectCommittedGameFailure(RuntimeProbe& runtime, CommittedFailurePoint currentPoint,
                                                           std::string_view phase)
{
    if (runtime.failurePoint != currentPoint)
    {
        return Core::success();
    }
    if (runtime.failureOutcome == InjectedOutcome::Throw)
    {
        throw std::runtime_error(std::string(phase) + " exception");
    }
    return injectedGameFailure(phase);
}

class ScriptedGameState final : public IGameState {
  public:
    explicit ScriptedGameState(GameProbe& probe) noexcept : probe_(&probe)
    {
    }

    ~ScriptedGameState() noexcept override
    {
        probe_->runtime->events.emplace_back("state.destroy");
    }

    Core::Status onEnter(GameStateEnterContext& context) override
    {
        probe_->runtime->events.emplace_back("state.enter");
        EXPECT_FALSE(context.engineConfig().applicationName.empty());
        if (probe_->subscribeToPlatformEvents)
        {
            auto subscription =
                context.platformEventSubscriptions().subscribe([probe = probe_](const PlatformEventNotification&) {
                    probe->runtime->events.emplace_back("platform.event");
                    ++probe->platformEventCount;
                    if (probe->throwFromPlatformEvent)
                    {
                        throw std::runtime_error("platform event callback exception");
                    }
                });
            if (!subscription)
            {
                return Core::failure(std::move(subscription.error()));
            }
            probe_->platformEventSubscription.emplace(std::move(*subscription));
        }
        probe_->runtime->clock->advance(probe_->advanceClockDuringEnter);
        switch (probe_->enterMode)
        {
        case EnterMode::Normal:
            return Core::success();
        case EnterMode::ReturnError:
            return injectedGameFailure("onEnter");
        case EnterMode::Throw:
            throw std::runtime_error("onEnter exception");
        }
        return injectedGameFailure("unreachable enter mode");
    }

    void onExit(GameStateExitContext& context) noexcept override
    {
        probe_->runtime->events.emplace_back("state.exit");
        ++probe_->exitCount;
        probe_->exitStopCause = context.stopCause();
        if (context.runtimeFailure() != nullptr)
        {
            probe_->exitFailureCode = context.runtimeFailure()->code;
        }
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        probe_->runtime->events.emplace_back("state.policy");
        return {};
    }

    Core::Status fixedUpdate(FixedUpdateContext& context) override
    {
        const auto& frameTiming = context.frameTiming();
        const auto& fixedTiming = context.fixedUpdateTiming();
        probe_->runtime->events.emplace_back("state.fixed." + std::to_string(frameTiming.frameIndex) + "." +
                                             std::to_string(fixedTiming.fixedStepIndexInFrame));
        probe_->fixedObservations.push_back(FixedObservation{
            .frameIndex = frameTiming.frameIndex,
            .simulationTickIndex = fixedTiming.simulationTickIndex,
            .fixedStepIndexInFrame = fixedTiming.fixedStepIndexInFrame,
            .fixedStepCountInFrame = fixedTiming.fixedStepCountInFrame,
        });
        if (probe_->actionWiring.enabled)
        {
            const SimulationActionSnapshot& actions = context.simulationActions();
            EXPECT_EQ(actions.targetSimulationTick, fixedTiming.simulationTickIndex);
            EXPECT_TRUE(actions.isHeld(probe_->actionWiring.simulationAction));
            EXPECT_EQ(actions.find(probe_->actionWiring.frameAction), nullptr);
            const usize expectedTransitionCount = fixedTiming.fixedStepIndexInFrame == 0U ? 1U : 0U;
            EXPECT_EQ(actions.transitions.size(), expectedTransitionCount);
            if (fixedTiming.fixedStepIndexInFrame == 0U && actions.transitions.size() == 1U)
            {
                const auto* digital = std::get_if<DigitalActionTransition>(&actions.transitions.front());
                EXPECT_NE(digital, nullptr);
                if (digital != nullptr)
                {
                    EXPECT_EQ(digital->action, probe_->actionWiring.simulationAction);
                    EXPECT_EQ(digital->kind, DigitalActionTransitionKind::Pressed);
                    EXPECT_EQ(digital->sourceSequence, 1U);
                }
            }
            probe_->actionWiring.fixedObserved = true;
        }
        return maybeInjectCommittedGameFailure(*probe_->runtime, CommittedFailurePoint::FixedUpdate, "fixedUpdate");
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        const u64 frameIndex = context.frameTiming().frameIndex;
        probe_->runtime->events.emplace_back("state.update." + std::to_string(frameIndex));
        probe_->fixedCountsByFrame.push_back(context.frameTiming().fixedStepCount);
        if (probe_->actionWiring.enabled)
        {
            const FrameActionSnapshot& actions = context.frameActions();
            EXPECT_EQ(actions.engineFrameIndex, frameIndex);
            EXPECT_TRUE(actions.isHeld(probe_->actionWiring.frameAction));
            EXPECT_EQ(actions.find(probe_->actionWiring.simulationAction), nullptr);
            EXPECT_EQ(actions.transitions.size(), 1U);
            if (actions.transitions.size() == 1U)
            {
                const auto* digital = std::get_if<DigitalActionTransition>(&actions.transitions.front());
                EXPECT_NE(digital, nullptr);
                if (digital != nullptr)
                {
                    EXPECT_EQ(digital->action, probe_->actionWiring.frameAction);
                    EXPECT_EQ(digital->kind, DigitalActionTransitionKind::Pressed);
                    EXPECT_EQ(digital->sourceSequence, 2U);
                }
            }
            probe_->actionWiring.frameObserved = true;
        }
        if (frameIndex == probe_->exitOnFrame)
        {
            context.requestExitAfterFrame();
        }
        return maybeInjectCommittedGameFailure(*probe_->runtime, CommittedFailurePoint::UpdateFrame, "updateFrame");
    }

    Core::Status extractRenderScene(RenderSceneExtractionContext& context) const override
    {
        probe_->runtime->events.emplace_back("state.extract." + std::to_string(context.frameTiming().frameIndex));
        return maybeInjectCommittedGameFailure(*probe_->runtime, CommittedFailurePoint::ExtractRenderScene,
                                               "extractRenderScene");
    }

    Core::Status updateUI(UIUpdateContext& context) override
    {
        probe_->runtime->events.emplace_back("state.ui." + std::to_string(context.frameTiming().frameIndex));
        return maybeInjectCommittedGameFailure(*probe_->runtime, CommittedFailurePoint::UpdateUI, "updateUI");
    }

  private:
    GameProbe* probe_;
};

class ScriptedGameApplication final : public IGameApplication {
  public:
    explicit ScriptedGameApplication(GameProbe& probe) noexcept : probe_(&probe)
    {
    }

    Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext& context) override
    {
        probe_->runtime->events.emplace_back("game.create");
        EXPECT_FALSE(context.engineConfig().applicationName.empty());
        switch (probe_->startupMode)
        {
        case StartupMode::Normal: {
            std::unique_ptr<IGameState> state = std::make_unique<ScriptedGameState>(*probe_);
            return state;
        }
        case StartupMode::ReturnNull:
            return std::unique_ptr<IGameState>{};
        case StartupMode::ReturnError:
            return Core::failure(Core::CoreErrorCode::Internal, "createInitialState");
        case StartupMode::Throw:
            throw std::runtime_error("createInitialState 中文异常");
        }
        return Core::failure(Core::CoreErrorCode::Internal, "unreachable startup mode");
    }

    void onShutdown(GameShutdownContext& context) noexcept override
    {
        probe_->runtime->events.emplace_back("game.shutdown");
        ++probe_->shutdownCount;
        probe_->shutdownStopCause = context.stopCause();
        if (context.runtimeFailure() != nullptr)
        {
            probe_->shutdownFailureCode = context.runtimeFailure()->code;
        }
    }

  private:
    GameProbe* probe_;
};

Core::Result<std::unique_ptr<EngineHost>> createRuntimeHost(RuntimeProbe& probe,
                                                            EngineConfig config = EngineConfig::Defaults())
{
    return EngineHost::Create(std::move(config), makeRuntimeFactories(probe));
}

} // namespace

TEST(EngineConfigTest, DefaultsAreValidAndUseSixtyHertzWithFourCatchUpSteps)
{
    const EngineConfig config = EngineConfig::Defaults();
    EXPECT_TRUE(config.validate().has_value());
    EXPECT_EQ(config.applicationName, "Tina");
    EXPECT_EQ(config.primaryWindowUICapacities.nodeCapacity, UI::UIContextCapacityConfig::DefaultNodeCapacity);
    EXPECT_EQ(config.primaryWindowUICapacities.rootCapacity, UI::UIContextCapacityConfig::DefaultRootCapacity);
    EXPECT_DOUBLE_EQ(config.fixedSimulation.fixedDelta.count(), 1.0 / 60.0);
    EXPECT_EQ(config.fixedSimulation.maximumStepsPerFrame, 4U);
    EXPECT_DOUBLE_EQ(config.gameplayTimeScale, 1.0);
}

TEST(EngineConfigTest, RejectsEveryInvalidPrimaryWindowUICapacityCombination)
{
    std::vector<UI::UIContextCapacityConfig> invalidCapacities = {
        {.nodeCapacity = 0, .rootCapacity = 1},
        {.nodeCapacity = 1, .rootCapacity = 0},
        {.nodeCapacity = 1, .rootCapacity = 2},
        {.nodeCapacity = UI::UIContextCapacityConfig::MaxNodeCapacity + 1, .rootCapacity = 1},
        {.nodeCapacity = UI::UIContextCapacityConfig::MaxNodeCapacity,
         .rootCapacity = UI::UIContextCapacityConfig::MaxRootCapacity + 1},
        {.nodeCapacity = 4, .rootCapacity = 1, .dirtyQueueCapacity = 5},
        {.nodeCapacity = 4, .rootCapacity = 1, .layoutSnapshotCapacity = 5},
        {.nodeCapacity = 4, .rootCapacity = 1, .hitSnapshotCapacity = 5},
        {.nodeCapacity = 4, .rootCapacity = 1, .routePathCapacity = 5},
        {.nodeCapacity = 4,
         .rootCapacity = 1,
         .routedPointerListenerCapacity = UI::UIContextCapacityConfig::MaxRoutedPointerListenerCapacity + 1},
    };

    for (const UI::UIContextCapacityConfig& capacities : invalidCapacities)
    {
        auto config = EngineConfig::Defaults();
        config.primaryWindowUICapacities = capacities;
        const Core::Status result = config.validate();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    }
}

TEST(EngineConfigTest, RejectsInvalidTextTimingAndScale)
{
    std::vector<EngineConfig> invalidConfigs;

    auto emptyName = EngineConfig::Defaults();
    emptyName.applicationName.clear();
    invalidConfigs.push_back(std::move(emptyName));

    auto embeddedNul = EngineConfig::Defaults();
    embeddedNul.applicationName = std::string("Tina\0Engine", 11);
    invalidConfigs.push_back(std::move(embeddedNul));

    auto invalidUtf8 = EngineConfig::Defaults();
    invalidUtf8.applicationName = std::string("\xC0\xAF", 2);
    invalidConfigs.push_back(std::move(invalidUtf8));

    auto negativeScale = EngineConfig::Defaults();
    negativeScale.gameplayTimeScale = -1.0;
    invalidConfigs.push_back(std::move(negativeScale));

    auto infiniteScale = EngineConfig::Defaults();
    infiniteScale.gameplayTimeScale = (std::numeric_limits<double>::infinity)();
    invalidConfigs.push_back(std::move(infiniteScale));

    auto zeroShutdownDeadline = EngineConfig::Defaults();
    zeroShutdownDeadline.shutdownDeadline = Core::Duration::zero();
    invalidConfigs.push_back(std::move(zeroShutdownDeadline));

    auto invalidFixedStep = EngineConfig::Defaults();
    invalidFixedStep.fixedSimulation.maximumStepsPerFrame = 0;
    invalidConfigs.push_back(std::move(invalidFixedStep));

    auto excessiveFixedSteps = EngineConfig::Defaults();
    excessiveFixedSteps.fixedSimulation.maximumStepsPerFrame = EngineConfig::MaximumFixedStepsPerFrame + 1;
    invalidConfigs.push_back(std::move(excessiveFixedSteps));

    for (const EngineConfig& config : invalidConfigs)
    {
        auto result = config.validate();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    }
}

TEST(EngineConfigTest, RejectsInvalidWindowInputAndPlatformEventConfiguration)
{
    std::vector<EngineConfig> invalidConfigs;

    auto emptyTitle = EngineConfig::Defaults();
    emptyTitle.primaryWindow.title.clear();
    invalidConfigs.push_back(std::move(emptyTitle));

    auto invalidTitleUtf8 = EngineConfig::Defaults();
    invalidTitleUtf8.primaryWindow.title = std::string("\xED\xA0\x80", 3);
    invalidConfigs.push_back(std::move(invalidTitleUtf8));

    auto zeroWindowWidth = EngineConfig::Defaults();
    zeroWindowWidth.primaryWindow.initialLogicalExtent.width = 0;
    invalidConfigs.push_back(std::move(zeroWindowWidth));

    auto invalidWindowMode = EngineConfig::Defaults();
    invalidWindowMode.primaryWindow.mode = static_cast<Platform::WindowMode>(255);
    invalidConfigs.push_back(std::move(invalidWindowMode));

    auto noEventSubscribers = EngineConfig::Defaults();
    noEventSubscribers.platformEventSubscriptions.subscriberCapacity = 0;
    invalidConfigs.push_back(std::move(noEventSubscribers));

    auto tooManyEventSubscribers = EngineConfig::Defaults();
    tooManyEventSubscribers.platformEventSubscriptions.subscriberCapacity =
        PlatformEventSubscriptionConfig::MaximumSubscriberCapacity + 1;
    invalidConfigs.push_back(std::move(tooManyEventSubscribers));

    auto noRawTransitions = EngineConfig::Defaults();
    noRawTransitions.platformFrameCapacities.inputTransitionCapacity = 0;
    invalidConfigs.push_back(std::move(noRawTransitions));

    auto tooManyPlatformEvents = EngineConfig::Defaults();
    tooManyPlatformEvents.platformFrameCapacities.platformEventCapacity =
        Platform::PlatformFrameCapacityConfig::MaximumPlatformEventCapacity + 1;
    invalidConfigs.push_back(std::move(tooManyPlatformEvents));

    auto noSimulationEdges = EngineConfig::Defaults();
    noSimulationEdges.inputActions.capacities.simulationActionTransitionCapacity = 0;
    invalidConfigs.push_back(std::move(noSimulationEdges));

    auto noFrameEdges = EngineConfig::Defaults();
    noFrameEdges.inputActions.capacities.frameActionTransitionCapacity = 0;
    invalidConfigs.push_back(std::move(noFrameEdges));

    auto noBindings = EngineConfig::Defaults();
    noBindings.inputActions.capacities.digitalActionBindingCapacity = 0;
    invalidConfigs.push_back(std::move(noBindings));

    for (const EngineConfig& config : invalidConfigs)
    {
        auto result = config.validate();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    }
}

TEST(EngineConfigTest, RejectsInvalidDuplicateAndExcessDigitalBindings)
{
    constexpr InputActionId JumpAction{1};

    auto invalidAction = EngineConfig::Defaults();
    invalidAction.inputActions.digitalBindings.push_back(DigitalActionBinding{
        .input = PrimaryWindowKeyBinding{.key = Platform::Key::Space},
    });
    EXPECT_FALSE(invalidAction.validate().has_value());

    auto invalidKey = EngineConfig::Defaults();
    invalidKey.inputActions.digitalBindings.push_back(DigitalActionBinding{
        .input = PrimaryWindowKeyBinding{.key = Platform::Key::Unknown},
        .action = JumpAction,
    });
    EXPECT_FALSE(invalidKey.validate().has_value());

    auto unsupportedPointer = EngineConfig::Defaults();
    unsupportedPointer.inputActions.digitalBindings.push_back(DigitalActionBinding{
        .input =
            PrimaryPointerButtonBinding{
                .pointer = Platform::PrimaryPointerId + 1,
                .button = Platform::PointerButton::Primary,
            },
        .action = JumpAction,
    });
    EXPECT_FALSE(unsupportedPointer.validate().has_value());

    auto invalidDomain = EngineConfig::Defaults();
    invalidDomain.inputActions.digitalBindings.push_back(DigitalActionBinding{
        .input = PrimaryWindowKeyBinding{.key = Platform::Key::Space},
        .action = JumpAction,
        .domain = static_cast<InputActionDomain>(255),
    });
    EXPECT_FALSE(invalidDomain.validate().has_value());

    auto duplicateControl = EngineConfig::Defaults();
    duplicateControl.inputActions.digitalBindings = {
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::Space},
            .action = JumpAction,
            .domain = InputActionDomain::Simulation,
        },
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::Space},
            .action = InputActionId{2},
            .domain = InputActionDomain::Frame,
        },
    };
    EXPECT_FALSE(duplicateControl.validate().has_value());

    auto actionInTwoDomains = EngineConfig::Defaults();
    actionInTwoDomains.inputActions.digitalBindings = {
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::Space},
            .action = JumpAction,
            .domain = InputActionDomain::Simulation,
        },
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::Enter},
            .action = JumpAction,
            .domain = InputActionDomain::Frame,
        },
    };
    EXPECT_FALSE(actionInTwoDomains.validate().has_value());

    auto beyondConfiguredCapacity = EngineConfig::Defaults();
    beyondConfiguredCapacity.inputActions.capacities.digitalActionBindingCapacity = 1;
    beyondConfiguredCapacity.inputActions.digitalBindings = {
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::Space},
            .action = JumpAction,
        },
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::Enter},
            .action = InputActionId{2},
        },
    };
    EXPECT_FALSE(beyondConfiguredCapacity.validate().has_value());
}

TEST(EngineHostCreationTest, InvalidConfigIsRejectedBeforeAnyFactoryInvocation)
{
    EventLog events;
    auto config = EngineConfig::Defaults();
    config.applicationName.clear();

    auto result =
        EngineHost::Create(std::move(config), makeInjectedFactories(events, FactoryStage::Clock, FactoryMode::Failure));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    EXPECT_TRUE(events.empty());
}

TEST(EngineHostCreationTest, InvalidPrimaryWindowUICapacityIsRejectedBeforeAnyFactoryInvocation)
{
    EventLog events;
    auto config = EngineConfig::Defaults();
    config.primaryWindowUICapacities.layoutSnapshotCapacity = config.primaryWindowUICapacities.nodeCapacity + 1;

    auto result = EngineHost::Create(config, makeInjectedFactories(events, FactoryStage::Clock, FactoryMode::Failure));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    EXPECT_TRUE(events.empty());
}

TEST(EngineHostCreationTest, DestroyingReadyHostWithoutRunShutsModulesDownInReverseOrder)
{
    EventLog events;
    EngineCompositionFactories factories;
    auto& platformRender = std::get<IndependentPlatformRenderFactories>(factories.platformRender);
    factories.createMonotonicClock = [&events]() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        events.emplace_back("factory.clock");
        std::unique_ptr<Core::IMonotonicClock> clock = std::make_unique<LoggingClock>(events);
        return clock;
    };
    platformRender.createPlatformBackend =
        [&events](
            const Platform::PlatformBackendCreateParams&) -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        events.emplace_back("factory.platform");
        std::unique_ptr<Platform::IPlatformBackend> platform = std::make_unique<LoggingPlatform>(events);
        return platform;
    };
    factories.createTaskSystem =
        [&events](const Task::TaskSystemCreateParams&) -> Core::Result<std::unique_ptr<Task::ITaskSystem>> {
        events.emplace_back("factory.task");
        std::unique_ptr<Task::ITaskSystem> taskSystem = std::make_unique<LoggingTaskSystem>(events);
        return taskSystem;
    };
    platformRender.createRenderDevice =
        [&events](const Render::RenderDeviceCreateParams&) -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
        events.emplace_back("factory.render");
        std::unique_ptr<Render::IRenderDevice> renderDevice = std::make_unique<LoggingRenderDevice>(events);
        return renderDevice;
    };

    auto hostResult = EngineHost::Create(EngineConfig::Defaults(), std::move(factories));
    ASSERT_TRUE(hostResult.has_value());
    ASSERT_NE(*hostResult, nullptr);
    hostResult->reset();

    EXPECT_EQ(events, EventLog({
                          "factory.clock",
                          "factory.platform",
                          "factory.task",
                          "factory.render",
                          "render.shutdown",
                          "render.destroy",
                          "task.shutdown",
                          "task.destroy",
                          "platform.shutdown",
                          "platform.destroy",
                          "clock.destroy",
                      }));
}

TEST(EngineHostThreadAffinityTest, RejectsRunOnAnotherThreadWithoutConsumingTheHost)
{
    RuntimeProbe runtime;
    runtime.platformExitRequested = true;
    auto host = createRuntimeHost(runtime);
    ASSERT_TRUE(host.has_value()) << host.error().message;
    GameProbe gameProbe;
    gameProbe.runtime = &runtime;
    ScriptedGameApplication application{gameProbe};
    std::optional<Core::Result<RunExitReason>> crossThreadResult;

    std::thread worker([&] { crossThreadResult.emplace((*host)->run(application)); });
    worker.join();

    ASSERT_TRUE(crossThreadResult.has_value());
    ASSERT_FALSE(crossThreadResult->has_value());
    EXPECT_EQ(crossThreadResult->error().code, RuntimeErrorCode::WrongOwnerThread);

    auto ownerThreadRun = (*host)->run(application);
    ASSERT_TRUE(ownerThreadRun.has_value()) << ownerThreadRun.error().message;
    EXPECT_EQ(*ownerThreadRun, RunExitReason::PrimaryWindowRequestedClose);
}

struct StartupFailureCase final {
    StartupMode startupMode;
    EnterMode enterMode;
    Core::ErrorCode expectedCode;
};

class RuntimeStartupFailureTest : public testing::TestWithParam<StartupFailureCase> {};

TEST_P(RuntimeStartupFailureTest, RollsBackWithoutExitOrApplicationShutdown)
{
    RuntimeProbe runtime;
    GameProbe game;
    game.runtime = &runtime;
    game.startupMode = GetParam().startupMode;
    game.enterMode = GetParam().enterMode;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, GetParam().expectedCode);
    if (GetParam().startupMode == StartupMode::Throw)
    {
        EXPECT_TRUE(std::ranges::any_of(runResult.error().context, [](const Core::ErrorContext& context) {
            return context.detail == "createInitialState 中文异常";
        }));
    }
    EXPECT_EQ(game.exitCount, 0U);
    EXPECT_EQ(game.shutdownCount, 0U);
    EXPECT_FALSE(containsEvent(runtime.events, "state.exit"));
    EXPECT_FALSE(containsEvent(runtime.events, "game.shutdown"));
    EXPECT_EQ(runtime.submittedFrames, 0U);
    EXPECT_EQ(runtime.presentedFrames, 0U);
    EXPECT_TRUE(containsEvent(runtime.events, "render.shutdown"));
    EXPECT_TRUE(containsEvent(runtime.events, "task.shutdown"));
    EXPECT_TRUE(containsEvent(runtime.events, "platform.shutdown"));
}

INSTANTIATE_TEST_SUITE_P(TransactionalStartup, RuntimeStartupFailureTest,
                         testing::Values(
                             StartupFailureCase{
                                 StartupMode::ReturnNull,
                                 EnterMode::Normal,
                                 RuntimeErrorCode::InitialGameStateWasNull,
                             },
                             StartupFailureCase{
                                 StartupMode::ReturnError,
                                 EnterMode::Normal,
                                 Core::CoreErrorCode::Internal,
                             },
                             StartupFailureCase{
                                 StartupMode::Throw,
                                 EnterMode::Normal,
                                 RuntimeErrorCode::GameCallbackThrewException,
                             },
                             StartupFailureCase{
                                 StartupMode::Normal,
                                 EnterMode::ReturnError,
                                 Core::CoreErrorCode::Internal,
                             },
                             StartupFailureCase{
                                 StartupMode::Normal,
                                 EnterMode::Throw,
                                 RuntimeErrorCode::GameCallbackThrewException,
                             }));

TEST(EngineHostRunTest, ExitRequestStillCompletesExtractionUiRenderAndPresent)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::GameRequestedExitAfterCurrentFrame);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::GameRequestedExitAfterCurrentFrame);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::GameRequestedExitAfterCurrentFrame);
    EXPECT_FALSE(game.exitFailureCode.has_value());
    EXPECT_FALSE(game.shutdownFailureCode.has_value());
    EXPECT_EQ(runtime.submittedFrames, 1U);
    EXPECT_EQ(runtime.presentedFrames, 1U);
    EXPECT_EQ(runtime.events, EventLog({
                                  "game.create",
                                  "state.enter",
                                  "state.policy",
                                  "platform.poll.0",
                                  "state.update.0",
                                  "state.extract.0",
                                  "state.ui.0",
                                  "render.submit.0",
                                  "render.present",
                                  "state.exit",
                                  "state.destroy",
                                  "game.shutdown",
                                  "render.shutdown",
                                  "render.destroy",
                                  "task.shutdown",
                                  "task.destroy",
                                  "platform.shutdown",
                                  "platform.destroy",
                              }));
}

TEST(EngineHostRunTest, LaterUiFailureWinsOverExitRequestFromSameFrame)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.failurePoint = CommittedFailurePoint::UpdateUI;
    runtime.failureOutcome = InjectedOutcome::ReturnError;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, Core::CoreErrorCode::Internal);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.exitFailureCode, Core::CoreErrorCode::Internal);
    EXPECT_EQ(game.shutdownFailureCode, Core::CoreErrorCode::Internal);
    EXPECT_EQ(runtime.submittedFrames, 0U);
    EXPECT_EQ(runtime.presentedFrames, 0U);
    EXPECT_TRUE(containsEvent(runtime.events, "state.extract.0"));
    EXPECT_TRUE(containsEvent(runtime.events, "state.ui.0"));
    EXPECT_FALSE(containsEvent(runtime.events, "render.submit.0"));
}

namespace {

using CommittedFailureCase = std::tuple<CommittedFailurePoint, InjectedOutcome>;

[[nodiscard]] Core::ErrorCode expectedFailureCode(CommittedFailurePoint failurePoint, InjectedOutcome outcome) noexcept
{
    if (outcome == InjectedOutcome::ReturnError)
    {
        return Core::CoreErrorCode::Internal;
    }

    switch (failurePoint)
    {
    case CommittedFailurePoint::FixedUpdate:
    case CommittedFailurePoint::UpdateFrame:
    case CommittedFailurePoint::ExtractRenderScene:
    case CommittedFailurePoint::UpdateUI:
        return RuntimeErrorCode::GameCallbackThrewException;
    case CommittedFailurePoint::PlatformPoll:
    case CommittedFailurePoint::RenderSubmit:
    case CommittedFailurePoint::RenderPresent:
        return RuntimeErrorCode::LifecycleInvariantViolation;
    case CommittedFailurePoint::None:
        break;
    }
    return Core::CoreErrorCode::Internal;
}

[[nodiscard]] std::string_view invocationEvent(CommittedFailurePoint failurePoint) noexcept
{
    switch (failurePoint)
    {
    case CommittedFailurePoint::PlatformPoll:
        return "platform.poll.0";
    case CommittedFailurePoint::FixedUpdate:
        return "state.fixed.0.0";
    case CommittedFailurePoint::UpdateFrame:
        return "state.update.0";
    case CommittedFailurePoint::ExtractRenderScene:
        return "state.extract.0";
    case CommittedFailurePoint::UpdateUI:
        return "state.ui.0";
    case CommittedFailurePoint::RenderSubmit:
        return "render.submit.0";
    case CommittedFailurePoint::RenderPresent:
        return "render.present";
    case CommittedFailurePoint::None:
        break;
    }
    return {};
}

[[nodiscard]] std::string_view boundaryOperation(CommittedFailurePoint failurePoint) noexcept
{
    switch (failurePoint)
    {
    case CommittedFailurePoint::PlatformPoll:
        return "IPlatformBackend::pollFrame";
    case CommittedFailurePoint::FixedUpdate:
        return "IGameState::fixedUpdate";
    case CommittedFailurePoint::UpdateFrame:
        return "IGameState::updateFrame";
    case CommittedFailurePoint::ExtractRenderScene:
        return "IGameState::extractRenderScene";
    case CommittedFailurePoint::UpdateUI:
        return "IGameState::updateUI";
    case CommittedFailurePoint::RenderSubmit:
        return "IRenderDevice::submitFrame";
    case CommittedFailurePoint::RenderPresent:
        return "IRenderDevice::present";
    case CommittedFailurePoint::None:
        break;
    }
    return {};
}

[[nodiscard]] std::string_view forbiddenLaterPhasePrefix(CommittedFailurePoint failurePoint) noexcept
{
    switch (failurePoint)
    {
    case CommittedFailurePoint::PlatformPoll:
        return "state.fixed.";
    case CommittedFailurePoint::FixedUpdate:
        return "state.update.";
    case CommittedFailurePoint::UpdateFrame:
        return "state.extract.";
    case CommittedFailurePoint::ExtractRenderScene:
        return "state.ui.";
    case CommittedFailurePoint::UpdateUI:
        return "render.submit.";
    case CommittedFailurePoint::RenderSubmit:
        return "render.present";
    case CommittedFailurePoint::RenderPresent:
    case CommittedFailurePoint::None:
        return {};
    }
    return {};
}

class CommittedRuntimeFailureTest : public testing::TestWithParam<CommittedFailureCase> {};

TEST_P(CommittedRuntimeFailureTest, StopsLaterPhasesAndPerformsExactlyOnceReverseCleanup)
{
    const auto [failurePoint, outcome] = GetParam();
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{0.02}};
    runtime.failurePoint = failurePoint;
    runtime.failureOutcome = outcome;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    const Core::ErrorCode expectedCode = expectedFailureCode(failurePoint, outcome);
    EXPECT_EQ(runResult.error().code, expectedCode);
    ASSERT_GE(runResult.error().context.size(), 2U);
    EXPECT_EQ(runResult.error().context.front().operation, boundaryOperation(failurePoint));
    EXPECT_EQ(runResult.error().context.back().operation, "EngineHost::run");
    EXPECT_TRUE(runResult.error().context.back().detail.starts_with("frame=0, simulationTick="));
    EXPECT_TRUE(containsEvent(runtime.events, invocationEvent(failurePoint)));
    const std::string_view forbiddenPrefix = forbiddenLaterPhasePrefix(failurePoint);
    if (!forbiddenPrefix.empty())
    {
        EXPECT_FALSE(containsEventPrefix(runtime.events, forbiddenPrefix));
    }
    if (failurePoint == CommittedFailurePoint::PlatformPoll)
    {
        EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    }

    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.exitFailureCode, expectedCode);
    EXPECT_EQ(game.shutdownFailureCode, expectedCode);

    const bool submitWasReached =
        failurePoint == CommittedFailurePoint::RenderSubmit || failurePoint == CommittedFailurePoint::RenderPresent;
    const bool presentWasReached = failurePoint == CommittedFailurePoint::RenderPresent;
    EXPECT_EQ(runtime.submitCalls, submitWasReached ? 1U : 0U);
    EXPECT_EQ(runtime.presentCalls, presentWasReached ? 1U : 0U);
    expectEventSuffix(runtime.events, EventLog({
                                          "state.exit",
                                          "state.destroy",
                                          "game.shutdown",
                                          "render.shutdown",
                                          "render.destroy",
                                          "task.shutdown",
                                          "task.destroy",
                                          "platform.shutdown",
                                          "platform.destroy",
                                      }));
}

INSTANTIATE_TEST_SUITE_P(
    EveryCommittedBoundary, CommittedRuntimeFailureTest,
    testing::Combine(testing::Values(CommittedFailurePoint::PlatformPoll, CommittedFailurePoint::FixedUpdate,
                                     CommittedFailurePoint::UpdateFrame, CommittedFailurePoint::ExtractRenderScene,
                                     CommittedFailurePoint::UpdateUI, CommittedFailurePoint::RenderSubmit,
                                     CommittedFailurePoint::RenderPresent),
                     testing::Values(InjectedOutcome::ReturnError, InjectedOutcome::Throw)));

} // namespace

TEST(EngineHostRunTest, PlatformExitRequestStopsNormallyBeforeStartingAFrame)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{0.02}};
    runtime.platformExitRequested = true;
    GameProbe game;
    game.runtime = &runtime;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::PrimaryWindowRequestedClose);
    EXPECT_EQ(runtime.pollCount, 1U);
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::PrimaryWindowRequestedClose);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::PrimaryWindowRequestedClose);
    EXPECT_FALSE(game.exitFailureCode.has_value());
    EXPECT_FALSE(game.shutdownFailureCode.has_value());
    expectEventSuffix(runtime.events, EventLog({
                                          "state.exit",
                                          "state.destroy",
                                          "game.shutdown",
                                          "render.shutdown",
                                          "render.destroy",
                                          "task.shutdown",
                                          "task.destroy",
                                          "platform.shutdown",
                                          "platform.destroy",
                                      }));
}

TEST(EngineHostRunTest, DispatchesPlatformLifecycleEventsBeforeFrameCallbacks)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.emitPlatformEvent = true;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(game.platformEventCount, 1U);
    ASSERT_TRUE(game.platformEventSubscription.has_value());
    EXPECT_FALSE(game.platformEventSubscription->isActive());
    const auto eventPosition = std::ranges::find(runtime.events, "platform.event");
    const auto updatePosition = std::ranges::find(runtime.events, "state.update.0");
    ASSERT_NE(eventPosition, runtime.events.end());
    ASSERT_NE(updatePosition, runtime.events.end());
    EXPECT_LT(eventPosition, updatePosition);
}

TEST(EngineHostRunTest, UiInputRouteValidationRunsAfterPlatformDispatchAndBeforeGameFramePhases)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.emitPlatformEvent = true;
    runtime.emitUnrepresentableUiPointerMove = true;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(runResult.error().message, "UI pointer transition identity, state, owner, position, or delta is invalid");
    EXPECT_EQ(game.platformEventCount, 1U);
    EXPECT_TRUE(containsEvent(runtime.events, "platform.event"));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.extract."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.ui."));
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitFailureCode, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(game.shutdownFailureCode, RuntimeErrorCode::LifecycleInvariantViolation);
    ASSERT_TRUE(game.platformEventSubscription.has_value());
    EXPECT_FALSE(game.platformEventSubscription->isActive());
    expectEventSuffix(runtime.events, EventLog({
                                          "state.exit",
                                          "state.destroy",
                                          "game.shutdown",
                                          "render.shutdown",
                                          "render.destroy",
                                          "task.shutdown",
                                          "task.destroy",
                                          "platform.shutdown",
                                          "platform.destroy",
                                      }));
}

TEST(EngineHostRunTest, StartupRollbackInvalidatesPlatformEventSubscription)
{
    RuntimeProbe runtime;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.enterMode = EnterMode::ReturnError;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    ASSERT_TRUE(game.platformEventSubscription.has_value());
    EXPECT_FALSE(game.platformEventSubscription->isActive());
    EXPECT_EQ(runtime.pollCount, 0U);
}

TEST(EngineHostRunTest, PlatformEventCallbackFailureStopsBeforeGameFramePhases)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.emitPlatformEvent = true;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.throwFromPlatformEvent = true;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::PlatformEventCallbackThrewException);
    EXPECT_EQ(game.platformEventCount, 1U);
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    ASSERT_TRUE(game.platformEventSubscription.has_value());
    EXPECT_FALSE(game.platformEventSubscription->isActive());
}

class OversizedPlatformFrameBackendTest : public testing::TestWithParam<OversizedPlatformFrameCase> {};

TEST_P(OversizedPlatformFrameBackendTest, HostRejectsFrameBeforePlatformEventDispatchAndGameFramePhases)
{
    const OversizedPlatformFrameCase& testCase = GetParam();
    RuntimeProbe runtime;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);

    const EngineConfig config = undersizedPlatformFrameConfig(testCase.frameKind);
    const Platform::PlatformFrameCapacityConfig builderCapacities = oversizedBuilderCapacities(config);
    EXPECT_GT(builderCapacities.inputTransitionCapacity, config.platformFrameCapacities.inputTransitionCapacity);
    EXPECT_GT(builderCapacities.inputTextByteCapacity, config.platformFrameCapacities.inputTextByteCapacity);
    EXPECT_GT(builderCapacities.platformEventCapacity, config.platformFrameCapacities.platformEventCapacity);
    auto hostResult =
        EngineHost::Create(config, makeOversizedPlatformFrameFactories(runtime, testCase.frameKind, builderCapacities));
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(runResult.error().message, testCase.expectedMessage);
    ASSERT_FALSE(runResult.error().context.empty());
    EXPECT_EQ(runResult.error().context.back().operation, "EngineHost::run");
    EXPECT_EQ(runResult.error().context.back().detail, "frame=0, simulationTick=0");
    if (!testCase.expectedBatchDetail.empty())
    {
        EXPECT_EQ(runResult.error().context.front().operation, "EngineHost::run");
        EXPECT_EQ(runResult.error().context.front().detail, testCase.expectedBatchDetail);
    }

    EXPECT_EQ(runtime.pollCount, 1U);
    EXPECT_EQ(game.platformEventCount, 0U);
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.extract."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.ui."));
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.exitFailureCode, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(game.shutdownFailureCode, RuntimeErrorCode::LifecycleInvariantViolation);
    ASSERT_TRUE(game.platformEventSubscription.has_value());
    EXPECT_FALSE(game.platformEventSubscription->isActive());
}

INSTANTIATE_TEST_SUITE_P(UntrustedBackends, OversizedPlatformFrameBackendTest,
                         testing::Values(
                             OversizedPlatformFrameCase{
                                 .frameKind = OversizedPlatformFrameKind::RawInputBatch,
                                 .testName = "RawInputBatch",
                                 .expectedMessage = "The reserved Platform overflow slot must contain a stream reset",
                                 .expectedBatchDetail = "input transition batch",
                             },
                             OversizedPlatformFrameCase{
                                 .frameKind = OversizedPlatformFrameKind::TextByteBatch,
                                 .testName = "TextAndPreeditBytes",
                                 .expectedMessage = "Platform input text exceeded its configured frame byte capacity",
                                 .expectedBatchDetail = {},
                             },
                             OversizedPlatformFrameCase{
                                 .frameKind = OversizedPlatformFrameKind::PlatformEventBatch,
                                 .testName = "PlatformEventBatch",
                                 .expectedMessage = "The reserved Platform overflow slot must contain a stream reset",
                                 .expectedBatchDetail = "platform event batch",
                             }),
                         [](const testing::TestParamInfo<OversizedPlatformFrameCase>& info) {
                             return std::string(info.param.testName);
                         });

TEST(EngineHostRunTest, RoutesConfiguredInputActionsIntoTheirRuntimePhaseContexts)
{
    constexpr InputActionId JumpAction{101};
    constexpr InputActionId PauseAction{202};

    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{0.011}};
    runtime.heldKeysByFrame = {{
        Platform::Key::A,
        Platform::Key::B,
    }};
    runtime.keyTransitionsByFrame = {{
        ScriptedKeyTransition{
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
        ScriptedKeyTransition{
            .key = Platform::Key::B,
            .state = Platform::DigitalTransition::Down,
        },
    }};

    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    game.actionWiring = ActionWiringProbe{
        .simulationAction = JumpAction,
        .frameAction = PauseAction,
        .enabled = true,
    };
    ScriptedGameApplication application(game);

    auto config = EngineConfig::Defaults();
    config.fixedSimulation = Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.01},
        .maximumAcceptedRealDelta = Core::Duration{0.20},
        .maximumStepsPerFrame = 4,
    };
    config.inputActions.digitalBindings = {
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::A},
            .action = JumpAction,
            .domain = InputActionDomain::Simulation,
        },
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{.key = Platform::Key::B},
            .action = PauseAction,
            .domain = InputActionDomain::Frame,
        },
    };
    auto hostResult = createRuntimeHost(runtime, std::move(config));
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::GameRequestedExitAfterCurrentFrame);
    EXPECT_TRUE(game.actionWiring.fixedObserved);
    EXPECT_TRUE(game.actionWiring.frameObserved);
    ASSERT_GE(game.fixedObservations.size(), 1U);
    EXPECT_EQ(game.fixedObservations.front().frameIndex, 0U);
    EXPECT_EQ(game.fixedObservations.front().simulationTickIndex, 0U);
}

TEST(EngineHostRunTest, RepeatedPlatformFrameIdFailsBeforeDispatchingItsEvents)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero(), Core::Duration::zero()};
    runtime.platformFrameIds = {1, 1};
    runtime.emitPlatformEventOnEveryFrame = true;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.exitOnFrame = 99;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(runtime.pollCount, 2U);
    EXPECT_EQ(game.platformEventCount, 1U);
    EXPECT_TRUE(containsEvent(runtime.events, "state.update.0"));
    EXPECT_FALSE(containsEvent(runtime.events, "state.update.1"));
    EXPECT_EQ(runtime.submittedFrames, 1U);
}

TEST(EngineHostRunTest, PrimaryWindowGenerationChangeFailsBeforeSecondGameFramePhases)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero(), Core::Duration::zero()};
    runtime.replacePrimaryWindowOnFrame = 1U;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 1;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(runtime.pollCount, 2U);
    EXPECT_TRUE(containsEvent(runtime.events, "state.update.0"));
    EXPECT_TRUE(containsEvent(runtime.events, "state.extract.0"));
    EXPECT_TRUE(containsEvent(runtime.events, "state.ui.0"));
    EXPECT_TRUE(containsEvent(runtime.events, "render.submit.0"));
    EXPECT_FALSE(containsEvent(runtime.events, "state.update.1"));
    EXPECT_FALSE(containsEvent(runtime.events, "state.extract.1"));
    EXPECT_FALSE(containsEvent(runtime.events, "state.ui.1"));
    EXPECT_FALSE(containsEvent(runtime.events, "render.submit.1"));
    EXPECT_EQ(runtime.submittedFrames, 1U);
    EXPECT_EQ(runtime.presentedFrames, 1U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.exitFailureCode, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_EQ(game.shutdownFailureCode, RuntimeErrorCode::LifecycleInvariantViolation);
    expectEventSuffix(runtime.events, EventLog({
                                          "state.exit",
                                          "state.destroy",
                                          "game.shutdown",
                                          "render.shutdown",
                                          "render.destroy",
                                          "task.shutdown",
                                          "task.destroy",
                                          "platform.shutdown",
                                          "platform.destroy",
                                      }));
}

TEST(EngineHostRunTest, StartupElapsedTimeIsExcludedFromTheFirstFrameDelta)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.advanceClockDuringEnter = Core::Duration{2.0};
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    ASSERT_EQ(game.fixedCountsByFrame.size(), 1U);
    EXPECT_EQ(game.fixedCountsByFrame.front(), 0U);
    EXPECT_TRUE(game.fixedObservations.empty());
}

TEST(EngineHostRunTest, MonotonicClockRegressionFailsBeforeFrameCallbacksAndCleansUp)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{-0.001}};
    runtime.emitPlatformEvent = true;
    GameProbe game;
    game.runtime = &runtime;
    game.subscribeToPlatformEvents = true;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::MonotonicClockMovedBackward);
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_EQ(game.platformEventCount, 0U);
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
}

TEST(EngineHostRunTest, M7ANullRenderDoesNotFabricateSurfaceSuspension)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_FALSE(runtime.lastSubmittedHadPrimaryWindowSurface);
    EXPECT_EQ(runtime.submittedFrames, 1U);
    EXPECT_EQ(runtime.presentedFrames, 1U);
}

TEST(EngineHostRunTest, FixedStepTimingCoversZeroOneAndMaximumFourStepsWithStableIndices)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {
        Core::Duration{0.0},
        Core::Duration{0.01},
        Core::Duration{0.10},
    };
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 2;
    ScriptedGameApplication application(game);
    auto config = EngineConfig::Defaults();
    config.fixedSimulation = Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.01},
        .maximumAcceptedRealDelta = Core::Duration{0.20},
        .maximumStepsPerFrame = 4,
    };
    auto hostResult = createRuntimeHost(runtime, std::move(config));
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(game.fixedCountsByFrame, std::vector<u32>({0, 1, 4}));
    ASSERT_EQ(game.fixedObservations.size(), 5U);
    EXPECT_EQ(game.fixedObservations[0].frameIndex, 1U);
    EXPECT_EQ(game.fixedObservations[0].simulationTickIndex, 0U);
    EXPECT_EQ(game.fixedObservations[0].fixedStepIndexInFrame, 0U);
    EXPECT_EQ(game.fixedObservations[0].fixedStepCountInFrame, 1U);
    for (u32 index = 0; index < 4; ++index)
    {
        const auto& observation = game.fixedObservations[index + 1U];
        EXPECT_EQ(observation.frameIndex, 2U);
        EXPECT_EQ(observation.simulationTickIndex, static_cast<u64>(index + 1U));
        EXPECT_EQ(observation.fixedStepIndexInFrame, index);
        EXPECT_EQ(observation.fixedStepCountInFrame, 4U);
    }
}

TEST(EngineHostRunTest, RunCanOnlyBeStartedOnce)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    ASSERT_TRUE((*hostResult)->run(application).has_value());
    const std::size_t eventCountAfterFirstRun = runtime.events.size();

    auto secondRun = (*hostResult)->run(application);
    ASSERT_FALSE(secondRun.has_value());
    EXPECT_EQ(secondRun.error().code, RuntimeErrorCode::EngineRunAlreadyStarted);
    EXPECT_EQ(runtime.events.size(), eventCountAfterFirstRun);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
}

TEST(EngineHostRunTest, StartupFailureAlsoConsumesTheSingleRunAttempt)
{
    RuntimeProbe runtime;
    GameProbe game;
    game.runtime = &runtime;
    game.startupMode = StartupMode::ReturnError;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    ASSERT_FALSE((*hostResult)->run(application).has_value());
    const std::size_t eventCountAfterFailure = runtime.events.size();

    auto secondRun = (*hostResult)->run(application);
    ASSERT_FALSE(secondRun.has_value());
    EXPECT_EQ(secondRun.error().code, RuntimeErrorCode::EngineRunAlreadyStarted);
    EXPECT_EQ(runtime.events.size(), eventCountAfterFailure);
}

TEST(EngineHostRunTest, CommittedRuntimeFailureAlsoConsumesTheSingleRunAttempt)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.failurePoint = CommittedFailurePoint::UpdateUI;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    ASSERT_FALSE((*hostResult)->run(application).has_value());
    const std::size_t eventCountAfterFailure = runtime.events.size();

    auto secondRun = (*hostResult)->run(application);
    ASSERT_FALSE(secondRun.has_value());
    EXPECT_EQ(secondRun.error().code, RuntimeErrorCode::EngineRunAlreadyStarted);
    EXPECT_EQ(runtime.events.size(), eventCountAfterFailure);
}

TEST(EngineHostRunTest, NullRuntimeRunsThreeHundredFramesAndShutsDownExactlyOnce)
{
    RuntimeProbe runtime;
    runtime.frameDeltas.resize(300, Core::Duration{1.0 / 60.0});
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 299;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::GameRequestedExitAfterCurrentFrame);
    EXPECT_EQ(runtime.pollCount, 300U);
    EXPECT_EQ(runtime.submittedFrames, 300U);
    EXPECT_EQ(runtime.presentedFrames, 300U);
    EXPECT_TRUE(std::ranges::all_of(game.fixedCountsByFrame, [](u32 fixedStepCount) { return fixedStepCount <= 4U; }));
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
}

} // namespace Tina::Tests
